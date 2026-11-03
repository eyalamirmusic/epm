#pragma once

#include "CacheRoot.h"
#include "Fingerprint.h"
#include "SourceCache.h"

#include <epm/Spec/Env.h>
#include <epm/Spec/Request.h>

#include <optional>
#include <string>

namespace epm
{
// bin/<name>/<platform>/<fp12>/manifest.json, written when the entry is
// created, before its first config is built.
struct EntryManifest
{
    int schema = 2;
    AbiFingerprint fingerprint;
    std::string sourceId;
    std::string createdAt;

    MIRO_REFLECT(schema, fingerprint, sourceId, createdAt)
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

    MIRO_REFLECT(config, builtAt, compilerPath, compilerVersion)
};

struct BinaryEntry
{
    eacp::FilePath prefix;
    AbiFingerprint fingerprint;

    // The configs whose markers exist, canonical.
    eacp::Vector<std::string> configs;
    std::string createdAt;

    eacp::FilePath targetsFile() const;
};

// bin/<name>/<platform>/<fp12>: fp12 is the first 12 hex of fingerprintId.
eacp::FilePath entryPrefix(const CacheRoot& cache,
                           const std::string& name,
                           const AbiFingerprint& fingerprint);

// Every entry of `name` under `platform` that has a manifest of this schema.
// Entries of the old bin/<name>/<key12> layout are never seen.
eacp::Vector<BinaryEntry> findEntries(const CacheRoot& cache,
                                      const std::string& name,
                                      const std::string& platform);

// Every entry in the cache, by name then platform.
eacp::Vector<BinaryEntry> allEntries(const CacheRoot& cache);

// The entry to use for `wanted` among those `policy` accepts, preferring an
// exact (strict) match, then one holding every config in `configs`, then the
// one holding most of them, then the newest.
std::optional<BinaryEntry> pickEntry(const eacp::Vector<BinaryEntry>& entries,
                                     const AbiFingerprint& wanted,
                                     AbiMatch policy,
                                     const eacp::Vector<std::string>& configs);

struct BinaryResult
{
    bool ok = false;
    BinaryEntry entry;

    // What this call built and added to the entry.
    eacp::Vector<std::string> builtConfigs;
    std::string error;
    std::string details;
};

// Under the package lock: picks the entry for the request again (another
// process may have built it meanwhile) or creates the one for the current
// fingerprint, then builds and harvests only the configs it lacks, adding
// each to the entry as it completes. `dependencyGraph` goes to each child
// build (ChildBuild::dependencyGraph).
BinaryResult buildBinary(const CacheRoot& cache,
                         const Request& request,
                         const Env& env,
                         const SourceSnapshot& snapshot,
                         const std::string& dependencyGraph = {});
} // namespace epm
