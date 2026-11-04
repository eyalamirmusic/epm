#pragma once

#include <epm/Build/ChildBuild.h>
#include <epm/Cache/CacheRoot.h>
#include <epm/Source/SourceSnapshot.h>
#include <epm/Emit/Response.h>
#include <epm/Spec/Env.h>
#include <epm/Spec/Request.h>

#include <optional>
#include <string>

namespace epm
{
enum class Decision
{
    useSource,
    useBinary,
    buildBinary,
    missingBinary
};

// MODE source: always source. A cached binary is used whenever one exists.
// Otherwise prebuilt builds it, binary-only fails, and auto builds it only
// when buildMissingBinaries allows, else falls back to source.
Decision decide(const Request& request, bool binaryExists);

// Why the request cannot be resolved, or nullopt when it is well formed.
std::optional<std::string> requestError(const Request& request);

struct ResolveResult
{
    bool ok = false;
    Response response;

    // Prebuilt only: fingerprintId of the entry used, which dependents record
    // in their deps.
    std::string fingerprintHash;
    std::string error;

    // Child build output worth showing above the error line.
    std::string details;
};

// Finds, builds or skips the binary entry for a source already in the cache,
// as the mode decides. The request is assumed well formed (requestError). An
// entry's configs built against dependencies the lock no longer holds at the
// same hashes count as missing. `childLock` and `dependencyGraph` go to the
// child build.
ResolveResult resolveSnapshot(const Request& request,
                              const Env& env,
                              const CacheRoot& cache,
                              const SourceSnapshot& snapshot,
                              const LockForChildBuilds& childLock,
                              const std::string& dependencyGraph = {});
} // namespace epm
