#pragma once

#include "CacheRoot.h"
#include "Fingerprint.h"

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

// The entry to use for `wanted` among those `policy` accepts, preferring one
// that holds any config at all (an empty one is a failed build's leftover),
// then an exact (strict) match, then one holding every config in `configs`,
// then the one holding most of them, then the newest.
std::optional<BinaryEntry> pickEntry(const eacp::Vector<BinaryEntry>& entries,
                                     const AbiFingerprint& wanted,
                                     AbiMatch policy,
                                     const eacp::Vector<std::string>& configs);

// The entry at `prefix`: its manifest and the configs whose markers exist;
// nullopt without a manifest of this schema.
std::optional<BinaryEntry> readEntry(const eacp::FilePath& prefix);

// A fresh entry for `fingerprint`, its manifest written first. Whatever is at
// its path has no usable manifest (an exact match would have been picked), so
// it is a leftover of an interrupted creation and goes. Call it under the
// package lock.
std::optional<BinaryEntry> createEntry(const CacheRoot& cache,
                                       const std::string& name,
                                       const AbiFingerprint& fingerprint,
                                       const std::string& sourceId);

// Removes what an unmarked `config` may have left in the entry: its marker,
// lib/<config>, gen/<config> and its export files. gen/shared stays: its
// files are content-addressed and other configs point at them.
bool clearConfig(const eacp::FilePath& prefix, const std::string& config);

// <prefix>/export-<config>.cmake, which a harvested build of `config` leaves.
eacp::FilePath exportFileFor(const eacp::FilePath& prefix,
                             const std::string& config);

// True once the export file of `config` and its .names are in the entry.
bool hasExport(const eacp::FilePath& prefix, const std::string& config);

// Adds a harvested `config` to the entry: writes its marker (built with
// `env`'s compiler), then regenerates epm-targets.cmake for every config
// marked. The marker comes first, so the facade never names a config whose
// files may still be cleared. Returns the error.
std::optional<std::string> addConfigToEntry(const eacp::FilePath& prefix,
                                            const std::string& config,
                                            const Env& env);
} // namespace epm
