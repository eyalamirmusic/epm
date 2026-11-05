#include "RecipeBuild.h"

#include <epm/Cache/PackageLock.h>
#include <epm/Util/Command.h>
#include <epm/Util/Log.h>
#include <epm/Util/Paths.h>
#include <epm/Util/Sha256.h>
#include <epm/Util/Text.h>

#include <eacp/Core/Platform/Platform.h>
#include <eacp/Core/Utils/Environment.h>
#include <eacp/Core/Utils/Files.h>

#include <map>
#include <mutex>

namespace epm
{
using namespace eacp;

namespace
{
constexpr auto executableFileName =
    Platform::isWindows() ? "epm-recipe.exe" : "epm-recipe";
constexpr auto failureLines = 30;

// What recipes are built against for one epm binary: its SDK, the binary's
// hash and recipeInputsHash. The binary is rebuilt with whatever a recipe
// compiles against, so its hash covers what recipeInputsHash leaves out:
// eacp's and Miro's headers.
struct RecipeToolchain
{
    FilePath epmExecutable;
    RecipeSdk sdk;
    std::string executableHash;
    std::optional<std::string> inputsHash;
};

// The toolchain of `epmExecutable`, this executable when empty. Located and
// hashed once per binary and process.
const RecipeToolchain& toolchainFor(const FilePath& epmExecutable)
{
    static auto mutex = std::mutex {};
    static auto toolchains = std::map<std::string, RecipeToolchain> {};

    const auto epm = epmExecutable.empty() ? Files::executablePath() : epmExecutable;
    const auto key = resolvedPath(epm).str();
    const auto lock = std::lock_guard {mutex};

    if (const auto found = toolchains.find(key); found != toolchains.end())
        return found->second;

    auto toolchain = RecipeToolchain {};
    toolchain.epmExecutable = epm;
    toolchain.sdk = recipeSdkFor(epm);
    toolchain.executableHash = sha256HexOfFile(epm).value_or(std::string {});
    toolchain.inputsHash = recipeInputsHash(toolchain.sdk);
    return toolchains.emplace(key, std::move(toolchain)).first->second;
}

// epm's own headers: those under the include directory holding
// <epm/Recipe/Recipe.h>, the build tree's sources or the install's include/.
Vector<FilePath> epmHeaders(const RecipeSdk& sdk)
{
    for (const auto& directory: sdk.includeDirectories)
    {
        const auto headers = FilePath {directory} / "epm";

        if (!Files::isFile(headers / "Recipe" / "Recipe.h"))
            continue;

        auto options = Files::DirectoryOptions {};
        options.recursive = true;
        options.pattern = "*.h";
        return Files::listFiles(headers, options);
    }

    return {};
}

std::string commandLine(const std::string& executable,
                        const Vector<std::string>& arguments)
{
    return executable + " " + joinList(arguments, " ");
}

// "error:" (GCC, Clang, clang-cl), or cl's "error C2065:".
std::string firstErrorLine(std::string_view output)
{
    for (const auto line: splitLines(output))
        if (line.find("error:") != std::string_view::npos
            || line.find("error C") != std::string_view::npos)
            return std::string {line};

    return {};
}

RecipeBuild failure(std::string error, std::string details = {})
{
    auto build = RecipeBuild {};
    build.error = std::move(error);
    build.details = std::move(details);
    return build;
}

// A link item as CMake's target_link_libraries takes it, as compiler driver
// arguments: "-framework Foundation" is two, a bare name is a system library.
void addLinkOption(Vector<std::string>& arguments, const std::string& option)
{
    constexpr auto framework = std::string_view {"-framework "};

    if (option.starts_with(framework))
        arguments.add({"-framework", option.substr(framework.size())});
    else if (option.starts_with('-') || option.find('/') != std::string::npos)
        arguments.add(option);
    else
        arguments.add("-l" + option);
}

// The same after cl's /link: a bare name is a system library, <name>.lib.
void addClLinkOption(Vector<std::string>& arguments, const std::string& option)
{
    const auto isName = !option.starts_with('/') && !option.starts_with('-')
                        && option.find_first_of("/\\.") == std::string::npos;
    arguments.add(isName ? option + ".lib" : option);
}

// /std: takes the standards cl names; anything newer is c++latest.
std::string clStandard(const std::string& standard)
{
    for (const auto known: {"14", "17", "20"})
        if (standard == known)
            return "/std:c++" + standard;

    return "/std:c++latest";
}

// cl's spelling of an MSVC_RUNTIME_LIBRARY value.
std::string clRuntimeFlag(const std::string& runtime)
{
    const auto debug = runtime.find("Debug") != std::string::npos;
    const auto dll = runtime.ends_with("DLL");
    return std::string {dll ? "/MD" : "/MT"} + (debug ? "d" : "");
}

Vector<std::string> gnuCompileArguments(const RecipeSdk& sdk,
                                        const FilePath& source,
                                        const FilePath& output)
{
    auto arguments = Vector<std::string> {};
    arguments.add((sdk.cxxExtensions ? "-std=gnu++" : "-std=c++") + sdk.cxxStandard);

    for (const auto& flag: sdk.flags)
        arguments.add(flag);

    // After the configuration's flags: a recipe is a few calls, so a quick
    // compile beats a Release -O3.
    arguments.add("-O1");

    if (!sdk.osxSysroot.empty())
        arguments.add({"-isysroot", sdk.osxSysroot});

    for (const auto& architecture: sdk.osxArchitectures)
        arguments.add({"-arch", architecture});

    if (!sdk.osxDeploymentTarget.empty())
        arguments.add("-mmacosx-version-min=" + sdk.osxDeploymentTarget);

    for (const auto& directory: sdk.includeDirectories)
        arguments.add("-I" + directory);

    for (const auto& definition: sdk.compileDefinitions)
        arguments.add("-D" + definition);

    for (const auto& option: sdk.compileOptions)
        arguments.add(option);

    arguments.add({"-o", output.str(), source.str()});

    // After the source, each archive before those it depends on: a
    // single-pass linker (GNU ld) only takes from an archive what is already
    // undefined.
    for (const auto& library: recipeLibraries(sdk))
        arguments.add(library);

    for (const auto& option: sdk.linkOptions)
        addLinkOption(arguments, option);

    return arguments;
}

// cl and clang-cl: the configuration's flags as they are (its /O level, and
// a Debug /RTC1 that no /O may follow), the archives' runtime library, the
// object beside the executable rather than in the working directory, then
// the linker's arguments after /link.
Vector<std::string> clCompileArguments(const RecipeSdk& sdk,
                                       const FilePath& source,
                                       const FilePath& output)
{
    auto arguments = Vector<std::string> {"/nologo", clStandard(sdk.cxxStandard)};

    for (const auto& flag: sdk.flags)
        arguments.add(flag);

    if (!sdk.msvcRuntime.empty())
        arguments.add(clRuntimeFlag(sdk.msvcRuntime));

    for (const auto& directory: sdk.includeDirectories)
        arguments.add("/I" + directory);

    for (const auto& definition: sdk.compileDefinitions)
        arguments.add("/D" + definition);

    for (const auto& option: sdk.compileOptions)
        arguments.add(option);

    arguments.add({"/Fo" + output.parentDirectory().str() + "/",
                   "/Fe:" + output.str(),
                   source.str(),
                   "/link"});

    for (const auto& library: recipeLibraries(sdk))
        arguments.add(library);

    for (const auto& option: sdk.linkOptions)
        addClLinkOption(arguments, option);

    return arguments;
}

// cl finds its headers and libraries through INCLUDE and LIB, which a
// developer command prompt (vcvarsall) sets; outside one the compile fails
// on the first standard header, so say why first.
bool msvcEnvironmentIsSet()
{
    return !getEnvValue("INCLUDE").empty() && !getEnvValue("LIB").empty();
}

RecipeBuild compile(const RecipeSdk& sdk,
                    const FilePath& source,
                    const FilePath& executable,
                    const FilePath& log)
{
    const auto temporary =
        executable.sibling(executable.stem() + ".tmp" + executable.extension());
    const auto arguments = recipeCompileArguments(sdk, source, temporary);
    const auto command = commandLine(sdk.compiler, arguments);

    Log::step("compiling " + source.str());
    Log::step(command);

    const auto result = runCommand(sdk.compiler, arguments);
    writeTextFile(log, command + "\n\n" + result.output);

    auto build = RecipeBuild {};
    build.executable = executable;
    build.log = log;

    if (!result.ok)
    {
        Files::removeFile(temporary);
        build.error = "the recipe " + source.str() + " did not compile ("
                      + result.error + "); see " + log.str();
        build.details = lastLines(result.output, failureLines);
        build.firstDiagnostic = firstErrorLine(result.output);
        return build;
    }

    if (!Files::moveFile(temporary, executable))
    {
        Files::removeFile(temporary);
        build.error = "cannot move the recipe executable into " + executable.str();
        return build;
    }

    build.ok = true;
    return build;
}
} // namespace

std::optional<std::string> recipeInputsHash(const RecipeSdk& sdk)
{
    auto files = Vector<FilePath> {};

    for (const auto& library: recipeLibraries(sdk))
        files.add(FilePath {library});

    for (const auto& header: epmHeaders(sdk))
        files.add(header);

    auto lines = std::string {};

    for (const auto& file: files)
    {
        const auto hash = sha256HexOfFile(file);

        if (!hash)
            return std::nullopt;

        lines += *hash + " " + file.str() + "\n";
    }

    return sha256Hex(lines);
}

std::string recipeKey(const RecipeSdk& sdk,
                      std::string_view executableHash,
                      std::string_view inputsHash,
                      std::string_view source)
{
    return sha256Hex(sdk.identity + "\n" + std::string {executableHash} + "\n"
                     + std::string {inputsHash} + "\n" + sha256Hex(source));
}

Vector<std::string> recipeCompileArguments(const RecipeSdk& sdk,
                                           const FilePath& source,
                                           const FilePath& output)
{
    return clCommandLine(sdk) ? clCompileArguments(sdk, source, output)
                              : gnuCompileArguments(sdk, source, output);
}

RecipeBuild buildRecipe(const FilePath& sourceToBuild,
                        const CacheRoot& cache,
                        const FilePath& epmExecutable)
{
    // Absolute, so the compiler's diagnostics in compile.log name it fully.
    const auto source = resolvedPath(sourceToBuild);

    if (!Files::isFile(source))
        return failure("no recipe at " + source.str());

    const auto& toolchain = toolchainFor(epmExecutable);
    const auto& sdk = toolchain.sdk;

    if (toolchain.executableHash.empty())
        return failure("cannot hash the epm executable "
                       + toolchain.epmExecutable.str());

    if (recipeLibraries(sdk).empty())
        return failure("this epm has no recipe libraries for its configuration '"
                       + sdk.config + "'");

    if (!toolchain.inputsHash)
        return failure("cannot read this epm's recipe libraries and headers");

    const auto key = recipeKey(sdk,
                               toolchain.executableHash,
                               *toolchain.inputsHash,
                               Files::readFile(source))
                         .substr(0, 12);
    const auto directory = cache.recipe() / key;
    const auto executable = directory / executableFileName;
    const auto log = directory / "compile.log";

    auto cached = RecipeBuild {};
    cached.ok = true;
    cached.cached = true;
    cached.executable = executable;
    cached.log = log;

    if (Files::isFile(executable))
        return cached;

    if (!Files::isFile(sdk.compiler))
        return failure("the compiler that built epm is gone: " + sdk.compiler);

    if (clCommandLine(sdk) && !msvcEnvironmentIsSet())
        return failure("cannot compile " + source.str() + " with " + sdk.compiler
                       + ": INCLUDE and LIB are not set; run from a Visual "
                         "Studio developer command prompt (vcvarsall)");

    const auto lock = PackageLock {cache, "recipe-" + key, packageLockTimeout};

    if (!lock.isLocked())
        return failure("timed out waiting for another epm to compile "
                       + source.str());

    if (Files::isFile(executable))
        return cached;

    Files::createDirectories(directory);
    return compile(sdk, source, executable, log);
}

std::string recipeBuildError(const RecipeBuild& build)
{
    if (build.firstDiagnostic.empty())
        return build.error;

    return build.error + "\n  " + build.firstDiagnostic;
}
} // namespace epm
