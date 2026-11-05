#pragma once

#include <epm/Spec/Env.h>
#include <epm/Spec/Manifest.h>
#include <epm/Spec/Overrides.h>
#include <epm/Spec/Request.h>

#include <map>
#include <optional>
#include <string>

namespace epm
{
// What the root project calls itself as a declarer, in notes and
// provenance.
constexpr auto rootDeclarer = "root";

// One package entry of one project's epm.cpp: the root's, or a dependency's.
struct Declaration
{
    // The declaring package's name; rootDeclarer for the root project.
    std::string declarer;
    ManifestPackage package;

    // The root project's declaration, told apart by this rather than by
    // name: a dependency may be called "root" too.
    bool isRoot = false;
};

// Every source of a package's values, highest precedence first (§4).
struct MergeLayers
{
    PackageOverride packageOverride;
    Overrides overrides;
    std::optional<ManifestPackage> root;

    // The winning dependency declaration (first in the walk), if any.
    std::optional<Declaration> dependency;
};

// Which source set each value: "override", "override:package", "root",
// "manifest:<declarer>", "default", or why it was forced
// ("source dependency <name>").
// Keys: git, tag, mode, configs, abiMatch, buildMissingBinaries, source and
// options.<KEY>.
using Provenance = std::map<std::string, std::string>;

struct EffectiveRequest
{
    Request request;

    // The override checkout used instead of a fetch; empty for a fetch.
    std::string source;
    Provenance provenance;
};

struct MergeResult
{
    bool ok = false;
    EffectiveRequest effective;
    std::string error;
};

// The configs a configs value asks for: empty or ["auto"] (any case) by the
// PoC rule from `env` (multi-config: Debug, Release; else the build type as
// it is built, buildConfigFor, where an empty or unknown one means Release);
// otherwise each name as it is built, canonical. nullopt for a name that is
// not a configuration.
std::optional<eacp::Vector<std::string>>
    resolveConfigs(const eacp::Vector<std::string>& configs, const Env& env);

// True for an empty list or ["auto"]: the PoC's auto rule.
bool isAutoConfigs(const eacp::Vector<std::string>& configs);

// The effective request of `name`, each value from the first layer that sets
// it, options merged by key. The git url is expanded (expandGitUrl).
MergeResult
    mergeRequest(const std::string& name, const MergeLayers& layers, const Env& env);
} // namespace epm
