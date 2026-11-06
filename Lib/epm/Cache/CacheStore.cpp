#include "CacheStore.h"
#include "CacheListing.h"

#include <epm/Build/Configs.h>
#include <epm/Build/Harvest.h>
#include <epm/Build/Lto.h>
#include <epm/Util/Sha256.h>
#include <epm/Util/Text.h>

#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/StdPath.h>

#include <filesystem>
#include <set>

namespace epm
{
using namespace eacp;

namespace
{
// A marker's path stays inside the directory it is relative to: a store is
// read from other machines, so its markers are not trusted to.
bool isInside(const std::string& path)
{
    const auto relative = std::filesystem::path {path};

    if (path.empty() || relative.is_absolute() || relative.has_root_name())
        return false;

    for (const auto& part: relative)
        if (part == "..")
            return false;

    return true;
}

constexpr auto sharedPrefix = std::string_view {"gen/shared/"};

// The bytes of `from`, checked against `recorded`; the problem otherwise.
struct CheckedFile
{
    std::optional<std::string> bytes;
    std::string problem;
};

CheckedFile readChecked(const FilePath& from, const UnitFile& recorded)
{
    if (!Files::isFile(from))
        return {std::nullopt, "missing"};

    auto bytes = Files::readFile(from);

    if (static_cast<std::int64_t>(bytes.size()) != recorded.size)
        return {std::nullopt,
                "size " + std::to_string(bytes.size()) + ", recorded "
                    + std::to_string(recorded.size)};

    if (sha256Hex(bytes) != recorded.sha256)
        return {std::nullopt, "sha256 differs from the recorded one"};

    return {std::move(bytes), {}};
}

// What makes a unit's marker and files list unusable before anything is
// copied: a path outside its directory, a shared file outside gen/shared/,
// the unit's own bookkeeping named as one of its files, a unit without the
// export file and its .names (which the facade reads), or LTO objects
// recorded for a compiler other than the entry's, which epm never publishes
// (LTO configs go only into their compiler's exact entry).
std::optional<std::string> unitProblem(const ConfigMarker& marker,
                                       const ConfigFiles& files,
                                       const AbiFingerprint& entry)
{
    const auto exportName = "export-" + marker.config + ".cmake";
    auto hasExport = false;
    auto hasNames = false;

    for (const auto& file: files.files)
    {
        if (!isInside(file.path))
            return file.path + ": named outside its directory";

        if (file.path == configMarkerName || file.path == configFilesName)
            return file.path + ": the unit's own bookkeeping, listed as a file";

        hasExport = hasExport || file.path == exportName;
        hasNames = hasNames || file.path == exportName + ".names";
    }

    if (!hasExport || !hasNames)
        return "the unit lists no " + exportName + (hasExport ? ".names" : "");

    for (const auto& file: files.shared)
        if (!isInside(file.path) || !file.path.starts_with(sharedPrefix))
            return file.path + ": a shared file outside "
                   + std::string {sharedPrefix};

    if (!marker.lto.empty()
        && (marker.compilerId != entry.compilerId
            || marker.compilerVersion != entry.compilerVersion))
        return "it records " + marker.lto + " objects of " + marker.compilerId + " "
               + marker.compilerVersion + " in an entry of " + entry.compilerId + " "
               + entry.compilerVersion;

    return std::nullopt;
}

// The marker's lto against the LTO kinds the copied files hold.
bool ltoAgrees(const std::string& recorded, const std::set<std::string>& kinds)
{
    return recorded.empty() ? kinds.empty() : kinds.contains(recorded);
}

// copyConfig's copy into a fresh staging directory of `to`, deleted on
// return, and its publish: `entry` is the fingerprint of both entries.
CopiedConfig copyThroughStaging(const FilePath& from,
                                const FilePath& to,
                                const ConfigMarker& marker,
                                const AbiFingerprint& entry)
{
    auto copied = CopiedConfig {};
    const auto failure = [&](const std::string& error)
    {
        copied.result = PublishResult::failed;
        copied.error = error;
        return copied;
    };

    const auto sourceUnit = configDirectory(from, marker.config);
    const auto loaded = loadConfigFiles(sourceUnit, marker);

    if (!loaded.files)
        return failure(loaded.error);

    const auto& files = *loaded.files;

    if (auto problem = unitProblem(marker, files, entry))
        return failure(*problem);

    const auto staging = newStaging(to);
    const auto unit = configDirectory(staging.path(), marker.config);
    auto kinds = std::set<std::string> {};

    const auto copy = [&](const FilePath& source,
                          const FilePath& destination,
                          const UnitFile& file) -> std::optional<std::string>
    {
        const auto checked = readChecked(source, file);

        if (!checked.bytes)
            return file.path + ": " + checked.problem;

        if (auto kind = ltoKindOf(*checked.bytes); !kind.empty())
            kinds.insert(std::move(kind));

        if (!writeTextFile(destination, *checked.bytes))
            return file.path + ": cannot copy it to " + destination.str();

        copied.bytes += file.size;
        return std::nullopt;
    };

    for (const auto& file: files.files)
        if (auto problem = copy(sourceUnit / file.path, unit / file.path, file))
            return failure(*problem);

    for (const auto& file: files.shared)
    {
        // The same contents by its path, so it is read only for its LTO kind.
        if (const auto there = to / file.path; Files::isFile(there))
        {
            if (auto kind = ltoKindOf(Files::readFile(there)); !kind.empty())
                kinds.insert(std::move(kind));

            continue;
        }

        if (auto problem = copy(from / file.path, staging.path() / file.path, file))
            return failure(*problem);
    }

    if (!ltoAgrees(marker.lto, kinds))
        return failure("its files hold "
                       + (kinds.empty() ? std::string {"no LTO objects"}
                                        : *kinds.begin() + " objects")
                       + ", but the marker records "
                       + (marker.lto.empty() ? std::string {"none"} : marker.lto));

    const auto filesFile = unit / std::string {configFilesName};

    if (!writeTextFile(filesFile,
                       Files::readFile(sourceUnit / std::string {configFilesName})))
        return failure("cannot write " + filesFile.str());

    const auto published = publishConfig(to, staging.path(), marker);
    copied.result = published.result;
    copied.error = published.error;
    return copied;
}

std::string entryLabel(const BinaryEntry& entry)
{
    return entryName(entry) + "/" + entry.fingerprint.platform + "/"
           + entry.prefix.fileName();
}

const BinaryEntry* entryWithId(const Vector<BinaryEntry>& entries,
                               const std::string& fp12)
{
    return entries.findIf([&](const BinaryEntry& entry)
                          { return entry.prefix.fileName() == fp12; });
}

// `local` with the configs of `store` added to the entry of the same fp12, and
// the store's other entries beside them: what the local cache would hold with
// the store copied into it.
Vector<BinaryEntry> withStoreConfigs(Vector<BinaryEntry> local,
                                     const Vector<BinaryEntry>& store)
{
    for (const auto& entry: store)
    {
        auto* same = local.findIf(
            [&](const BinaryEntry& candidate)
            { return candidate.prefix.fileName() == entry.prefix.fileName(); });

        if (same == nullptr)
        {
            local.add(entry);
            continue;
        }

        for (const auto& marker: entry.markers)
        {
            if (same->markerOf(marker.config) == nullptr)
            {
                same->configs.add(marker.config);
                same->markers.add(marker);
            }
        }

        same->configs = canonicalConfigs(same->configs);
    }

    return local;
}

// The store's usable entries of `name`, without the units that failed to copy
// ("<fp12>/<Config>").
Vector<BinaryEntry> storeEntries(const FilePath& store,
                                 const std::string& name,
                                 const std::string& platform,
                                 const std::set<std::string>& rejected)
{
    auto entries = Vector<BinaryEntry> {};

    for (auto entry: findEntriesIn(store / "bin", name, platform))
    {
        if (!entryNamedByFingerprint(entry))
            continue;

        const auto fp12 = entry.prefix.fileName();
        auto kept =
            BinaryEntry {entry.prefix, entry.fingerprint, {}, entry.createdAt, {}};

        for (const auto& marker: entry.markers)
        {
            if (!rejected.contains(fp12 + "/" + marker.config))
            {
                kept.configs.add(marker.config);
                kept.markers.add(marker);
            }
        }

        entries.add(std::move(kept));
    }

    return entries;
}

std::size_t missingCount(const std::optional<BinaryEntry>& entry,
                         const AbiFingerprint& wanted,
                         const Vector<std::string>& configs)
{
    if (!entry)
        return configs.size();

    return missingConfigs(entry->usableConfigs(wanted), configs).size();
}

// Copies what the store at `store` lets the local pick gain, until it gains
// nothing more.
void pullForPick(const CacheRoot& cache,
                 const FilePath& store,
                 const std::string& name,
                 const AbiFingerprint& wanted,
                 AbiMatch policy,
                 const Vector<std::string>& configs,
                 StorePull& pull)
{
    auto rejected = std::set<std::string> {};

    // Each round copies or rejects at least one unit, so it ends.
    while (true)
    {
        const auto local = findEntries(cache, name, wanted.platform);
        const auto localPick = pickEntry(local, wanted, policy, configs);
        const auto localMissing = missingCount(localPick, wanted, configs);

        if (localMissing == 0)
            return;

        const auto fromStore = storeEntries(store, name, wanted.platform, rejected);
        const auto best =
            pickEntry(withStoreConfigs(local, fromStore), wanted, policy, configs);

        if (!best || missingCount(best, wanted, configs) >= localMissing)
            return;

        const auto fp12 = best->prefix.fileName();
        const auto* source = entryWithId(fromStore, fp12);
        const auto* existing = entryWithId(local, fp12);

        if (source == nullptr)
            return;

        auto target = existing != nullptr
                          ? std::optional<BinaryEntry> {*existing}
                          : createEntry(cache,
                                        name,
                                        source->fingerprint,
                                        source->fingerprint.sourceId);
        auto progressed = false;

        for (const auto& config: best->usableConfigs(wanted))
        {
            if (!configs.contains(config)
                || (target && target->markerOf(config) != nullptr))
                continue;

            const auto* marker = source->markerOf(config);

            if (marker == nullptr)
                continue;

            progressed = true;
            const auto copied =
                target
                    ? copyConfig(source->prefix, target->prefix, *marker)
                    : CopiedConfig {
                          PublishResult::failed,
                          "cannot create the local entry "
                              + entryPrefix(cache, name, source->fingerprint).str(),
                          0};

            if (copied.result == PublishResult::failed)
            {
                rejected.insert(fp12 + "/" + config);
                pull.notes.add("the cache store " + store.str() + " holds " + config
                               + " of " + name + " (" + fp12
                               + "), but it cannot be used: " + copied.error);
            }
            else
                pull.copied.add(config + " from " + store.str());
        }

        if (!progressed)
            return;
    }
}

Transfer transferEntries(const Vector<BinaryEntry>& entries,
                         const CacheRoot& destination,
                         const Vector<std::string>& names)
{
    auto transfer = Transfer {};

    for (const auto& name: names)
        if (entries.findIf([&](const BinaryEntry& entry)
                           { return entryName(entry) == name; })
            == nullptr)
            transfer.problems.add(name + ": no entries");

    for (const auto& entry: entries)
    {
        const auto name = entryName(entry);
        const auto label = entryLabel(entry);

        if (!names.empty() && !names.contains(name))
            continue;

        if (!entryNamedByFingerprint(entry))
        {
            transfer.problems.add(label + ": not named after its fingerprint");
            continue;
        }

        if (entry.markers.empty())
            continue;

        const auto target = createEntry(
            destination, name, entry.fingerprint, entry.fingerprint.sourceId);

        if (!target)
        {
            transfer.problems.add(
                label + ": cannot create "
                + entryPrefix(destination, name, entry.fingerprint).str());
            continue;
        }

        auto copiedAny = false;

        for (const auto& marker: entry.markers)
        {
            const auto copied = copyConfig(entry.prefix, target->prefix, marker);

            if (copied.result == PublishResult::failed)
                transfer.problems.add(label + " " + marker.config + ": "
                                      + copied.error);
            else if (copied.result == PublishResult::alreadyThere)
                ++transfer.alreadyThere;
            else
            {
                ++transfer.configs;
                transfer.bytes += copied.bytes;
                copiedAny = true;
            }
        }

        if (copiedAny)
            ++transfer.entries;
    }

    return transfer;
}
} // namespace

std::optional<std::string> storeProblem(const FilePath& store)
{
    if (!Files::exists(store))
        return "does not exist";

    if (!Files::isDirectory(store))
        return "is not a directory";

    auto failed = std::error_code {};
    std::filesystem::directory_iterator {toStdPath(store), failed};

    if (failed)
        return "cannot be read (" + failed.message() + ")";

    return std::nullopt;
}

Vector<std::string> storeNotes(const CacheRoot& cache)
{
    auto notes = Vector<std::string> {};

    for (const auto& store: cache.stores())
        if (const auto problem = storeProblem(store))
            notes.add("the cache store " + store.str() + " " + *problem
                      + ": not used");

    return notes;
}

CopiedConfig
    copyConfig(const FilePath& from, const FilePath& to, const ConfigMarker& marker)
{
    const auto& config = marker.config;
    auto copied = CopiedConfig {};

    const auto existing = readEntry(to);

    if (!existing)
    {
        copied.error = "no entry at " + to.str();
        return copied;
    }

    if (existing->markerOf(config))
    {
        copied.result = PublishResult::alreadyThere;
        return copied;
    }

    removeStaleStaging(stagingDirectory(to));
    copied = copyThroughStaging(from, to, marker, existing->fingerprint);
    removeStagingIfEmpty(to);
    return copied;
}

StorePull pullFromStores(const CacheRoot& cache,
                         const std::string& name,
                         const AbiFingerprint& wanted,
                         AbiMatch policy,
                         const Vector<std::string>& configs)
{
    auto pull = StorePull {};

    for (const auto& store: cache.stores())
    {
        const auto local = pickEntry(
            findEntries(cache, name, wanted.platform), wanted, policy, configs);

        if (missingCount(local, wanted, configs) == 0)
            break;

        if (!storeProblem(store))
            pullForPick(cache, store, name, wanted, policy, configs, pull);
    }

    return pull;
}

Transfer pushToStore(const CacheRoot& cache,
                     const FilePath& store,
                     const Vector<std::string>& names)
{
    return transferEntries(allEntries(cache), CacheRoot {store}, names);
}

Transfer pullFromStore(const CacheRoot& cache,
                       const FilePath& store,
                       const Vector<std::string>& names)
{
    auto transfer = Transfer {};

    if (const auto problem = storeProblem(store))
    {
        transfer.problems.add(store.str() + " " + *problem);
        return transfer;
    }

    return transferEntries(allEntriesIn(store / "bin"), cache, names);
}

std::string transferText(const Transfer& transfer,
                         std::string_view verb,
                         std::string_view direction,
                         const FilePath& where)
{
    auto text = std::string {};

    for (const auto& problem: transfer.problems)
        text += problem + "\n";

    text += std::string {verb} + " " + std::to_string(transfer.configs)
            + " configs (" + humanSize(static_cast<std::uint64_t>(transfer.bytes))
            + ") into " + std::to_string(transfer.entries) + " entries "
            + std::string {direction} + " " + where.str() + "; "
            + std::to_string(transfer.alreadyThere) + " already there";

    if (!transfer.problems.empty())
        text += "; " + std::to_string(transfer.problems.size())
                + (transfer.problems.size() == 1 ? " problem" : " problems");

    return text;
}
} // namespace epm
