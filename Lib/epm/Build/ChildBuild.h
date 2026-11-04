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

// The consumer's lock, which the child's own CPMAddPackage and FetchContent
// calls resolve against, and the root epm.cpp whose overrides apply there.
struct LockForChildBuilds
{
    eacp::FilePath lock;
    std::string mode = "locked";
    eacp::FilePath manifest;
};

// One configuration of a dependency, built straight into a binary cache prefix.
struct ChildBuild
{
    Request request;
    Env env;
    Generator generator;
    eacp::FilePath sourceDirectory;
    eacp::FilePath buildDirectory;
    eacp::FilePath prefix;
    std::string config;

    // graph.cmake of the package's dependency closure, all
    // prebuilt, which the nested provider loads. Empty for a package
    // without dependencies.
    std::string dependencyGraph;

    // EPM_EXECUTABLE in the child, whose epm.cmake runs it.
    eacp::FilePath epmExecutable;
    eacp::FilePath cacheRoot;
    LockForChildBuilds childLock;
};

// The child's `cmake -S -B` arguments. Toolchain values the consumer left
// empty are not passed at all. Every child also gets EPM_NESTED, the lock
// settings and epm.cmake after epm-export.cmake in
// CMAKE_PROJECT_TOP_LEVEL_INCLUDES, so its own package calls go through the
// lock; with a dependencyGraph also EPM_GRAPH.
EA::Vector<std::string> configureArguments(const ChildBuild& build);

EA::Vector<std::string> buildArguments(const ChildBuild& build);

// epm-export.cmake as it was when this binary was built.
std::string_view embeddedExportScript();

// epm.cmake and epm-provider.cmake as they were when this binary was built.
std::string_view embeddedModuleScript();
std::string_view embeddedProviderScript();

struct ChildBuildResult
{
    bool ok = false;
    std::string error;

    // The tail of the failing step's output; the whole of it is in a log
    // file in the build directory, which is kept.
    std::string details;
};

// Writes epm-export.cmake, epm.cmake and epm-provider.cmake into the build
// directory (with dependencies also epm-graph.cmake), then configures and
// builds.
ChildBuildResult runChildBuild(const ChildBuild& build);
} // namespace epm
