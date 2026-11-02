#pragma once

#include <epm/Cache/CacheRoot.h>
#include <epm/Spec/Env.h>
#include <epm/Spec/Request.h>

#include <string>

namespace epm
{
// One configuration of a dependency, built straight into a binary cache prefix.
struct ChildBuild
{
    Request request;
    Env env;
    eacp::FilePath sourceDirectory;
    eacp::FilePath buildDirectory;
    eacp::FilePath prefix;
    std::string config;
};

struct ChildBuildResult
{
    bool ok = false;
    std::string error;
};

// The consumer's generator when single-config, else Ninja if on PATH, else the
// platform default.
std::string chooseGenerator(const Env& env);

eacp::Vector<std::string> configureArguments(const ChildBuild& build,
                                             const CacheRoot& cache);

ChildBuildResult runChildBuild(const ChildBuild& build, const CacheRoot& cache);
} // namespace epm
