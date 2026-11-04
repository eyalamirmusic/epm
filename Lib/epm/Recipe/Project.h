#pragma once

#include <epm/Spec/Manifest.h>

#include <eacp/Core/Utils/Containers.h>

#include <Miro/Reflect.h>

#include <map>
#include <optional>
#include <string>

namespace epm
{
// Go's `replace` for one package: it replaces every requirement on the
// package, whoever declares it; empty fields keep what the requirements say.
// Only the root project's overrides count.
struct ManifestOverride
{
    std::string url;
    std::string commit;
    std::string tag;

    MIRO_REFLECT(url, commit, tag)
};

// What an epm.cpp describes: the packages its project depends on, in the
// order that decides among them.
struct Project
{
    eacp::Vector<ManifestPackage> packages;

    // Settles, by name, what minimal version selection cannot.
    std::map<std::string, ManifestOverride> overrides;

    // Appends a dependency. The reference is valid until the next add. The
    // root project may leave git and tag empty to only add options (or
    // configs, mode, abiMatch) to a package a dependency declares.
    ManifestPackage&
        add(std::string name, std::string git = {}, std::string tag = {});

    // Appends a package find_package finds with CMake's own search, never
    // fetched: the only way a find_package of a name outside the graph
    // reaches that search.
    ManifestPackage& addSystem(std::string name);

    // The override of `name`, created empty on first use.
    ManifestOverride& overridePackage(const std::string& name);

    std::optional<ManifestOverride> findOverride(const std::string& name) const;

    MIRO_REFLECT(packages, overrides)
};

// Each sets one field of `package` and returns it, so calls chain:
// setMode(setOption(project.add("fmt", "fmtlib/fmt", "11.0.2"), "FMT_DOC",
// "OFF"), "source").
ManifestPackage&
    setOption(ManifestPackage& package, std::string key, std::string value);
ManifestPackage& addConfig(ManifestPackage& package, std::string config);
ManifestPackage& setMode(ManifestPackage& package, std::string mode);
ManifestPackage& setAbiMatch(ManifestPackage& package, std::string abiMatch);

// The first problem of a described project: a package without a name, or a
// name added twice. nullopt when there is none.
std::optional<std::string> projectError(const Project& project);
} // namespace epm
