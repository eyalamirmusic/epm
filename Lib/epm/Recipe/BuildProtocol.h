#pragma once

#include <epm/Cache/SourceCache.h>
#include <epm/Spec/Env.h>
#include <epm/Spec/Request.h>

#include <eacp/Core/Utils/Containers.h>

#include <Miro/Reflect.h>

#include <map>
#include <optional>
#include <string>

// What epm and a package's build step exchange: the step's input and what it
// recorded, as JSON files when the step runs in a recipe executable
// (`epm-recipe build --package <name> --in <input> --out <output>`), or as
// these structs when epm runs the default steps in its own process
// (Package.h, buildInProcess).
namespace epm
{
constexpr auto buildProtocolSchema = 1;

// The source handed to the step: a resolved commit with its sourceId and,
// once fetched, the snapshot's directory; or an override checkout
// (EPM_<Name>_SOURCE), which has a directory but no commit and no sourceId.
struct BuildSource
{
    std::string commit;
    std::string sourceId;
    std::string directory;

    MIRO_REFLECT(commit, sourceId, directory)
};

BuildSource buildSourceFor(const SourceSnapshot& snapshot);
SourceSnapshot snapshotOf(const BuildSource& source);

// One direct dependency of the package, as epm resolved it before the step.
struct PackageDependency
{
    std::string name;

    // "prebuilt" or "source".
    std::string mode;

    // Prebuilt only: the fingerprint hash of its entry (full hex), the entry
    // prefix, its epm-targets.cmake and the configs it holds.
    std::string fingerprint;
    std::string prefix;
    std::string targetsFile;
    eacp::Vector<std::string> configs;

    MIRO_REFLECT(name, mode, fingerprint, prefix, targetsFile, configs)
};

// A package of the dependency closure whose SOURCE_DIR dependencyGraph leaves
// empty (prebuilt, its source not fetched): what the step fetches, by the
// commit epm resolved, once a child build of its own loads the closure, so
// that the child's <name>_SOURCE_DIR names the snapshot.
struct DependencySource
{
    std::string name;
    std::string gitUrl;

    // The tag or branch the commit was resolved from: what a server that
    // will not serve a commit by its hash is asked for instead.
    std::string gitRef;
    std::string commit;

    MIRO_REFLECT(name, gitUrl, gitRef, commit)
};

struct BuildInput
{
    int schema = buildProtocolSchema;

    // The package whose epm.cpp the recipe executable is, as the graph names
    // it; empty for the root project. `build --package` with this name runs
    // the Recipe's own build().
    std::string recipeName;

    Env env;

    // The cache root, resolved (CacheRoot).
    std::string cacheRoot;

    // The cache stores a missing binary is taken from before it is built
    // (CacheRoot::stores, CacheStore.h), in order, as the configure was given
    // them; here so that a step run again by hand sees them too.
    eacp::Vector<std::string> cacheStores;

    // The package's effective request: its name, git url and ref, mode,
    // options, configs (concrete), abiMatch, buildMissingBinaries and deps
    // (each direct dependency's fingerprint hash).
    Request request;

    // The source as epm resolved it: its commit and sourceId, and the
    // checkout when there is one already (a snapshot in the cache, an
    // override). Without a directory Package::fetch() fetches that commit;
    // without a source at all, the request's ref.
    std::optional<BuildSource> source;

    eacp::Vector<PackageDependency> dependencies;

    // graph.cmake of the package's dependency closure (Docs/Graph.md §10),
    // which a child build of a package with dependencies loads; empty
    // without dependencies.
    std::string dependencyGraph;

    // The packages of dependencyGraph without a SOURCE_DIR, fetched only by a
    // child build (CMakeBuild::configure), which gets their snapshots as
    // SOURCE_DIR.
    eacp::Vector<DependencySource> dependencySources;

    // The epm binary running the graph: what a child build with dependencies
    // gets as EPM_EXECUTABLE.
    std::string epmExecutable;

    // Log::setVerbose for the step.
    bool verbose = false;

    // The package's owning declaration came with steps of its own
    // (Recipe::add<T>): a recipe without a factory for it fails rather than
    // running the default steps in their place.
    bool customBuild = false;

    MIRO_REFLECT(schema,
                 recipeName,
                 env,
                 cacheRoot,
                 cacheStores,
                 request,
                 source,
                 dependencies,
                 dependencyGraph,
                 dependencySources,
                 epmExecutable,
                 verbose,
                 customBuild)
};

// What the step recorded; the fields of Response (Emit/Response.h) plus the
// fingerprint hash and the error of ResolveResult (Resolve/Resolve.h).
struct BuildOutput
{
    int schema = buildProtocolSchema;
    std::string name;

    // "prebuilt" or "source"; empty when the step failed.
    std::string mode;
    std::string sourceDirectory;

    // Prebuilt only: the entry, its platform, the configs it holds that the
    // consumer can link (configUsableBy; the facade loads only those), how
    // its toolchain differs from the current one (abiNote), and its
    // fingerprint hash, which dependents record in their deps.
    std::string prefix;
    std::string platform;
    eacp::Vector<std::string> configs;
    std::string note;
    std::string fingerprintHash;

    // Prebuilt only: abiKey of the entry's fingerprint under compatible and
    // any, which dependents record in their compatibleDeps and anyDeps.
    std::string compatibleKey;
    std::string anyKey;

    // The options the package is built with, as the step's CMakeBuild set
    // them (CMakeBuild::definedValues, a hook's included): what graph.cmake
    // hands a source package's add_subdirectory. Empty when the step made no
    // CMakeBuild it recorded, and the request's options stand.
    std::map<std::string, std::string> options;

    // One line when the step failed, and output worth showing above it.
    std::string error;
    std::string details;

    bool ok() const { return error.empty() && !mode.empty(); }

    MIRO_REFLECT(schema,
                 name,
                 mode,
                 sourceDirectory,
                 prefix,
                 platform,
                 configs,
                 note,
                 fingerprintHash,
                 compatibleKey,
                 anyKey,
                 options,
                 error,
                 details)
};

// The schema of the build input or output in `path`, read on its own so that
// a file of another schema is reported as such rather than half read;
// nullopt when the file holds no JSON object.
std::optional<int> buildProtocolSchemaOf(const eacp::FilePath& path);
} // namespace epm
