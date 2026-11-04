#pragma once

#include <epm/Spec/Manifest.h>

#include <eacp/Core/Utils/Containers.h>

#include <Miro/Reflect.h>

#include <optional>
#include <string>

namespace epm
{
// What an epm.cpp describes: the packages its project depends on, in the
// order that decides among them (Docs/Graph.md §5 rule 3).
struct Project
{
    eacp::Vector<ManifestPackage> packages;

    // Appends a dependency. The reference is valid until the next add. The
    // root project may leave git and tag empty to only add options (or
    // configs, mode, abiMatch) to a package a dependency declares.
    ManifestPackage&
        add(std::string name, std::string git = {}, std::string tag = {});

    MIRO_REFLECT(packages)
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
