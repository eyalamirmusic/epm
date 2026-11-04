#pragma once

#include "CacheRoot.h"
#include "Fingerprint.h"
#include "SourceCache.h"

#include <epm/Build/ChildBuild.h>
#include <epm/Spec/Env.h>
#include <epm/Spec/Request.h>

#include <map>
#include <optional>
#include <string>

namespace epm
{
// bin/<name>/<platform>/<fp12>/manifest.json, written when the entry is
// created, before its first config is built.
struct EntryManifest
{
    int schema = 3;
    AbiFingerprint fingerprint;
    std::string sourceHash;
    std::string createdAt;

    MIRO_REFLECT(schema, fingerprint, sourceHash, createdAt)
};

// <prefix>/<Config>.json, written last for that config, after its harvest:
// the config is in the entry once this exists. Records the compiler that
// built it, which may differ from the manifest's for a config added through a
// compatible match.
struct ConfigMarker
{
    std::string config;
    std::string builtAt;
    std::string compilerPath;
    std::string compilerVersion;

    // The Merkle hash of every package the child build's own CPMAddPackage
    // and FetchContent calls took from the lock, by name.
    std::map<std::string, std::string> lockedDependencies;

    MIRO_REFLECT(config, builtAt, compilerPath, compilerVersion, lockedDependencies)
};

struct BinaryEntry
{
    eacp::FilePath prefix;
    AbiFingerprint fingerprint;

    // The configs whose markers exist, canonical.
    EA::Vector<std::string> configs;

    // Each config's lockedDependencies.
    std::map<std::string, std::map<std::string, std::string>> lockedDependencies;
    std::string createdAt;

    eacp::FilePath targetsFile() const;
};

// bin/<name>/<platform>/<fp12>: fp12 is the first 12 hex of fingerprintId.
eacp::FilePath entryPrefix(const CacheRoot& cache,
                           const std::string& name,
                           const AbiFingerprint& fingerprint);

// Every entry of `name` under `platform` that has a manifest of this schema.
// Entries of the old bin/<name>/<key12> layout are never seen.
EA::Vector<BinaryEntry> findEntries(const CacheRoot& cache,
                                    const std::string& name,
                                    const std::string& platform);

// The entry with only the configs whose lockedDependencies the lock still
// holds at the same hashes; the others are rebuilt.
BinaryEntry withConfigsMatchingTheLock(BinaryEntry entry,
                                       const eacp::FilePath& lock);

// Every entry in the cache, by name then platform.
EA::Vector<BinaryEntry> allEntries(const CacheRoot& cache);

// The entry to use for `wanted` among those `policy` accepts, preferring an
// exact (strict) match, then one holding every config in `configs`, then the
// one holding most of them, then the newest.
std::optional<BinaryEntry> pickEntry(const EA::Vector<BinaryEntry>& entries,
                                     const AbiFingerprint& wanted,
                                     AbiMatch policy,
                                     const EA::Vector<std::string>& configs);

struct BinaryResult
{
    bool ok = false;
    BinaryEntry entry;

    // What this call built and added to the entry.
    EA::Vector<std::string> builtConfigs;
    std::string error;
    std::string details;
};

// Under the package lock: picks the entry for the request again (another
// process may have built it meanwhile) or creates the one for the current
// fingerprint, then builds and harvests only the configs it lacks (or whose
// locked dependencies changed), adding each to the entry as it completes.
// `dependencyGraph` goes to each child build (ChildBuild::dependencyGraph),
// and so does the lock its own package calls resolve against.
BinaryResult buildBinary(const CacheRoot& cache,
                         const Request& request,
                         const Env& env,
                         const SourceSnapshot& snapshot,
                         const LockForChildBuilds& childLock,
                         const std::string& dependencyGraph = {});
} // namespace epm
