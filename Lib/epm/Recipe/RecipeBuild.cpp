#include "RecipeBuild.h"

#include <epm/Cache/CrossProcessLock.h>
#include <epm/Util/Command.h>
#include <epm/Util/Log.h>
#include <eacp/Core/Utils/Sha256.h>
#include <epm/Util/Text.h>

#include <ResEmbed/ResEmbed.h>

#include <eacp/Core/Platform/Platform.h>
#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/StdPath.h>

#include <filesystem>
#include <stdexcept>

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

RecipeBuild compile(const RecipeToolchain& toolchain,
                    const FilePath& source,
                    const FilePath& module,
                    const FilePath& log)
{
    const auto& sdk = recipeSdk();
    const auto temporary =
        module.sibling(module.stem() + ".tmp" + module.extension());
    const auto arguments = recipeCompileArguments(sdk, toolchain, source, temporary);
    const auto command = commandLine(toolchain.compiler, arguments);

    Log::step("compiling " + source.str());
    Log::step(command);

    const auto result = runCommand(toolchain.compiler, arguments);
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
constexpr auto recipeHeadersCategory = "RecipeHeaders";
constexpr auto binaryHashLength = 12;
} // namespace

FilePath writeRecipeHeaders(const CacheRoot& cache)
{
    const auto root =
        cache.path() / "recipe-sdk" / executableHash().substr(0, binaryHashLength);
    const auto include = root / "include";
    const auto complete = root / "complete";

    if (Files::isFile(complete))
        return include;

    const auto headers = ResEmbed::getCategory(recipeHeadersCategory);

    if (headers.empty())
        throw std::runtime_error("this epm embeds no recipe headers");

    const auto lock =
        CrossProcessLock {cache, "recipe-sdk", crossProcessLockTimeout};

    if (!lock.isLocked())
        throw std::runtime_error("timed out waiting for another epm to write "
                                 + include.str());

    if (Files::isFile(complete))
        return include;

    for (const auto& [name, data]: headers)
        Files::writeFileAtomically(include / name, data.toStringView());

    Files::writeFileAtomically(complete, std::string_view {});
    return include;
}

RecipeToolchain recipeToolchainFor(const Env& env, const CacheRoot& cache)
{
    return {env.recipeCompiler,
            env.cxxCompilerId,
            env.osxSysroot,
            writeRecipeHeaders(cache)};
}

std::string recipeKey(const RecipeSdk& sdk,
                      const RecipeToolchain& toolchain,
                      std::string_view executableHash,
                      std::string_view source)
{
    return computeSha256Hex(
        sdk.identity + "\n" + toolchain.compiler + "\n" + toolchain.osxSysroot + "\n"
        + std::string {executableHash} + "\n" + computeSha256Hex(source));
}

Vector<std::string> recipeCompileArguments(const RecipeSdk& sdk,
                                           const RecipeToolchain& toolchain,
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

    if (!toolchain.osxSysroot.empty())
        arguments.add({"-isysroot", toolchain.osxSysroot});

    for (const auto& architecture: sdk.osxArchitectures)
        arguments.add({"-arch", architecture});

    if (!sdk.osxDeploymentTarget.empty())
        arguments.add("-mmacosx-version-min=" + sdk.osxDeploymentTarget);

    arguments.add("-I" + toolchain.includeDirectory.str());

    for (const auto& definition: sdk.compileDefinitions)
        arguments.add("-D" + definition);

    // epm-core's warning flags are only meant for the compiler that built it.
    if (toolchain.compilerId == sdk.compilerId)
    {
        for (const auto& option: sdk.compileOptions)
            arguments.add(option);
    }

    // The module's undefined symbols are epm's, bound when it is loaded.
    if (Platform::isApple())
        arguments.add("-Wl,-undefined,dynamic_lookup");

    // So the last dlclose really unmaps it (eacp's EacpPlugin.cmake).
    if (toolchain.compilerId == "GNU")
        arguments.add("-fno-gnu-unique");

    arguments.add({"-o", module.str(), source.str()});
    return arguments;
}

RecipeBuild buildRecipe(const FilePath& sourceToBuild,
                        const CacheRoot& cache,
                        const Env& env)
{
    if (Platform::isWindows())
        return failure("epm.cpp recipes are not supported on Windows yet");

    if (env.recipeCompiler.empty())
        return failure("no C++ compiler to build " + sourceToBuild.str()
                       + " with: the configure found none");

    if (!Files::isFile(FilePath {env.recipeCompiler}))
        return failure("the configure's C++ compiler " + env.recipeCompiler
                       + " does not exist");

    auto toolchain = RecipeToolchain {};

    try
    {
        toolchain = recipeToolchainFor(env, cache);
    }
    catch (const std::exception& error)
    {
        return failure(error.what());
    }

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
        recipeKey(recipeSdk(), toolchain, executableHash(), Files::readFile(source))
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

    const auto lock =
        CrossProcessLock {cache, "recipe-" + key, crossProcessLockTimeout};

    if (!lock.isLocked())
        return failure("timed out waiting for another epm to compile "
                       + source.str());

    if (Files::isFile(module))
        return cached;

    Files::createDirectories(directory);
    return compile(toolchain, source, module, log);
}
} // namespace epm
