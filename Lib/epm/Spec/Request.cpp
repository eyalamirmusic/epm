#include "Request.h"

#include <epm/Build/Configs.h>
#include <epm/Cache/Fingerprint.h>

namespace epm
{
namespace
{
bool isKnownMode(const std::string& mode)
{
    return mode == "auto" || mode == "prebuilt" || mode == "binary-only"
           || mode == "source";
}
} // namespace

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
} // namespace epm
