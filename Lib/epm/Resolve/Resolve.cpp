#include "Resolve.h"

#include <epm/Build/Configs.h>
#include <epm/Cache/BinaryCache.h>
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

ResolveResult sourceResult(const Request& request, const SourceSnapshot& snapshot)
{
    auto result = ResolveResult {};
    result.ok = true;
    result.response.name = request.name;
    result.response.mode = "source";
    result.response.sourceDirectory = snapshot.directory;
    return result;
}

ResolveResult binaryResult(const Request& request,
                           const SourceSnapshot& snapshot,
                           const BinaryEntry& entry,
                           const AbiFingerprint& wanted)
{
    auto result = ResolveResult {};
    result.ok = true;
    result.response = {request.name,
                       "prebuilt",
                       snapshot.directory,
                       entry.prefix,
                       entry.fingerprint.platform,
                       entry.configs,
                       abiNote(entry.fingerprint, wanted).value_or("")};
    result.fingerprintHash = fingerprintId(entry.fingerprint);
    return result;
}

bool isKnownMode(const std::string& mode)
{
    return mode == "auto" || mode == "prebuilt" || mode == "binary-only"
           || mode == "source";
}
} // namespace

Decision decide(const Request& request, bool binaryExists)
{
    if (request.mode == "source")
        return Decision::useSource;

    if (binaryExists)
        return Decision::useBinary;

    if (request.mode == "binary-only")
        return Decision::missingBinary;

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

    if (!isKnownMode(request.mode))
        return "the request for " + request.name + " has an unknown mode '"
               + request.mode + "' (auto, prebuilt, binary-only or source)";

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

ResolveResult resolveSnapshot(const Request& request,
                              const Env& env,
                              const CacheRoot& cache,
                              const SourceSnapshot& snapshot,
                              const LockForChildBuilds& childLock,
                              const std::string& dependencyGraph)
{
    if (request.mode == "source")
        return sourceResult(request, snapshot);

    const auto wanted = makeFingerprint(request, env, snapshot.hash);
    const auto configs = buildConfigsFor(request.configs);
    auto entries = EA::Vector<BinaryEntry> {};

    for (const auto& found: findEntries(cache, request.name, wanted.platform))
        entries.add(withConfigsMatchingTheLock(found, childLock.lock));

    const auto existing =
        pickEntry(entries,
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
            return sourceResult(request, snapshot);

        case Decision::useBinary:
            return binaryResult(request, snapshot, *existing, wanted);

        case Decision::missingBinary:
            return failure(
                request.name + " is binary-only, but the cache has no "
                + wanted.platform + " entry holding " + joinList(configs, ", ")
                + " for " + fingerprintId(wanted).substr(0, 12)
                + (existing ? " (entry " + existing->prefix.fileName() + " lacks "
                                  + joinList(missing, ", ") + ")"
                            : ""));

        case Decision::buildBinary:
            break;
    }

    const auto built =
        buildBinary(cache, request, env, snapshot, childLock, dependencyGraph);

    if (!built.ok)
        return failure(built.error, built.details);

    return binaryResult(request, snapshot, built.entry, wanted);
}

} // namespace epm
