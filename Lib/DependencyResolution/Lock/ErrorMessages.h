#pragma once

#include <eacp/Core/Utils/Containers.h>

#include <optional>
#include <string>

namespace DependencyResolution
{
// Where a package was asked for, as the user should read it: a path relative to
// the project when it is inside it, and the line when one call names it.
struct RequestLocation
{
    std::string file;
    std::optional<int> line;
};

struct LockMismatch
{
    RequestLocation requestedAt;
    std::string name;
    std::string lockFile;
    std::string requestedDescription;
    std::string lockedDescription;
    std::string updateCommand;
};

struct UnlockedRequest
{
    RequestLocation requestedAt;
    std::string name;
    std::string lockFile;
    bool lockFileExists = false;
    std::string updateCommand;
};

struct UnknownUpdateName
{
    std::string name;
    std::string lockFile;
    EA::Vector<std::string> lockedNames;
};

struct FailedDownload
{
    std::string name;
    std::string url;
    std::string commit;
};

struct ChangedContent
{
    std::string name;
    std::string url;
    std::string lockFile;
};

struct DamagedCacheEntry
{
    std::string name;
    std::string cacheDirectory;
};

// Every failure a user reads goes through this: what happened, then the
// command that fixes it, each on its own line.
std::string formatUserError(const std::string& problem, const std::string& fix);

std::string describeLockMismatch(const LockMismatch& mismatch);
std::string describeUnlockedRequest(const UnlockedRequest& request);
std::string describeUnknownUpdateName(const UnknownUpdateName& unknown);
std::string describeFailedDownload(const FailedDownload& download);
std::string describeChangedContent(const ChangedContent& changed);
std::string describeDamagedCacheEntry(const DamagedCacheEntry& entry);

std::string formatRequestLocation(const RequestLocation& location);

} // namespace DependencyResolution
