#include "CMakeBuild.h"
#include "BuildError.h"

#include <epm/Build/Harvest.h>
#include <epm/Util/Log.h>
#include <epm/Util/Text.h>
#include <epm/Util/Timestamp.h>

#include <eacp/Core/Utils/Console.h>
#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/StdPath.h>

#include <filesystem>

namespace epm
{
using namespace eacp;

CMakeBuild::CMakeBuild(ChildBuild baseToUse, FilePath buildRootsToUse)
    : base(std::move(baseToUse))
    , buildRoots(std::move(buildRootsToUse))
{
}

CMakeBuild& CMakeBuild::option(std::string key, std::string value)
{
    base.request.options[std::move(key)] = std::move(value);
    return *this;
}

CMakeBuild& CMakeBuild::define(std::string name, std::string value)
{
    base.definitions[std::move(name)] = std::move(value);
    return *this;
}

CMakeBuild& CMakeBuild::configs(Vector<std::string> configsToBuild)
{
    chosenConfigs = std::move(configsToBuild);
    return *this;
}

CMakeBuild& CMakeBuild::generator(std::string name, std::string makeProgram)
{
    base.generator = {std::move(name), std::move(makeProgram)};
    return *this;
}

CMakeBuild& CMakeBuild::into(const Prefix& prefixToUse)
{
    prefix = prefixToUse;
    return *this;
}

CMakeBuild& CMakeBuild::source(FilePath directory)
{
    base.sourceDirectory = std::move(directory);
    return *this;
}

std::map<std::string, std::string> CMakeBuild::definedValues() const
{
    auto values = base.request.options;

    // -DNAME:BOOL=ON sets NAME, as an option of that name would.
    for (const auto& [name, value]: base.definitions)
        values[name.substr(0, name.find(':'))] = value;

    return values;
}

const Vector<std::string>& CMakeBuild::configs() const
{
    static const auto none = Vector<std::string> {};

    if (chosenConfigs)
        return *chosenConfigs;

    return prefix ? prefix->configs() : none;
}

const Generator& CMakeBuild::generator() const
{
    return base.generator;
}

const Prefix& CMakeBuild::requirePrefix() const
{
    if (!prefix)
        throw BuildError {"the CMake build of " + base.request.name
                          + " has no prefix: make it with cmake(source, prefix) "
                            "or give it one with into(prefix)"};

    return *prefix;
}

FilePath CMakeBuild::buildRoot() const
{
    const auto& prefixToUse = requirePrefix();
    const auto& entry = prefixToUse.entry();
    const auto root = buildRoots / base.request.name / entry.fingerprint.platform
                      / entry.prefix.fileName();

    // One per staging directory, so builds of the same config in two
    // processes never share a tree.
    if (prefixToUse.stagedEntry() == prefixToUse.path())
        return root;

    return root / prefixToUse.stagedEntry().fileName();
}

FilePath CMakeBuild::buildDirectory(const std::string& config) const
{
    return buildRoot() / config;
}

ChildBuild CMakeBuild::childBuild(const std::string& config) const
{
    auto build = base;
    build.buildDirectory = buildDirectory(config);
    build.prefix = requirePrefix().configDirectory(config);
    build.config = config;
    return build;
}

Vector<std::string> CMakeBuild::configureArguments(const std::string& config) const
{
    return epm::configureArguments(childBuild(config));
}

void CMakeBuild::addDependencySources()
{
    if (addedDependencySources || base.dependencyGraph.empty())
        return;

    base.dependencyGraph += dependencySources();
    addedDependencySources = true;
}

void CMakeBuild::configure(const std::string& config)
{
    addDependencySources();
    const auto build = childBuild(config);

    if (!Files::removeAll(build.buildDirectory))
        throw BuildError {"cannot clear " + build.buildDirectory.str()};

    const auto configured = configureChildBuild(build);

    if (!configured.ok)
        throw BuildError {configured.error, configured.details};
}

void CMakeBuild::build(const std::string& config)
{
    const auto built = buildChildBuild(childBuild(config));

    if (!built.ok)
        throw BuildError {built.error, built.details};
}

void CMakeBuild::harvest(const std::string& config)
{
    const auto& name = base.request.name;
    const auto& staged = requirePrefix().stagedEntry();
    // buildRoots is the cache's build/, so its parent is the cache root. The
    // source root is the snapshot's, whichever subdirectory of it -S named.
    const auto cacheRoot = buildRoots.parentDirectory();
    const auto roots = HarvestRoots {
        cacheRoot, sourceRootOf(cacheRoot, base.sourceDirectory), base.cpmCache};
    const auto harvested =
        harvestConfig(buildDirectory(config), staged, config, roots);

    if (!harvested.ok)
        throw BuildError {"harvesting " + name + " (" + config
                          + "): " + harvested.error};

    if (!harvested.warning.empty())
    {
        Console::printError("epm: warning: " + harvested.warning);
        keptBuildTree = true;
    }
}

void CMakeBuild::run()
{
    const auto& name = base.request.name;
    const auto& entry = requirePrefix().entry();

    Log::step("epm: building " + name + " (" + joinList(configs(), ", ") + ") into "
              + entry.prefix.str() + " with generator '" + base.generator.name
              + "'");

    // Trees that failed builds of this entry kept, once they are stale.
    removeStaleStaging(buildRoot().parentDirectory());

    const auto& prefix = requirePrefix();
    const auto staged = prefix.stagedEntry() != prefix.path();

    for (const auto& config: configs())
    {
        // So that another host sharing the cache sees the build is alive.
        if (staged)
        {
            touchStagingHeartbeat(prefix.stagedEntry());
            touchStagingHeartbeat(buildRoot());
        }

        Log::progress("epm: building " + name + " from source into the cache ("
                      + config + ", " + entry.fingerprint.platform
                      + "), this can take a while");

        const auto stopwatch = Stopwatch {};
        configure(config);
        build(config);

        Log::progress("epm: built " + name + " (" + config + ") in "
                      + formatDuration(stopwatch.elapsedSeconds()));

        harvest(config);
    }

    if (keptBuildTree)
        return;

    Files::removeAll(buildRoot());

    // The entry's directory of trees, unless another build is using it: a
    // plain remove fails on a directory that is not empty.
    if (requirePrefix().stagedEntry() != requirePrefix().path())
    {
        auto notEmpty = std::error_code {};
        std::filesystem::remove(toStdPath(buildRoot().parentDirectory()), notEmpty);
    }
}
} // namespace epm
