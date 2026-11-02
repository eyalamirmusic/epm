#pragma once

#include "CacheRoot.h"

#include <eacp/Network/IPC/Lock.h>

#include <string_view>

namespace epm
{
// Serialises every process that fetches or builds one package in one cache.
// Waits up to `timeout` for the holder (a build can take minutes); ask
// isLocked() before touching the package's entries.
//
// eacp's IPC::Lock keeps its lock files in its own per-user directory, so the
// lock name carries a hash of the cache root to keep two caches independent.
class PackageLock
{
public:
    PackageLock(const CacheRoot& cache,
                std::string_view name,
                eacp::Time::MS timeout);

    bool isLocked() const;

private:
    eacp::IPC::Lock lock;
    eacp::IPC::ScopedLock guard;
};

// Long enough for another process to finish a full Debug + Release build.
constexpr auto packageLockTimeout = eacp::Time::MS {60 * 60 * 1000};
} // namespace epm
