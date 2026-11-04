#include "CMakeBuild.h"
#include "BuildError.h"

#include <epm/Build/Harvest.h>
#include <epm/Util/Log.h>
#include <epm/Util/Text.h>
#include <epm/Util/Timestamp.h>

#include <eacp/Core/Utils/Console.h>
#include <eacp/Core/Utils/Files.h>

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
    const auto& entry = requirePrefix().entry();
    return buildRoots / base.request.name / entry.fingerprint.platform
           / entry.prefix.fileName();
}

FilePath CMakeBuild::buildDirectory(const std::string& config) const
{
    return buildRoot() / config;
}

ChildBuild CMakeBuild::childBuild(const std::string& config) const
{
    auto build = base;
    build.buildDirectory = buildDirectory(config);
    build.prefix = requirePrefix().path();
    build.config = config;
    return build;
}

Vector<std::string> CMakeBuild::configureArguments(const std::string& config) const
{
    return epm::configureArguments(childBuild(config));
}

void CMakeBuild::configure(const std::string& config)
{
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
    const auto& prefixPath = requirePrefix().path();
    const auto harvested = harvestConfig(buildDirectory(config), prefixPath, config);

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

    for (const auto& config: configs())
    {
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

    if (!keptBuildTree)
        Files::removeAll(buildRoot());
}
} // namespace epm
