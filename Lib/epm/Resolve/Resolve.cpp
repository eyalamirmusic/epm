#include "Resolve.h"

#include <epm/Cache/BinaryCache.h>
#include <epm/Cache/SourceCache.h>
#include <epm/Util/Log.h>

namespace epm
{
namespace
{
ResolveResult failure(std::string error, std::string details = {})
{
    auto result = ResolveResult {};
    result.error = std::move(error);
    result.details = std::move(details);
    return result;
}

ResolveResult success(Response response)
{
    auto result = ResolveResult {};
    result.ok = true;
    result.response = std::move(response);
    return result;
}

Response sourceResponse(const Request& request, const SourceSnapshot& snapshot)
{
    return {request.name, "source", snapshot.directory, {}};
}

Response binaryResponse(const Request& request,
                        const SourceSnapshot& snapshot,
                        const BinaryEntry& entry)
{
    return {request.name, "prebuilt", snapshot.directory, entry.prefix};
}
} // namespace

Decision decide(const Request& request, bool binaryExists)
{
    if (request.mode == "source")
        return Decision::useSource;

    if (binaryExists)
        return Decision::useBinary;

    if (request.mode == "prebuilt" || request.buildMissingBinaries)
        return Decision::buildBinary;

    return Decision::useSource;
}

std::optional<std::string> requestError(const Request& request)
{
    if (request.name.empty())
        return "the request has no name";

    if (request.gitUrl.empty())
        return "the request for " + request.name + " has no gitUrl";

    if (request.gitRef.empty())
        return "the request for " + request.name + " has no gitRef";

    if (request.mode != "auto" && request.mode != "prebuilt"
        && request.mode != "source")
        return "the request for " + request.name + " has an unknown mode '"
               + request.mode + "'";

    return std::nullopt;
}

ResolveResult
    resolvePackage(const Request& request, const Env& env, const CacheRoot& cache)
{
    if (auto error = requestError(request))
        return failure(*error);

    const auto source =
        fetchSource(cache, request.name, request.gitUrl, request.gitRef);

    if (!source.ok)
        return failure(source.error);

    const auto& snapshot = source.snapshot;

    if (request.mode == "source")
        return success(sourceResponse(request, snapshot));

    const auto key = binaryKey(makeManifest(request, env, snapshot.sourceId));
    const auto existing = findBinary(cache, request.name, key);

    Log::step("epm: " + request.name + " binary key " + key.substr(0, 12) + " "
              + (existing ? "found" : "missing"));

    switch (decide(request, existing.has_value()))
    {
        case Decision::useSource:
            return success(sourceResponse(request, snapshot));

        case Decision::useBinary:
            return success(binaryResponse(request, snapshot, *existing));

        case Decision::buildBinary:
            break;
    }

    const auto built = buildBinary(cache, request, env, snapshot);

    if (!built.ok)
        return failure(built.error, built.details);

    return success(binaryResponse(request, snapshot, built.entry));
}
} // namespace epm
