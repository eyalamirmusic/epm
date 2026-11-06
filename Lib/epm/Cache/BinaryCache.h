#pragma once

#include "CacheRoot.h"
#include "Fingerprint.h"

#include <epm/Spec/Env.h>
#include <epm/Spec/Request.h>

#include <eacp/Core/Utils/TemporaryFile.h>

#include <cstdint>
#include <optional>
#include <string>

namespace epm
{
// The layout of bin/<name>/<platform>/<fp12>/ (Docs/PoC.md §3). Entries of
// another schema are ignored.
constexpr auto entrySchema = 3;

// <entry>/manifest.json, in the entry from the moment it appears: an entry is
// created whole, by one rename (createEntry).
struct EntryManifest
{
    int schema = entrySchema;
    AbiFingerprint fingerprint;
    std::string sourceId;
    std::string createdAt;

    MIRO_REFLECT(schema, fingerprint, sourceId, createdAt)
};

// One file a config depends on, as it was published.
struct UnitFile
{
    std::string path;
    std::int64_t size = 0;
    std::string sha256;

    MIRO_REFLECT(path, size, sha256)

    bool operator==(const UnitFile&) const = default;
};

// <entry>/configs/<Config>/epm-config.json: the config's marker, written into
// its unit before the unit is renamed into the entry, so a config is in the
// entry exactly when this is (completeness needs no re-hashing). Records the
// compiler that built it, which may differ from the manifest's for a config
// added through a compatible match, and whether its archives or objects are
// LTO objects (Lto.h). Small, since every pick reads it: the files the config
// depends on are in the unit's ConfigFiles, whose sha256 it records.
struct ConfigMarker
{
    std::string config;
    std::string builtAt;
    std::string compilerId;
    std::string compilerVersion;

    // For diagnostics only: no policy compares a path.
    std::string compilerPath;

    // llvmBitcode, gccLto, msvcLtcg or empty (Lto.h).
    std::string lto;

    // sha256 of the unit's epm-files.json, as written.
    std::string filesSha256;

    MIRO_REFLECT(
        config, builtAt, compilerId, compilerVersion, compilerPath, lto, filesSha256)
};

constexpr auto configMarkerName = std::string_view {"epm-config.json"};

// <entry>/configs/<Config>/epm-files.json, beside the marker and published
// with it, in the same rename: every file the config depends on, those of
// its unit (relative to the unit, the marker and this file left out) and the
// gen/shared files its export names (relative to the entry). Read only to
// check or copy the config (`epm cache verify`, push, pull, a cache store),
// never to pick it.
struct ConfigFiles
{
    eacp::Vector<UnitFile> files;
    eacp::Vector<UnitFile> shared;

    MIRO_REFLECT(files, shared)
};

constexpr auto configFilesName = std::string_view {"epm-files.json"};

struct LoadedConfigFiles
{
    std::optional<ConfigFiles> files;

    // Why there are none: missing, not readable, or not what the marker
    // recorded.
    std::string error;
};

// The ConfigFiles of the config `marker` describes in `unit`, checked
// against the marker's filesSha256.
LoadedConfigFiles loadConfigFiles(const eacp::FilePath& unit,
                                  const ConfigMarker& marker);

// Whether `name` is a config's canonical spelling (Debug, Release,
// RelWithDebInfo, MinSizeRel, or a name that is none of those in any case):
// a unit spelled otherwise (configs/release) is never read as a config.
bool isCanonicalConfigName(const std::string& name);

// Whether a consumer with `wanted`'s compiler can link the config, under
// every policy: always, unless the config holds LTO objects, which link only
// with the compiler that wrote them (the same id and full version).
bool configUsableBy(const ConfigMarker& marker, const AbiFingerprint& wanted);

struct BinaryEntry
{
    eacp::FilePath prefix;
    AbiFingerprint fingerprint;

    // The configs published in the entry, canonical.
    eacp::Vector<std::string> configs;
    std::string createdAt;

    // Their markers, in the order of configs.
    eacp::Vector<ConfigMarker> markers;

    eacp::FilePath targetsFile() const;
    const ConfigMarker* markerOf(const std::string& config) const;

    // The configs a consumer with `wanted`'s compiler can link
    // (configUsableBy), canonical.
    eacp::Vector<std::string> usableConfigs(const AbiFingerprint& wanted) const;
};

// bin/<name>/<platform>/<fp12>: fp12 is the first 12 hex of fingerprintId.
eacp::FilePath entryPrefix(const CacheRoot& cache,
                           const std::string& name,
                           const AbiFingerprint& fingerprint);

// Every entry of `name` under `platform` that has a manifest of this schema.
// Hidden directories (staging) and entries of the old bin/<name>/<key12>
// layout are never seen.
eacp::Vector<BinaryEntry> findEntries(const CacheRoot& cache,
                                      const std::string& name,
                                      const std::string& platform);

// The same in the bin/ directory `bin`, creating nothing: a cache store's.
eacp::Vector<BinaryEntry> findEntriesIn(const eacp::FilePath& bin,
                                        const std::string& name,
                                        const std::string& platform);

// Every entry in the cache, by name then platform.
eacp::Vector<BinaryEntry> allEntries(const CacheRoot& cache);

// The same in the bin/ directory `bin`, creating nothing.
eacp::Vector<BinaryEntry> allEntriesIn(const eacp::FilePath& bin);

// The package an entry belongs to: bin/<name>/<platform>/<fp12>.
std::string entryName(const BinaryEntry& entry);

// Whether the entry's directory is named after its fingerprint (fp12), as
// every entry epm makes is: one that is not was renamed or tampered with.
bool entryNamedByFingerprint(const BinaryEntry& entry);

// The entry to use for `wanted` among those `policy` accepts, leaving out
// one that holds a config in `configs` that `wanted` cannot link
// (configUsableBy): it could not be completed. Preferred, in order: one that
// holds any config at all (an empty one is a failed build's leftover), one
// holding every config in `configs` that `wanted` can link, an exact (strict)
// match, the one holding most of them, then the lowest fp12, so the same
// entries give the same pick on every machine. So a complete compatible
// entry is used before an exact one that lacks a config, and when none is
// complete the exact one is picked, to build the missing configs into.
std::optional<BinaryEntry> pickEntry(const eacp::Vector<BinaryEntry>& entries,
                                     const AbiFingerprint& wanted,
                                     AbiMatch policy,
                                     const eacp::Vector<std::string>& configs);

// The entry at `prefix`: its manifest and the configs whose units hold a
// marker that names them, spelled the same and canonically
// (isCanonicalConfigName); nullopt without a manifest of this schema. Reads
// only the markers, never a unit's ConfigFiles.
std::optional<BinaryEntry> readEntry(const eacp::FilePath& prefix);

// The entry for `fingerprint`, created if there is none: a manifest and the
// facade are staged and renamed into place, so the entry appears whole, and
// a process that loses that race to another takes the winner's. What sits at
// the path without a manifest of this schema (an entry of an older layout,
// or one an older epm left half made) is moved aside and deleted first.
std::optional<BinaryEntry> createEntry(const CacheRoot& cache,
                                       const std::string& name,
                                       const AbiFingerprint& fingerprint,
                                       const std::string& sourceId);

// bin/<name>/<platform>/.staging: private directories, each laid out as an
// entry, where configs are built before they are published and entries are
// assembled before they appear. Readers never look there.
eacp::FilePath stagingDirectory(const eacp::FilePath& entryPrefix);

// A fresh private directory in stagingDirectory(entryPrefix), deleted with
// the returned object. Its name ends in who made it and when,
// ".<pid>-<host12>.<unix seconds>" (host12: the first 12 hex of the sha256
// of the host name), which removeStaleStaging reads.
eacp::TemporaryFile newStaging(const eacp::FilePath& entryPrefix);

// What removeStaleStaging reads of a name newStaging made.
struct StagingOwner
{
    std::int64_t createdAt = 0;

    // 0 and empty in a name that does not say (an older epm's).
    std::int64_t processId = 0;
    std::string host;
};

std::optional<StagingOwner> stagingOwner(const std::string& name);

// The first 12 hex of the sha256 of this machine's host name.
std::string hostId();

// A staging directory, or a build tree named after one, unused for this long
// is stale whoever made it: no build goes a week without a heartbeat.
constexpr auto staleStagingSeconds = std::int64_t {7 * 24 * 60 * 60};

// Records in `directory` (a staging directory or the build tree named after
// one) that the process using it is alive now: a long build calls it before
// each config, so a process on another host sharing the cache can tell a
// live build from a crashed one by more than its name's age.
void touchStagingHeartbeat(const eacp::FilePath& directory);

// Deletes what a crashed or killed process left in `directory` (a staging or
// build-tree directory): children named as newStaging names them that a
// process of this host made that is no longer running, whatever their age;
// and those of another host (whose processes this one cannot see) or of an
// older name without a process, whose last heartbeat (touchStagingHeartbeat,
// else the name's creation time) is older than staleStagingSeconds. Others
// are left alone. Runs without a lock: what it deletes no live process uses.
void removeStaleStaging(const eacp::FilePath& directory);

// Removes stagingDirectory(entryPrefix) when nothing is staged in it.
void removeStagingIfEmpty(const eacp::FilePath& entryPrefix);

// <unit>/export-<config>.cmake, which a harvested build of `config` leaves in
// its unit.
eacp::FilePath exportFileFor(const eacp::FilePath& unit, const std::string& config);

// True once the export file of `config` and its .names are in the unit.
bool hasExport(const eacp::FilePath& unit, const std::string& config);

struct StagedConfig
{
    std::optional<ConfigMarker> marker;
    ConfigFiles files;
    std::string error;
};

// The marker of the harvested `config` in the staged entry `staged`, built
// with `env`'s compiler, and its ConfigFiles: every file of its unit and
// every gen/shared file its export names, each hashed, and what LTO objects
// they hold. The ConfigFiles are written into the unit (epm-files.json), and
// the marker records their sha256; publishConfig writes the marker.
StagedConfig describeStagedConfig(const eacp::FilePath& staged,
                                  const std::string& config,
                                  const Env& env);

enum class PublishResult
{
    published,

    // Another process published the config first; this one's was dropped.
    alreadyThere,
    failed
};

struct Published
{
    PublishResult result = PublishResult::failed;
    std::string error;
};

// Publishes the config `marker` describes from the staged entry `staged`
// into the entry at `prefix`: first each gen/shared file its staged
// epm-files.json names that the entry lacks (one rename each; a file already
// there, which has the same contents by its path, is kept), then its marker
// into the unit, then the unit by a single rename to configs/<config>, which
// fails rather than replace a published unit. Files of configs already
// published are never
// touched, and correctness needs no lock: of two publishers the second finds
// the first's unit and drops its own.
Published publishConfig(const eacp::FilePath& prefix,
                        const eacp::FilePath& staged,
                        const ConfigMarker& marker);
} // namespace epm
