#pragma once

#include <epm/Build/ChildBuild.h>
#include <epm/Cache/CacheRoot.h>
#include <epm/Cache/SourceCache.h>
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
    buildBinary
};

// MODE source: always source. A cached binary is used whenever one exists.
// Otherwise prebuilt builds it, and auto builds it only when
// buildMissingBinaries allows, else falls back to source.
Decision decide(const Request& request, bool binaryExists);

// Why the request cannot be resolved, or nullopt when it is well formed.
std::optional<std::string> requestError(const Request& request);

struct ResolveResult
{
    bool ok = false;
    Response response;
    std::string error;

    // Child build output worth showing above the error line.
    std::string details;
};

// The binary half of resolving a package whose sources are already in the
// cache: find, build or skip the binary entry as the mode decides.
ResolveResult resolveBinary(const Request& request,
                            const Env& env,
                            const CacheRoot& cache,
                            const SourceSnapshot& snapshot,
                            const LockForChildBuilds& childLock);
} // namespace epm
