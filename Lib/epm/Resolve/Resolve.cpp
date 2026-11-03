#include "Resolve.h"

#include <epm/Build/Configs.h>
#include <epm/Cache/BinaryCache.h>
#include <epm/Cache/SourceCache.h>
#include <epm/Util/Log.h>
#include <epm/Util/Text.h>

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
                        const BinaryEntry& entry,
                        const AbiFingerprint& wanted)
{
    return {request.name,
            "prebuilt",
            snapshot.directory,
            entry.prefix,
            entry.fingerprint.platform,
            entry.configs,
            abiNote(entry.fingerprint, wanted).value_or("")};
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
    if (request.schema != requestSchema)
        return "the request" + (request.name.empty() ? "" : " for " + request.name)
               + " has schema " + std::to_string(request.schema)
               + ", but this epm reads schema " + std::to_string(requestSchema)
               + ": epm.cmake and the epm binary come from different versions "
                 "(include get_epm.cmake, which takes both from one install)";

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

    if (request.configs.empty())
        return "the request for " + request.name + " lists no configs";

    for (const auto& config: request.configs)
        if (!buildConfigFor(config))
            return "the request for " + request.name + " has an unknown config '"
                   + config + "' (Debug, Release, RelWithDebInfo or MinSizeRel)";

    if (!parseAbiMatch(request.abiMatch))
        return "the request for " + request.name + " has an unknown abiMatch '"
               + request.abiMatch + "' (compatible, strict or any)";

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

    const auto wanted = makeFingerprint(request, env, snapshot.sourceId);
    const auto configs = buildConfigsFor(request.configs);
    const auto existing =
        pickEntry(findEntries(cache, request.name, wanted.platform),
                  wanted,
                  parseAbiMatch(request.abiMatch).value_or(AbiMatch::compatible),
                  configs);
    const auto missing =
        existing ? missingConfigs(existing->configs, configs) : configs;

    Log::step("epm: " + request.name + " " + wanted.platform + " "
              + fingerprintId(wanted).substr(0, 12) + ": "
              + (existing ? "entry " + existing->prefix.fileName() : "no entry")
              + (missing.empty() ? "" : ", missing " + joinList(missing, ", ")));

    switch (decide(request, existing && missing.empty()))
    {
        case Decision::useSource:
            return success(sourceResponse(request, snapshot));

        case Decision::useBinary:
            return success(binaryResponse(request, snapshot, *existing, wanted));

        case Decision::buildBinary:
            break;
    }

    const auto built = buildBinary(cache, request, env, snapshot);

    if (!built.ok)
        return failure(built.error, built.details);

    return success(binaryResponse(request, snapshot, built.entry, wanted));
}
} // namespace epm
