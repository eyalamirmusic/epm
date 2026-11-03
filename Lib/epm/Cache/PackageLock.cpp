#include "PackageLock.h"

#include <epm/Util/Sha256.h>

namespace epm
{
namespace
{
std::string lockName(const CacheRoot& cache, std::string_view name)
{
    return "epm-" + computeSha256Hex(cache.path().str()).substr(0, 12) + "-"
           + std::string {name};
}
} // namespace

PackageLock::PackageLock(const CacheRoot& cache,
                         std::string_view name,
                         eacp::Time::MS timeout)
    : lock(lockName(cache, name))
    , guard(lock, timeout)
{
}

bool PackageLock::isLocked() const
{
    return guard.isLocked();
}
} // namespace epm
