#pragma once

#include "Graph.h"

#include <DependencyResolution/DependencyResolution.h>

#include <Miro/Reflect.h>

#include <map>
#include <string>

namespace epm
{
// The lock's options for `settings`, with the cache and the cmake that
// extracts archives.
DependencyResolution::LockOptions lockOptionsFor(const LockSettings& settings,
                                                 const CacheRoot& cache,
                                                 const Env& env);

// The graph walk's source fetch, through the lock: each package the walk
// meets is resolved at the commit and Merkle hash epm-lock.json records (or
// records it), and its epm.cpp is checked against the lock before the walk
// runs it.
SourceFetcher
    makeLockedSourceFetcher(DependencyResolution::SourceResolver& resolver);
} // namespace epm
