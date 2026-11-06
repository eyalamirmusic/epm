#pragma once

#include <epm/Cache/CacheRoot.h>
#include <epm/Cache/SourceCache.h>
#include <epm/Emit/Response.h>
#include <epm/Recipe/BuildProtocol.h>
#include <epm/Spec/Env.h>
#include <epm/Spec/Request.h>

#include <map>
#include <string>

namespace epm
{
struct ResolveResult
{
    bool ok = false;
    Response response;

    // Prebuilt only: fingerprintId of the entry used, which dependents record
    // in their deps.
    std::string fingerprintHash;

    // Prebuilt only: BuildOutput's compatibleKey and anyKey, which
    // dependents record in their compatibleDeps and anyDeps.
    std::string compatibleKey;
    std::string anyKey;

    // BuildOutput::options: what the step built or configured the package
    // with; empty when it recorded none.
    std::map<std::string, std::string> options;
    std::string error;

    // Child build output worth showing above the error line.
    std::string details;
};

// What a build step recorded, as `epm resolve` and the graph report it.
ResolveResult resolveResultFor(const BuildOutput& output);

// The build step's input for a resolved source (its directory empty until
// fetched), with `cache`'s stores and this process as the epm a child build
// calls back (EPM_EXECUTABLE).
BuildInput buildInputFor(const Request& request,
                         const Env& env,
                         const CacheRoot& cache,
                         const SourceSnapshot& snapshot,
                         const std::string& dependencyGraph = {},
                         const eacp::Vector<PackageDependency>& dependencies = {});

// Finds, builds or skips the binary entry for a resolved source, as
// the mode decides: the default build steps (CMakePackage) run in this
// process, never in a recipe executable, since `epm resolve` serves
// epm_add_package, which has no recipe and so no custom steps. The request is
// assumed well formed (requestError).
// `dependencyGraph` goes to the child build (ChildBuild::dependencyGraph).
ResolveResult
    resolveSnapshot(const Request& request,
                    const Env& env,
                    const CacheRoot& cache,
                    const SourceSnapshot& snapshot,
                    const std::string& dependencyGraph = {},
                    const eacp::Vector<PackageDependency>& dependencies = {});

// The whole `epm resolve` pipeline: resolve the source (resolveSource), then
// resolveSnapshot, whose step fetches it only when it needs it; then, for a
// prebuilt result, the snapshot for the consumer's <name>_SOURCE_DIR unless
// `fetchSourceDir` is false (EPM_FETCH_SOURCES, EPM_<Name>_FETCH_SOURCE),
// which changes no fingerprint.
ResolveResult resolvePackage(const Request& request,
                             const Env& env,
                             const CacheRoot& cache,
                             bool fetchSourceDir = true);
} // namespace epm
