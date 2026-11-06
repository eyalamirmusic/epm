#pragma once

#include "ManifestLoader.h"
#include "Merge.h"
#include "ResolvedGraph.h"

#include <epm/Cache/CacheRoot.h>
#include <epm/Cache/SourceCache.h>
#include <epm/Recipe/Project.h>
#include <epm/Spec/Env.h>
#include <epm/Spec/Overrides.h>

#include <functional>
#include <string>
#include <string_view>

namespace epm
{
// Resolves one package's source and fetches what the walk reads of it, its
// epm.cpp (SourceResult::manifestDirectory, else the snapshot's directory);
// fetchManifest into the cache in the binary.
using SourceFetcher = std::function<SourceResult(
    const std::string& name, const std::string& url, const std::string& ref)>;

struct GraphInputs
{
    // What the root epm.cpp described for `env`.
    Project project;

    // The root epm.cpp itself, whose executable runs the build steps the
    // root owns (StepOwner); empty without one (they run in this process,
    // buildInProcess).
    eacp::FilePath rootRecipe;

    // Also what every dependency's epm.cpp is described for.
    Env env;

    // As read from overrides.json; the environment fills what it leaves
    // unset (withEnvironment). No environment unless one is given.
    Overrides overrides;
    EnvironmentLookup environment = [](std::string_view)
    { return std::optional<std::string> {}; };
};

// The recipe whose executable runs a package's build step (Docs/Graph.md §6):
// the package's own epm.cpp when it has one; else that of the first project,
// in merge order (the root, then the dependencies in walk order), whose
// declaration has custom steps (add<T>); else that of the project whose
// declaration won the merge.
struct StepOwner
{
    // As BuildInput::recipeName: the package the recipe belongs to, the
    // package itself for its own epm.cpp; empty for the root project.
    std::string name;

    // The epm.cpp; empty when the owner has none (a root without a recipe
    // file), and the default steps run in epm's process.
    eacp::FilePath recipe;

    // The owning declaration came with steps of its own (Recipe::add<T>).
    bool customBuild = false;
};

struct PlannedPackage
{
    EffectiveRequest effective;

    // Resolved: its directory is set only when the checkout is there already
    // (an override, a snapshot in the cache); the step fetches it otherwise.
    SourceSnapshot snapshot;

    // Direct dependencies: the packages its own epm.cpp declares.
    eacp::Vector<std::string> deps;
    eacp::Vector<std::string> declaredBy;
    StepOwner owner;

    // Prebuilt, its source snapshot is still fetched for the consumer's
    // <name>_SOURCE_DIR (fetchesSourceOf: EPM_FETCH_SOURCES and
    // EPM_<Name>_FETCH_SOURCE).
    bool fetchSource = true;
};

struct GraphPlan
{
    bool ok = false;

    // In dependency order.
    eacp::Vector<PlannedPackage> packages;
    eacp::Vector<std::string> notes;
    std::string error;
};

// The rules of Docs/Graph.md §5: walks the root project depth first in
// declaration order, resolving every package and fetching its epm.cpp (or
// taking its EPM_<Name>_SOURCE) and loading it through `loader`, merges each
// package's effective request (§4), picks the owner of its build step
// (StepOwner, the step-ownership rule), forces the dependents of source
// packages into source, and collects the notes (strict makes them an
// error). Fails on a project that projectError
// rejects, on a recipe the loader cannot load, naming the package, and on
// custom steps (add<T>) declared for a package that has its own epm.cpp.
// Nothing is built.
GraphPlan planGraph(const GraphInputs& inputs,
                    const SourceFetcher& fetch,
                    ManifestLoader& loader);

// The packages of `graph` that `deps` reach, directly or through their own
// deps, in the graph's order and without notes: what a nested child build
// of a package with those deps loads (Docs/Graph.md §10).
ResolvedGraph dependencyClosure(const ResolvedGraph& graph,
                                const eacp::Vector<std::string>& deps);

struct GraphResult
{
    bool ok = false;
    ResolvedGraph graph;
    std::string error;

    // Child build output worth showing above the error line.
    std::string details;
};

// Resolves the planned packages in order, each through the build step of its
// owner's recipe executable (`steps.build`), its deps holding its
// dependencies' fingerprint hashes, and the step given their resolved
// entries (PackageDependency) and graph.cmake of their dependencyClosure. A
// package whose dependency ended up in source goes to source too. A source
// package's step runs in the executable as well, for the options its hooks
// set (BuildOutput::options, graph.cmake's OPTIONS); it must record source.
// A package whose owner has no recipe gets the default steps in this
// process. Then the snapshot of each prebuilt package is fetched, by the
// commit its step resolved, for its SOURCE_DIR: of every one the consumer
// wants it for (PlannedPackage::fetchSource), and always of those a package
// in source mode depends on, directly or not. That fetch comes after every
// step, so it changes no fingerprint and no step's input.
GraphResult resolvePlan(const GraphPlan& plan,
                        const Env& env,
                        const CacheRoot& cache,
                        ManifestLoader& steps);

// `epm configure`: planGraph fetching into `cache`, then resolvePlan.
GraphResult configureGraph(const GraphInputs& inputs,
                           const CacheRoot& cache,
                           ManifestLoader& loader);
} // namespace epm
