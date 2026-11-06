#pragma once

#include "BinaryCache.h"
#include "CacheRoot.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// Cache stores (Docs/PoC.md §3): directories laid out as a cache's bin/ (a
// network share, a synced folder, an unpacked CI artifact) that epm reads
// binary entries from and never writes to while it configures. A config a
// store holds is copied into the local cache, checked against its marker,
// and used from there; `epm cache push` fills a store from the local cache.
namespace epm
{
// Why `store` cannot be read as a cache store: it does not exist, is not a
// directory, or cannot be listed. nullopt when it can.
std::optional<std::string> storeProblem(const eacp::FilePath& store);

// One note per store of `cache` that storeProblem rejects, "the cache store
// <dir> <problem>: not used".
eacp::Vector<std::string> storeNotes(const CacheRoot& cache);

struct CopiedConfig
{
    PublishResult result = PublishResult::failed;
    std::string error;

    // What was copied: the unit's files and the gen/shared files the
    // destination lacked.
    std::int64_t bytes = 0;
};

// Copies the published config `marker` describes from the entry at `from`
// into the entry at `to`, which must exist for the same fingerprint, by the
// protocol a build publishes with (publishConfig): the unit's files, and the
// gen/shared files it names that `to` lacks, as its epm-files.json lists
// them (checked against the marker's filesSha256), are copied into a fresh
// staging directory of `to`, each checked against its recorded size and
// sha256 as it is read, then published by one no-replace rename. The copy
// fails, publishing nothing, on a file that is missing or differs, a path
// outside its directory or a shared one outside gen/shared/, a unit that
// lists no export file or no .names, LTO objects the marker's lto does not
// name (ltoKindOf of the bytes read, the files `to` has already included),
// and an LTO config recorded for a compiler other than the entry's. Nothing
// is copied when `to` holds the config already.
CopiedConfig copyConfig(const eacp::FilePath& from,
                        const eacp::FilePath& to,
                        const ConfigMarker& marker);

struct StorePull
{
    // "<Config> from <store>", one per config copied into the local cache.
    eacp::Vector<std::string> copied;

    // A unit that could not be copied, one line each, and skipped.
    eacp::Vector<std::string> notes;
};

// Fills the local cache from cache.stores() for `wanted` and `configs` when
// the local cache's own pick (pickEntry under `policy`) lacks some of them.
// Each store is searched in turn, until the local pick holds them all: its
// entries of `name` are added to the local ones, config by config for the
// same fp12, and picked by pickEntry's rules; when that pick holds more of
// `configs` than the local one, its configs the local cache lacks are copied
// (copyConfig) into the local entry of that fp12, created with the store
// entry's fingerprint when missing. A unit that fails to copy is skipped with
// a note and the pick is made again without it. A store that storeProblem
// rejects, or an entry not named after its fingerprint, is passed over.
StorePull pullFromStores(const CacheRoot& cache,
                         const std::string& name,
                         const AbiFingerprint& wanted,
                         AbiMatch policy,
                         const eacp::Vector<std::string>& configs);

struct Transfer
{
    // Configs copied, entries they went into, and their size.
    int configs = 0;
    int entries = 0;
    std::int64_t bytes = 0;

    // Configs the destination held already.
    int alreadyThere = 0;

    // One line each, "<name>/<platform>/<fp12>[ <Config>]: <problem>".
    eacp::Vector<std::string> problems;
};

// `epm cache push`: every published config of the local cache (only those of
// `names` when it is not empty) copied into the store directory `store`
// (copyConfig), its entries created there as a build creates them
// (createEntry), each config published by one no-replace rename, so pushes
// from several machines into one store are safe; configs already there are
// skipped.
Transfer pushToStore(const CacheRoot& cache,
                     const eacp::FilePath& store,
                     const eacp::Vector<std::string>& names);

// `epm cache pull`: the same from the store into the local cache.
Transfer pullFromStore(const CacheRoot& cache,
                       const eacp::FilePath& store,
                       const eacp::Vector<std::string>& names);

// The problems, one per line, then "<verb> <n> configs (<size>) into <n>
// entries <direction> <where>; <n> already there".
std::string transferText(const Transfer& transfer,
                         std::string_view verb,
                         std::string_view direction,
                         const eacp::FilePath& where);
} // namespace epm
