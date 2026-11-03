#pragma once

#include "Merge.h"
#include "ResolvedGraph.h"

#include <epm/Cache/CacheRoot.h>
#include <epm/Cache/SourceCache.h>
#include <epm/Spec/Env.h>
#include <epm/Spec/Manifest.h>
#include <epm/Spec/Overrides.h>

#include <functional>
#include <string>
#include <string_view>

namespace epm
{
// Fetches one package's source; fetchSource into the cache in the binary.
using SourceFetcher = std::function<SourceResult(
    const std::string& name, const std::string& url, const std::string& ref)>;

struct GraphInputs
{
    Manifest manifest;

    // Where path-valued profiles are looked up.
    eacp::FilePath manifestDirectory;
    Env env;

    // As read from overrides.json; the environment fills what it leaves
    // unset (withEnvironment). No environment unless one is given.
    Overrides overrides;
    EnvironmentLookup environment = [](std::string_view)
    { return std::optional<std::string> {}; };
};

struct PlannedPackage
{
    EffectiveRequest effective;
    SourceSnapshot snapshot;

    // Direct dependencies: the packages its own epm.json declares.
    eacp::Vector<std::string> deps;
    eacp::Vector<std::string> declaredBy;
};

struct GraphPlan
{
    bool ok = false;

    // In dependency order.
    eacp::Vector<PlannedPackage> packages;
    eacp::Vector<std::string> notes;
    std::string error;
};

// One spelling per repository, to tell when two names fetch the same one:
// scheme, user, a trailing slash and ".git" dropped, scp-like "host:path" as
// "host/path", lower case (local paths keep their case).
std::string repositoryKey(std::string_view url);

// Rules 1-6 of Docs/Graph.md §5: walks the root manifest depth first in
// manifest order, fetching every package (or taking its EPM_<Name>_SOURCE)
// and reading its epm.json, merges each package's effective request (§4),
// forces the dependents of source packages into source, and collects the
// notes (strict makes them an error). Fails on a root manifest that
// manifestError rejects. Nothing is built.
GraphPlan planGraph(const GraphInputs& inputs, const SourceFetcher& fetch);

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

// Resolves the planned packages in order through resolveSnapshot, each
// prebuilt one's deps holding its dependencies' fingerprint hashes and its
// child build given graph.cmake of their dependencyClosure. A package whose
// dependency ended up in source goes to source too.
GraphResult
    resolvePlan(const GraphPlan& plan, const Env& env, const CacheRoot& cache);

// `epm configure`: planGraph fetching into `cache`, then resolvePlan.
GraphResult configureGraph(const GraphInputs& inputs, const CacheRoot& cache);
} // namespace epm
