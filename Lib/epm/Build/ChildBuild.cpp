#include "ChildBuild.h"
#include "Configs.h"
#include "Harvest.h"

#include <epm/Spec/Platform.h>
#include <epm/Util/Command.h>
#include <epm/Util/Log.h>
#include <epm/Util/Paths.h>
#include <epm/Util/Text.h>

#include <eacp/Core/Platform/Platform.h>
#include <eacp/Core/Utils/Strings.h>

namespace epm
{
using namespace eacp;

namespace
{
constexpr auto failureTailLines = 40;
constexpr auto exportScriptName = "epm-export.cmake";
constexpr auto nestedScriptName = "epm-nested.cmake";
constexpr auto nestedGraphName = "epm-graph.cmake";

bool hasDependencies(const ChildBuild& build)
{
    return !build.dependencyGraph.empty();
}

std::string topLevelIncludes(const ChildBuild& build)
{
    const auto exportScript = (build.buildDirectory / exportScriptName).str();

    if (!hasDependencies(build))
        return exportScript;

    return exportScript + ";" + (build.buildDirectory / nestedScriptName).str();
}

// The consumer's resolved SDK directory, for every Apple child build: without
// it a native macOS child compiles against whatever SDK its own environment
// defaults to (SDKROOT, xcode-select), not the one sdkVersion records.
std::string childOsxSysroot(const Env& env)
{
    if (!env.osxSysrootPath.empty())
        return env.osxSysrootPath;

    return crossCompilesWithoutToolchain(env) ? env.osxSysroot : std::string {};
}

void addDefinition(Vector<std::string>& arguments,
                   std::string_view name,
                   const std::string& value)
{
    if (!value.empty())
        arguments.add("-D" + std::string {name} + "=" + value);
}

ChildBuildResult runStep(const ChildBuild& build,
                         std::string_view step,
                         const Vector<std::string>& arguments)
{
    Log::step("epm: " + std::string {step} + " " + build.request.name + " ("
              + build.config + ")");

    const auto result = runCommand(build.env.cmake, arguments);
    const auto log = build.buildDirectory / ("epm-" + std::string {step} + ".log");
    writeTextFile(log, result.output);

    if (result.ok)
        return {true, {}, {}};

    return {false,
            std::string {step} + " of " + build.request.name + " (" + build.config
                + ") failed: " + result.error + " - full log: " + log.str(),
            Log::isVerbose() ? std::string {}
                             : lastLines(result.output, failureTailLines)};
}
} // namespace

Generator chooseGenerator(const Env& env, const std::optional<FilePath>& ninja)
{
    if (!env.isMultiConfig && !env.generator.empty())
        return {env.generator, env.makeProgram};

    if (ninja)
        return {"Ninja", ninja->str()};

    if (env.generator == "Ninja Multi-Config" && !env.makeProgram.empty())
        return {"Ninja", env.makeProgram};

    if (env.generator.starts_with("Visual Studio"))
        return {env.generator,
                {},
                env.generatorPlatform,
                env.generatorToolset,
                env.generatorInstance};

    if (Platform::isWindows())
        return {};

    return {"Unix Makefiles", {}};
}

Vector<std::string> filePrefixMaps(const ChildBuild& build)
{
    auto maps = Vector<std::string> {};

    if (msvcAbi(build.env))
        return maps;

    const auto& name = build.request.name;
    const auto sourceRoot = sourceRootOf(build.cacheRoot, build.sourceDirectory);
    const auto directories = {
        std::pair {build.cacheRoot, std::string {"/epm/cache"}},
        std::pair {sourceRoot, "/epm/src/" + name},
        std::pair {build.buildDirectory, "/epm/build/" + name},
        std::pair {build.cpmCache, std::string {"/epm/cpm"}}};

    for (const auto& [directory, stable]: directories)
    {
        if (directory.empty())
            continue;

        for (const auto& spelling: {directory, resolvedPath(directory)})
            maps.addIfNotThere(spelling.str() + "=" + stable);
    }

    return maps;
}

Vector<std::string> configureArguments(const ChildBuild& build)
{
    const auto& env = build.env;
    const auto exportFile = build.prefix / ("export-" + build.config + ".cmake");

    auto arguments = Vector<std::string> {
        "-S", build.sourceDirectory.str(), "-B", build.buildDirectory.str()};

    const auto& generator = build.generator;

    for (const auto& [flag, value]: {std::pair {"-G", generator.name},
                                     std::pair {"-A", generator.platform},
                                     std::pair {"-T", generator.toolset}})
    {
        if (!value.empty())
            arguments.add({flag, value});
    }

    addDefinition(arguments, "CMAKE_GENERATOR_INSTANCE", generator.instance);
    addDefinition(arguments, "CMAKE_MAKE_PROGRAM", generator.makeProgram);
    addDefinition(arguments, "CMAKE_BUILD_TYPE", build.config);
    addDefinition(arguments, "CMAKE_TOOLCHAIN_FILE", env.toolchainFile);

    if (crossCompilesWithoutToolchain(env))
    {
        addDefinition(arguments, "CMAKE_SYSTEM_NAME", env.systemName);
        addDefinition(arguments, "CMAKE_SYSTEM_PROCESSOR", env.systemProcessor);

        if (env.systemName == "Android")
        {
            addDefinition(arguments, "CMAKE_ANDROID_ARCH_ABI", env.androidAbi);
            addDefinition(arguments, "CMAKE_ANDROID_NDK", env.androidNdk);
            addDefinition(arguments, "CMAKE_SYSTEM_VERSION", env.androidApi);
            addDefinition(arguments, "CMAKE_ANDROID_STL_TYPE", env.androidStl);
        }
    }
    else if (env.systemName == "Android" && !env.toolchainFile.empty())
    {
        // The NDK's toolchain file reads these, and otherwise defaults them
        // (armeabi-v7a, its minimum API level, c++_static).
        addDefinition(arguments, "ANDROID_ABI", env.androidAbi);
        addDefinition(arguments,
                      "ANDROID_PLATFORM",
                      env.androidApi.empty() ? "" : "android-" + env.androidApi);
        addDefinition(arguments, "ANDROID_STL", env.androidStl);
    }

    addDefinition(arguments, "CMAKE_OSX_SYSROOT", childOsxSysroot(env));

    // epm.cmake names a C compiler for a C++-only consumer (epm_c_compiler);
    // this covers an Env without one. CMake refuses a child build mixing
    // clang-cl for C++ with the cl it would pick for C, and both compile C
    // with the same executable, so the child gets it for C too.
    const auto cCompiler =
        env.cCompiler.empty() && msvcAbi(env) ? env.cxxCompiler : env.cCompiler;

    addDefinition(arguments, "CMAKE_C_COMPILER", cCompiler);
    addDefinition(arguments, "CMAKE_CXX_COMPILER", env.cxxCompiler);
    addDefinition(arguments, "CMAKE_OSX_ARCHITECTURES", env.osxArchitectures);
    addDefinition(arguments, "CMAKE_OSX_DEPLOYMENT_TARGET", env.osxDeploymentTarget);
    addDefinition(arguments, "CMAKE_CXX_STANDARD", env.cxxStandard);

    if (msvcAbi(env))
    {
        addDefinition(arguments, "CMAKE_MSVC_RUNTIME_LIBRARY", msvcRuntimeOf(env));
        addDefinition(arguments, "CMAKE_POLICY_DEFAULT_CMP0091", "NEW");
    }

    addDefinition(
        arguments, "EPM_FILE_PREFIX_MAPS", joinList(filePrefixMaps(build), ";"));

    const auto archives = (build.prefix / "lib" / build.config).str();
    addDefinition(arguments, "CMAKE_ARCHIVE_OUTPUT_DIRECTORY", archives);
    addDefinition(arguments,
                  "CMAKE_ARCHIVE_OUTPUT_DIRECTORY_" + Strings::toUpper(build.config),
                  archives);
    addDefinition(arguments, "CPM_SOURCE_CACHE", build.cpmCache.str());
    addDefinition(
        arguments, "CMAKE_PROJECT_TOP_LEVEL_INCLUDES", topLevelIncludes(build));
    addDefinition(arguments, "EPM_EXPORT_FILE", exportFile.str());
    addDefinition(arguments, "EPM_EXPORT_NAMESPACE", exportNamespace(build.config));

    if (hasDependencies(build))
    {
        addDefinition(arguments, "EPM_NESTED", "ON");
        addDefinition(
            arguments, "EPM_GRAPH", (build.buildDirectory / nestedGraphName).str());
        addDefinition(arguments, "EPM_EXECUTABLE", build.epmExecutable.str());
    }

    for (const auto& [key, value]: build.request.options)
        arguments.add("-D" + key + "=" + value);

    for (const auto& [name, value]: build.definitions)
        arguments.add("-D" + name + "=" + value);

    return arguments;
}

Vector<std::string> buildArguments(const ChildBuild& build)
{
    return {"--build",
            build.buildDirectory.str(),
            "--config",
            build.config,
            "--parallel"};
}

ChildBuildResult configureChildBuild(const ChildBuild& build)
{
    auto files = Vector<std::pair<FilePath, std::string_view>> {
        {build.buildDirectory / exportScriptName, embeddedExportScript()}};

    if (hasDependencies(build))
    {
        files.add({build.buildDirectory / nestedScriptName, embeddedNestedScript()});
        files.add({build.buildDirectory / nestedGraphName, build.dependencyGraph});
    }

    for (const auto& [path, text]: files)
        if (!writeTextFile(path, text))
            return {false, "cannot write " + path.str(), {}};

    return runStep(build, "configure", configureArguments(build));
}

ChildBuildResult buildChildBuild(const ChildBuild& build)
{
    return runStep(build, "build", buildArguments(build));
}
} // namespace epm
