#pragma once

#include "Prefix.h"

#include <epm/Build/ChildBuild.h>

#include <eacp/Core/Utils/Containers.h>
#include <eacp/Core/Utils/FilePath.h>

#include <map>
#include <optional>
#include <string>

namespace epm
{
// A CMake build of a package's source into a binary cache prefix, one child
// build per config (Docs/PoC.md §5), from Package::cmake(), inside the
// callback of Package::cached():
//
//     cmake(source, prefix).option("FMT_DOC", "OFF").run();
//
// It starts with the package's options (Package::spec().options) and the
// prefix's configs. Each step throws BuildError when it fails.
class CMakeBuild
{
public:
    // `base` carries everything of a ChildBuild but the build directory, the
    // prefix and the config; `buildRootsToUse` is the cache's build/.
    CMakeBuild(ChildBuild baseToUse, eacp::FilePath buildRootsToUse);

    // Sets a package option, -D<key>=<value>, over the request's value.
    CMakeBuild& option(std::string key, std::string value);

    // A raw -D<name>=<value>, after epm's own definitions and the options, so
    // it may override one of them: define("CMAKE_CXX_STANDARD", "17"),
    // define("CMAKE_POSITION_INDEPENDENT_CODE:BOOL", "ON").
    CMakeBuild& define(std::string name, std::string value);

    // The configs run() builds; by default the prefix's (Prefix::configs()).
    CMakeBuild& configs(eacp::Vector<std::string> configsToBuild);

    // Overrides the generator chooseGenerator picked: the consumer's when it
    // is single-config, else Ninja, else Unix Makefiles.
    CMakeBuild& generator(std::string name, std::string makeProgram = {});

    // The prefix to build into, for a CMakeBuild made before cached() chose
    // one (Package::cmake(source)).
    CMakeBuild& into(const Prefix& prefixToUse);

    // The options and the definitions, as the -D flags set them, a
    // definition by its name without the :TYPE (a definition wins over an
    // option of the same name): what Package::fingerprint records for this
    // build, and what useSource(build) hands a source build.
    std::map<std::string, std::string> definedValues() const;

    const eacp::Vector<std::string>& configs() const;
    const Generator& generator() const;

    // <cache>/build/<name>/<platform>/<fp12>/<config>.
    eacp::FilePath buildDirectory(const std::string& config) const;

    // The child build of one config, as configure() and build() run it.
    ChildBuild childBuild(const std::string& config) const;

    eacp::Vector<std::string> configureArguments(const std::string& config) const;

    // A fresh build directory, the injected scripts, `cmake -S -B`.
    void configure(const std::string& config);

    // `cmake --build`.
    void build(const std::string& config);

    // Relocates the build's output into the prefix (harvestConfig): its
    // export file and .names. The config is not in the entry yet:
    // Package::cached() checks it in once its callback has returned, so a
    // step that fails after the harvest leaves nothing that passes for built.
    void harvest(const std::string& config);

    // configure, build and harvest each config in turn, then deletes the
    // build trees unless a harvest had to keep one.
    void run();

private:
    const Prefix& requirePrefix() const;
    eacp::FilePath buildRoot() const;

    ChildBuild base;
    eacp::FilePath buildRoots;
    std::optional<Prefix> prefix;
    std::optional<eacp::Vector<std::string>> chosenConfigs;
    bool keptBuildTree = false;
};
} // namespace epm
