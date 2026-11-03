#pragma once

#include <epm/Cache/CacheRoot.h>
#include <epm/Spec/Env.h>
#include <epm/Spec/Request.h>

#include <optional>
#include <string>
#include <string_view>

namespace epm
{
struct Generator
{
    // Empty: leave -G out and take CMake's platform default.
    std::string name;

    // Empty: let CMake find the build tool.
    std::string makeProgram;
};

// The consumer's generator when it is single-config, else Ninja when it is on
// PATH, else Unix Makefiles (the platform default on Windows).
Generator chooseGenerator(const Env& env,
                          const std::optional<eacp::FilePath>& ninja);

// One configuration of a dependency, built straight into a binary cache prefix.
struct ChildBuild
{
    Request request;
    Env env;
    Generator generator;
    eacp::FilePath sourceDirectory;
    eacp::FilePath buildDirectory;
    eacp::FilePath prefix;
    eacp::FilePath cpmCache;
    std::string config;

    // graph.cmake (Docs/Graph.md §7) of the package's dependency closure, all
    // prebuilt, which the nested provider loads (§10). Empty for a package
    // without dependencies: no provider is injected.
    std::string dependencyGraph;

    // Handed to a child with dependencies as EPM_EXECUTABLE, so a get_epm.cmake
    // of its own finds this epm.
    eacp::FilePath epmExecutable;
};

// The `cmake -S -B` arguments of PoC §5. Toolchain values the consumer left
// empty are not passed at all. With a dependencyGraph also EPM_NESTED,
// EPM_GRAPH, EPM_EXECUTABLE and epm-nested.cmake after epm-export.cmake in
// CMAKE_PROJECT_TOP_LEVEL_INCLUDES (Docs/Graph.md §10).
eacp::Vector<std::string> configureArguments(const ChildBuild& build);

eacp::Vector<std::string> buildArguments(const ChildBuild& build);

// epm-export.cmake as it was when this binary was built.
std::string_view embeddedExportScript();

// epm-nested.cmake: epm-provider.cmake as it was when this binary was built,
// plus a tail registering the provider over ${EPM_GRAPH}.
std::string_view embeddedNestedScript();

struct ChildBuildResult
{
    bool ok = false;
    std::string error;

    // The tail of the failing step's output; the whole of it is in a log
    // file in the build directory, which is kept.
    std::string details;
};

// Writes epm-export.cmake into the build directory (with dependencies also
// epm-nested.cmake and epm-graph.cmake), then configures and builds.
ChildBuildResult runChildBuild(const ChildBuild& build);
} // namespace epm
