#include "RecipeBuild.h"

#include <epm/Cache/PackageLock.h>
#include <epm/Util/Command.h>
#include <epm/Util/Log.h>
#include <epm/Util/Paths.h>
#include <epm/Util/Sha256.h>
#include <epm/Util/Text.h>

#include <eacp/Core/Platform/Platform.h>
#include <eacp/Core/Utils/Files.h>

namespace epm
{
using namespace eacp;

namespace
{
constexpr auto executableFileName =
    Platform::isWindows() ? "epm-recipe.exe" : "epm-recipe";
constexpr auto failureLines = 30;

// The binary is rebuilt with whatever a recipe compiles against, so its hash
// covers what recipeInputsHash leaves out: eacp's and Miro's headers. Hashed
// once per process.
const std::string& executableHash()
{
    static const auto hash =
        sha256HexOfFile(Files::executablePath()).value_or(std::string {});
    return hash;
}

const std::optional<std::string>& inputsHash()
{
    static const auto hash = recipeInputsHash(recipeSdk());
    return hash;
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

std::string firstErrorLine(std::string_view output)
{
    for (const auto line: splitLines(output))
        if (line.find("error:") != std::string_view::npos)
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

RecipeBuild
    compile(const FilePath& source, const FilePath& executable, const FilePath& log)
{
    const auto& sdk = recipeSdk();
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

RecipeBuild buildRecipe(const FilePath& sourceToBuild, const CacheRoot& cache)
{
    // MSVC needs its own argument shape (/std:, /Fe, .lib names), not a
    // different design: a recipe is an executable on every platform.
    if (Platform::isWindows())
        return failure("epm.cpp recipes are not supported on Windows yet");

    // Absolute, so the compiler's diagnostics in compile.log name it fully.
    const auto source = resolvedPath(sourceToBuild);

    if (!Files::isFile(source))
        return failure("no recipe at " + source.str());

    if (executableHash().empty())
        return failure("cannot hash the epm executable "
                       + Files::executablePath().str());

    if (recipeLibraries(recipeSdk()).empty())
        return failure("this epm has no recipe libraries for its configuration '"
                       + recipeSdk().config + "'");

    if (!inputsHash())
        return failure("cannot read this epm's recipe libraries and headers");

    const auto key =
        recipeKey(
            recipeSdk(), executableHash(), *inputsHash(), Files::readFile(source))
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

    if (!Files::isFile(recipeSdk().compiler))
        return failure("the compiler that built epm is gone: "
                       + recipeSdk().compiler);

    const auto lock = PackageLock {cache, "recipe-" + key, packageLockTimeout};

    if (!lock.isLocked())
        return failure("timed out waiting for another epm to compile "
                       + source.str());

    if (Files::isFile(executable))
        return cached;

    Files::createDirectories(directory);
    return compile(source, executable, log);
}
} // namespace epm
