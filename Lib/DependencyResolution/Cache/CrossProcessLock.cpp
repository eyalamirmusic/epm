#include <DependencyResolution/Cache/CrossProcessLock.h>

#include <eacp/Core/Utils/Sha256.h>

namespace DependencyResolution
{
namespace
{
std::string lockName(const CacheRoot& cache, std::string_view name)
{
    return "epm-" + eacp::computeSha256Hex(cache.path().str()).substr(0, 12) + "-"
           + std::string {name};
}
} // namespace

CrossProcessLock::CrossProcessLock(const CacheRoot& cache,
                                   std::string_view name,
                                   eacp::Time::MS timeout)
    : lock(lockName(cache, name))
    , guard(lock, timeout)
{
}

bool CrossProcessLock::isLocked() const
{
    return guard.isLocked();
}
} // namespace DependencyResolution
