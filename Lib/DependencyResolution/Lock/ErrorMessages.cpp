#include <DependencyResolution/Lock/ErrorMessages.h>

#include <eacp/Core/Utils/Format.h>
#include <eacp/Core/Utils/Strings.h>

#include <algorithm>
#include <ea_data_structures/Structures/Array.h>
#include <cctype>
#include <string_view>

namespace DependencyResolution
{
std::string formatUserError(const std::string& problem, const std::string& fix)
{
    return eacp::Strings::format("{}\n  {}", problem, fix);
}

std::string formatRequestLocation(const RequestLocation& location)
{
    if (!location.line)
        return location.file;

    return eacp::Strings::format("{}:{}", location.file, *location.line);
}

std::string describeLockMismatch(const LockMismatch& mismatch)
{
    return formatUserError(
        eacp::Strings::format("{} asks for {} {}, but {} pins it {}.",
                              formatRequestLocation(mismatch.requestedAt),
                              mismatch.name,
                              mismatch.requestedDescription,
                              mismatch.lockFile,
                              mismatch.lockedDescription),
        eacp::Strings::format("To lock what is asked for, run: {}",
                              mismatch.updateCommand));
}

std::string describeUnlockedRequest(const UnlockedRequest& request)
{
    auto location = formatRequestLocation(request.requestedAt);

    if (!request.lockFileExists)
    {
        return formatUserError(
            eacp::Strings::format("{} asks for {}, and there is no {} yet; offline "
                                  "mode cannot write one.",
                                  location,
                                  request.name,
                                  request.lockFile),
            "To write it, resolve once outside offline mode.");
    }

    return formatUserError(
        eacp::Strings::format("{} asks for {}, which {} does "
                              "not have.",
                              location,
                              request.name,
                              request.lockFile),
        eacp::Strings::format("To add it, run: {}", request.updateCommand));
}

std::string describeUnknownUpdateName(const UnknownUpdateName& unknown)
{
    auto lockedNames = eacp::Strings::join(unknown.lockedNames, ", ");

    if (lockedNames.empty())
        lockedNames = "none";

    return formatUserError(
        eacp::Strings::format("{} has no package named {}. It has: {}.",
                              unknown.lockFile,
                              unknown.name,
                              lockedNames),
        "To add a new package, request it and resolve in update mode.");
}

std::string describeFailedDownload(const FailedDownload& download)
{
    auto what = download.name;

    if (!download.commit.empty())
        what =
            eacp::Strings::format("{} at commit {}", download.name, download.commit);

    return formatUserError(
        eacp::Strings::format("Could not download {} from {}, and the cache "
                              "does not have it yet.",
                              what,
                              download.url),
        "Check the network connection and resolve again; the error above "
        "says why the download failed.");
}

std::string describeChangedContent(const ChangedContent& changed)
{
    return formatUserError(
        eacp::Strings::format("{} downloaded from {} has different files than {} "
                              "records: the source changed upstream.",
                              changed.name,
                              changed.url,
                              changed.lockFile),
        eacp::Strings::format(
            "If the change is expected, re-lock {} in update mode.", changed.name));
}

std::string describeDamagedCacheEntry(const DamagedCacheEntry& entry)
{
    return formatUserError(
        eacp::Strings::format("The cached copy of {} in {} is missing or its files "
                              "were changed after download.",
                              entry.name,
                              entry.cacheDirectory),
        "Resolve again to download a fresh copy.");
}

} // namespace DependencyResolution
