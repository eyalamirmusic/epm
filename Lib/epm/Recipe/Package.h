#pragma once

// Recipes include only Recipe.h, and throw what the helpers throw.
#include "BuildError.h" // IWYU pragma: export
#include "BuildProtocol.h"
#include "CMakeBuild.h"
#include "Prefix.h"

#include <epm/Cache/CacheRoot.h>
#include <epm/Cache/Fingerprint.h>
#include <epm/Cache/SourceCache.h>
#include <epm/Spec/Env.h>
#include <epm/Spec/Request.h>

#include <eacp/Core/Utils/Containers.h>

#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace epm
{
// One package's build step: how epm turns its source into what the consumer
// links. epm runs build() once per configure for each package it resolves,
// in dependency order, with the package's effective request, the consumer's
// env and the cache; build() ends by recording the result, exportPrefix() or
// useSource().
//
// The default build() is the CMake flow every package without steps of its
// own gets; CMakePackage's hooks are where the comments are. It fetches the
// source only when it builds (or for source mode), never to use an entry the
// cache or a cache store already holds:
//
//     auto source = resolveSource();           // commit and sourceId
//     auto build = cmake(source);              // configure(build)
//     auto key = fingerprint(source, build);
//     if (auto prefix = cached(key, [&](const Prefix& p)
//                              {
//                                  build.source(fetch().directory)
//                                      .into(p)
//                                      .run(); // afterBuild(p)
//                              }))
//         exportPrefix(*prefix);
//     else
//         useSource(build);
//
// Helpers that fail throw BuildError; the runner reports it as the error.
class Package
{
public:
    Package() = default;
    Package(const Package&) = delete;
    Package& operator=(const Package&) = delete;
    virtual ~Package() = default;

    virtual void build();

    // The effective, merged request (Docs/Graph.md §4).
    const Request& spec() const;
    const Env& env() const;
    CacheRoot cache() const;

    // The direct dependencies, resolved before this step.
    const eacp::Vector<PackageDependency>& dependencies() const;

    // graph.cmake of the dependency closure; empty without dependencies.
    const std::string& dependencyGraph() const;
    bool verbose() const;

    // The source's commit and sourceId, which is all fingerprint() needs,
    // without fetching it: as epm resolved it before the step, else resolved
    // here (resolveSource: `git ls-remote`, nothing for a full commit hash).
    // Its directory is the checkout when there is one already (a snapshot in
    // the cache, an EPM_<Name>_SOURCE override), else empty: fetch() first to
    // read the source.
    SourceSnapshot resolveSource();

    // The source checkout: the one epm handed the step, else the commit
    // resolveSource() names fetched into the source cache (fetchSource,
    // which a completed snapshot makes cheap). Inside cached()'s callback it
    // fetches under the package lock cached() holds. A step that calls it
    // outside the callback records the checkout as the package's SOURCE_DIR
    // when it ends with exportPrefix(); a prebuilt package's SOURCE_DIR is
    // otherwise empty (Docs/Graph.md §7), so the default steps, which fetch
    // only to build, give the same graph whether they built or not.
    SourceSnapshot fetch();

    // makeFingerprint of spec() for `source`.
    AbiFingerprint fingerprint(const SourceSnapshot& source) const;

    // The same with the options and definitions `build` passes in place of
    // spec().options, so a hook's option changes the entry.
    AbiFingerprint fingerprint(const SourceSnapshot& source,
                               const CMakeBuild& build) const;

    // The entry for `wanted`, picked under spec().abiMatch, as the mode
    // decides (decide()): nullopt for source (mode source, or auto without
    // buildMissingBinaries); the entry when it holds every config this
    // compiler can link (configUsableBy); a BuildError for binary-only
    // without one. Before deciding, configs the local pick lacks are taken
    // from the cache stores, in order (pullFromStores, CacheStore.h), so a
    // store's config counts as cached in every mode but source, binary-only
    // included. Otherwise, under the package lock, it picks again or
    // creates the entry and calls `buildMissing` once with a Prefix whose
    // configs() are those to build and whose stagedEntry() is a fresh private
    // directory to build them into. Once the callback has returned, each
    // config it left harvested (export file and .names in its unit) is
    // published into the entry (publishConfig), or into the exact entry for
    // `wanted` when it holds LTO objects and the entry was recorded for
    // another compiler; one it left without is an error. A callback that
    // throws publishes none, and the next run builds them again. The
    // callback runs under the package lock, which fetch() then leaves to it.
    std::optional<Prefix>
        cached(const AbiFingerprint& wanted,
               const std::function<void(const Prefix&)>& buildMissing);

    // A CMake build of `source` into `prefix` (CMakeBuild.h), starting from
    // spec().options and prefix.configs().
    CMakeBuild cmake(const SourceSnapshot& source, const Prefix& prefix) const;

    // The same before a prefix is known; CMakeBuild::into gives it one.
    CMakeBuild cmake(const SourceSnapshot& source) const;

    // Records the result: the package is prebuilt, from `prefix`'s entry,
    // with the options of the fingerprint the step asked for
    // (prefix.wanted()).
    void exportPrefix(const Prefix& prefix);

    // Records the result: the consumer builds the package from source, with
    // the request's options.
    void useSource();

    // The same with the options `build` sets (CMakeBuild::definedValues), a
    // hook's included, which the consumer's add_subdirectory gets.
    void useSource(const CMakeBuild& build);

    // A progress line on stdout, as epm's own ("-- epm: <name>: <line>").
    void log(std::string_view line) const;

protected:
    // The default build(), with CMakePackage's hooks.
    void buildWithCMake(const std::function<void(CMakeBuild&)>& configure,
                        const std::function<void(const Prefix&)>& afterBuild);

private:
    friend struct PackageRunner;
    friend BuildOutput runPackage(Package& package, const BuildInput& input);

    // The entry recorded for `wanted` itself, created when missing.
    BinaryEntry exactEntry(const AbiFingerprint& wanted) const;

    // Builds `configs` through `buildMissing` into a staged entry, publishes
    // them (into `entry`, or the exact entry for LTO objects of a compiler
    // `entry` was not recorded for) and returns the entry they went into.
    BinaryEntry
        buildAndPublish(const BinaryEntry& entry,
                        const AbiFingerprint& wanted,
                        const eacp::Vector<std::string>& configs,
                        const std::function<void(const Prefix&)>& buildMissing);

    // fetch() without recording the checkout as the SOURCE_DIR.
    SourceSnapshot fetchSnapshot();

    BuildInput input;
    std::optional<SourceSnapshot> resolved;
    std::optional<SourceSnapshot> fetched;
    BuildOutput output;

    // Set while cached() holds the package lock.
    bool holdsLock = false;

    // fetch() was called outside cached()'s callback: exportPrefix records
    // the checkout.
    bool sourceRequested = false;
};

// Runs `package`'s build() with `input` as its context, and returns what it
// recorded. A BuildError or any other exception, or a build() that records
// nothing, comes back as the output's error.
BuildOutput runPackage(Package& package, const BuildInput& input);

// Gives `package` the context of a step without running it, which is what a
// Recipe's describe() sees (env()).
struct PackageRunner
{
    static void setInput(Package& package, const BuildInput& input);
};
} // namespace epm
