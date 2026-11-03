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

// The lock a child build resolves its own dependencies against. An empty
// lock leaves the child's CPM to fetch them, as before the lock existed.
struct LockForChildBuilds
{
    eacp::FilePath lock;
    std::string mode = "locked";
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
    eacp::FilePath cpmCache;
    eacp::FilePath cacheRoot;
    LockForChildBuilds childLock;
    std::string config;
};

// The `cmake -S -B` arguments of PoC §5. Toolchain values the consumer left
// empty are not passed at all.
eacp::Vector<std::string> configureArguments(const ChildBuild& build);

eacp::Vector<std::string> buildArguments(const ChildBuild& build);

// epm-export.cmake as it was when this binary was built.
std::string_view embeddedExportScript();

struct ChildBuildResult
{
    bool ok = false;
    std::string error;

    // The tail of the failing step's output; the whole of it is in a log
    // file in the build directory, which is kept.
    std::string details;
};

// Writes epm-export.cmake into the build directory, then configures and builds.
ChildBuildResult runChildBuild(const ChildBuild& build);
} // namespace epm
