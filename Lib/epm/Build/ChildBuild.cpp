#include "ChildBuild.h"
#include "Configs.h"

#include <epm/Util/Command.h>
#include <epm/Util/Log.h>
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

    if (Platform::isWindows())
        return {};

    return {"Unix Makefiles", {}};
}

Vector<std::string> configureArguments(const ChildBuild& build)
{
    const auto& env = build.env;
    const auto exportFile = build.prefix / ("export-" + build.config + ".cmake");

    auto arguments = Vector<std::string> {
        "-S", build.sourceDirectory.str(), "-B", build.buildDirectory.str()};

    if (!build.generator.name.empty())
    {
        arguments.add("-G");
        arguments.add(build.generator.name);
    }

    addDefinition(arguments, "CMAKE_MAKE_PROGRAM", build.generator.makeProgram);
    addDefinition(arguments, "CMAKE_BUILD_TYPE", build.config);
    addDefinition(arguments, "CMAKE_TOOLCHAIN_FILE", env.toolchainFile);
    addDefinition(arguments, "CMAKE_C_COMPILER", env.cCompiler);
    addDefinition(arguments, "CMAKE_CXX_COMPILER", env.cxxCompiler);
    addDefinition(arguments, "CMAKE_OSX_ARCHITECTURES", env.osxArchitectures);
    addDefinition(arguments, "CMAKE_OSX_DEPLOYMENT_TARGET", env.osxDeploymentTarget);
    addDefinition(arguments, "CMAKE_CXX_STANDARD", env.cxxStandard);
    addDefinition(arguments,
                  "CMAKE_ARCHIVE_OUTPUT_DIRECTORY",
                  (build.prefix / "lib" / build.config).str());
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

    return arguments;
}

Vector<std::string> buildArguments(const ChildBuild& build)
{
    return {"--build", build.buildDirectory.str(), "--parallel"};
}

ChildBuildResult runChildBuild(const ChildBuild& build)
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

    auto configured = runStep(build, "configure", configureArguments(build));

    if (!configured.ok)
        return configured;

    return runStep(build, "build", buildArguments(build));
}
} // namespace epm
