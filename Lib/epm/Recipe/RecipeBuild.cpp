#include "RecipeBuild.h"

#include <epm/Cache/CrossProcessLock.h>
#include <epm/Util/Command.h>
#include <epm/Util/Log.h>
#include <eacp/Core/Utils/Sha256.h>
#include <epm/Util/Text.h>

#include <eacp/Core/Platform/Platform.h>
#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/StdPath.h>

#include <filesystem>

namespace epm
{
using namespace eacp;

namespace
{
constexpr auto moduleFileName =
    Platform::isApple() ? "epm-recipe.dylib" : "epm-recipe.so";
constexpr auto failureLines = 30;

// The module resolves its symbols against this very binary, so a rebuilt epm
// gets fresh modules. Hashed once per process.
const std::string& executableHash()
{
    static const auto hash = computeFileSha256Hex(Files::executablePath());
    return hash;
}

std::string commandLine(const std::string& executable,
                        const Vector<std::string>& arguments)
{
    return executable + " " + eacp::Strings::join(arguments, " ");
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

RecipeBuild
    compile(const FilePath& source, const FilePath& module, const FilePath& log)
{
    const auto& sdk = recipeSdk();
    const auto temporary =
        module.sibling(module.stem() + ".tmp" + module.extension());
    const auto arguments = recipeCompileArguments(sdk, source, temporary);
    const auto command = commandLine(sdk.compiler, arguments);

    Log::step("compiling " + source.str());
    Log::step(command);

    const auto result = runCommand(sdk.compiler, arguments);
    Files::writeFileAtomically(log, command + "\n\n" + result.output);

    auto build = RecipeBuild {};
    build.module = module;
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

    if (!Files::moveFile(temporary, module))
    {
        build.error = "cannot move the recipe module into " + module.str();
        return build;
    }

    build.ok = true;
    return build;
}
} // namespace

std::string recipeKey(const RecipeSdk& sdk,
                      std::string_view executableHash,
                      std::string_view source)
{
    return computeSha256Hex(sdk.identity + "\n" + std::string {executableHash} + "\n"
                            + computeSha256Hex(source));
}

Vector<std::string> recipeCompileArguments(const RecipeSdk& sdk,
                                           const FilePath& source,
                                           const FilePath& module)
{
    auto arguments = Vector<std::string> {};
    arguments.add((sdk.cxxExtensions ? "-std=gnu++" : "-std=c++") + sdk.cxxStandard);
    arguments.add({"-shared", "-fPIC", "-fvisibility=hidden"});

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

    // The module's undefined symbols are epm's, bound when it is loaded.
    if (Platform::isApple())
        arguments.add("-Wl,-undefined,dynamic_lookup");

    // So the last dlclose really unmaps it (eacp's EacpPlugin.cmake).
    if (sdk.compilerId == "GNU")
        arguments.add("-fno-gnu-unique");

    arguments.add({"-o", module.str(), source.str()});
    return arguments;
}

RecipeBuild buildRecipe(const FilePath& sourceToBuild, const CacheRoot& cache)
{
    if (Platform::isWindows())
        return failure("epm.cpp recipes are not supported on Windows yet");

    // Absolute, so the compiler's diagnostics in compile.log name it fully.
    auto failed = std::error_code {};
    const auto absolute =
        std::filesystem::absolute(toStdPath(sourceToBuild), failed);
    const auto source = failed ? sourceToBuild : FilePath {absolute};

    if (!Files::isFile(source))
        return failure("no recipe at " + source.str());

    if (executableHash().empty())
        return failure("cannot hash the epm executable "
                       + Files::executablePath().str());

    const auto key =
        recipeKey(recipeSdk(), executableHash(), Files::readFile(source))
            .substr(0, 12);
    const auto directory = cache.recipe() / key;
    const auto module = directory / moduleFileName;
    const auto log = directory / "compile.log";

    auto cached = RecipeBuild {};
    cached.ok = true;
    cached.cached = true;
    cached.module = module;
    cached.log = log;

    if (Files::isFile(module))
        return cached;

    if (!Files::isFile(recipeSdk().compiler))
        return failure("the compiler that built epm is gone: "
                       + recipeSdk().compiler);

    const auto lock =
        CrossProcessLock {cache, "recipe-" + key, crossProcessLockTimeout};

    if (!lock.isLocked())
        return failure("timed out waiting for another epm to compile "
                       + source.str());

    if (Files::isFile(module))
        return cached;

    Files::createDirectories(directory);
    return compile(source, module, log);
}
} // namespace epm
