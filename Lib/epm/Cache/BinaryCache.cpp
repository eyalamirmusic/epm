#include "BinaryCache.h"

#include <epm/Build/Configs.h>
#include <epm/Build/Harvest.h>
#include <epm/Build/Lto.h>
#include <epm/Spec/JsonFile.h>
#include <epm/Util/Paths.h>
#include <epm/Util/ProcessInfo.h>
#include <epm/Util/Sha256.h>
#include <epm/Util/Text.h>
#include <epm/Util/Timestamp.h>

#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/StdPath.h>
#include <eacp/Core/Utils/Strings.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <tuple>

namespace epm
{
using namespace eacp;

namespace
{
constexpr auto entryManifestFileName = "manifest.json";
constexpr auto heartbeatFileName = ".epm-heartbeat";

FilePath companionOf(const FilePath& exportFile, std::string_view extension)
{
    return exportFile.sibling(exportFile.fileName() + std::string {extension});
}

std::int64_t unixSeconds()
{
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::optional<std::int64_t> integerIn(std::string_view text)
{
    auto value = std::int64_t {0};
    const auto* end = text.data() + text.size();
    const auto [stop, error] = std::from_chars(text.data(), end, value);

    if (text.empty() || error != std::errc {} || stop != end)
        return std::nullopt;

    return value;
}

// The heartbeat a staging directory's owner last wrote, else nullopt.
std::optional<std::int64_t> lastHeartbeat(const FilePath& directory)
{
    const auto file = directory / heartbeatFileName;

    if (!Files::isFile(file))
        return std::nullopt;

    return integerIn(Strings::trim(Files::readFile(file)));
}

// On this host the owner's process says it all (a reused pid only delays the
// cleanup); another host's processes cannot be seen, so its heartbeat decides.
bool isStale(const FilePath& directory, const StagingOwner& owner, std::int64_t now)
{
    if (owner.processId > 0 && owner.host == hostId())
        return owner.processId != currentProcessId()
               && !isProcessAlive(owner.processId);

    const auto heartbeat = lastHeartbeat(directory).value_or(owner.createdAt);
    return now - std::max(heartbeat, owner.createdAt) > staleStagingSeconds;
}

bool hasMarkerFor(const FilePath& unit, const std::string& config)
{
    const auto marker =
        loadJsonFile<ConfigMarker>(unit / std::string {configMarkerName});
    return marker && marker->config == config;
}

std::optional<UnitFile>
    unitFile(const FilePath& file, const FilePath& base, std::string& lto)
{
    if (!Files::isFile(file))
        return std::nullopt;

    const auto bytes = Files::readFile(file);

    if (lto.empty())
        lto = ltoKindOf(bytes);

    return UnitFile {file.relativeTo(base).str(),
                     static_cast<std::int64_t>(bytes.size()),
                     sha256Hex(bytes)};
}

// The unit's own bookkeeping, which ConfigFiles does not list.
bool isBookkeeping(const FilePath& file, const FilePath& unit)
{
    return file == unit / std::string {configMarkerName}
           || file == unit / std::string {configFilesName};
}
} // namespace

bool configUsableBy(const ConfigMarker& marker, const AbiFingerprint& wanted)
{
    return marker.lto.empty()
           || (marker.compilerId == wanted.compilerId
               && marker.compilerVersion == wanted.compilerVersion);
}

LoadedConfigFiles loadConfigFiles(const FilePath& unit, const ConfigMarker& marker)
{
    const auto file = unit / std::string {configFilesName};

    if (!Files::isFile(file))
        return {std::nullopt, std::string {configFilesName} + " is missing"};

    const auto text = Files::readFile(file);

    if (sha256Hex(text) != marker.filesSha256)
        return {std::nullopt,
                std::string {configFilesName}
                    + ": sha256 differs from the one the marker recorded"};

    if (auto files = parseJsonText<ConfigFiles>(text))
        return {std::move(files), {}};

    return {std::nullopt, std::string {configFilesName} + " is not readable"};
}

bool isCanonicalConfigName(const std::string& name)
{
    return !name.empty() && canonicalConfigs({name}) == Vector<std::string> {name};
}

FilePath BinaryEntry::targetsFile() const
{
    return prefix / "epm-targets.cmake";
}

const ConfigMarker* BinaryEntry::markerOf(const std::string& config) const
{
    return markers.findIf([&](const ConfigMarker& marker)
                          { return marker.config == config; });
}

Vector<std::string> BinaryEntry::usableConfigs(const AbiFingerprint& wanted) const
{
    auto usable = Vector<std::string> {};

    for (const auto& marker: markers)
        if (configUsableBy(marker, wanted))
            usable.add(marker.config);

    return canonicalConfigs(usable);
}

FilePath entryPrefix(const CacheRoot& cache,
                     const std::string& name,
                     const AbiFingerprint& fingerprint)
{
    return cache.bin() / name / fingerprint.platform
           / fingerprintId(fingerprint).substr(0, 12);
}

std::optional<BinaryEntry> readEntry(const FilePath& prefix)
{
    const auto manifest =
        loadJsonFile<EntryManifest>(prefix / entryManifestFileName);

    if (!manifest || manifest->schema != entrySchema)
        return std::nullopt;

    auto found = Vector<ConfigMarker> {};

    // configs/release beside configs/Release would otherwise be canonicalised
    // into the same config twice, or into one no unit is named after.
    for (const auto& unit: subdirectories(prefix / "configs"))
    {
        const auto name = unit.fileName();

        if (!isCanonicalConfigName(name))
            continue;

        auto marker =
            loadJsonFile<ConfigMarker>(unit / std::string {configMarkerName});

        if (marker && marker->config == name)
            found.add(std::move(*marker));
    }

    auto entry =
        BinaryEntry {prefix, manifest->fingerprint, {}, manifest->createdAt, {}};

    for (const auto& marker: found)
        entry.configs.add(marker.config);

    entry.configs = canonicalConfigs(entry.configs);

    for (const auto& config: entry.configs)
        if (const auto* marker =
                found.findIf([&](const ConfigMarker& candidate)
                             { return candidate.config == config; }))
            entry.markers.add(*marker);

    return entry;
}

Vector<BinaryEntry> findEntries(const CacheRoot& cache,
                                const std::string& name,
                                const std::string& platform)
{
    return findEntriesIn(cache.bin(), name, platform);
}

Vector<BinaryEntry> findEntriesIn(const FilePath& bin,
                                  const std::string& name,
                                  const std::string& platform)
{
    auto entries = Vector<BinaryEntry> {};

    for (const auto& prefix: subdirectories(bin / name / platform))
        if (auto entry = readEntry(prefix))
            if (entry->fingerprint.platform == platform)
                entries.add(std::move(*entry));

    return entries;
}

Vector<BinaryEntry> allEntries(const CacheRoot& cache)
{
    return allEntriesIn(cache.bin());
}

Vector<BinaryEntry> allEntriesIn(const FilePath& bin)
{
    auto entries = Vector<BinaryEntry> {};

    for (const auto& nameDirectory: subdirectories(bin))
        for (const auto& platformDirectory: subdirectories(nameDirectory))
            entries.addFrom(findEntriesIn(
                bin, nameDirectory.fileName(), platformDirectory.fileName()));

    return entries;
}

std::string entryName(const BinaryEntry& entry)
{
    return entry.prefix.parentDirectory().parentDirectory().fileName();
}

bool entryNamedByFingerprint(const BinaryEntry& entry)
{
    return entry.prefix.fileName() == fingerprintId(entry.fingerprint).substr(0, 12);
}

std::optional<BinaryEntry> pickEntry(const Vector<BinaryEntry>& entries,
                                     const AbiFingerprint& wanted,
                                     AbiMatch policy,
                                     const Vector<std::string>& configs)
{
    const BinaryEntry* best = nullptr;
    auto bestRank = std::tuple<bool, bool, bool, std::size_t> {};

    for (const auto& entry: entries)
    {
        if (!abiMatches(entry.fingerprint, wanted, policy))
            continue;

        const auto usable = entry.usableConfigs(wanted);
        const auto blocked = configs.findIf(
            [&](const std::string& config)
            { return entry.configs.contains(config) && !usable.contains(config); });

        // It holds a needed config this consumer cannot link, and a published
        // config is never replaced.
        if (blocked != nullptr)
            continue;

        // An entry without a config is what a failed build leaves; any built
        // one outranks it, even exact. A complete one outranks an exact one
        // that is not, which is only the better place to build into.
        const auto missing = missingConfigs(usable, configs).size();
        const auto rank =
            std::tuple {!entry.configs.empty(),
                        missing == 0,
                        abiMatches(entry.fingerprint, wanted, AbiMatch::strict),
                        configs.size() - missing};

        // Equal ranks go to the lowest fp12, never to a clock.
        if (best == nullptr || rank > bestRank
            || (rank == bestRank
                && entry.prefix.fileName() < best->prefix.fileName()))
        {
            best = &entry;
            bestRank = rank;
        }
    }

    if (best == nullptr)
        return std::nullopt;

    return *best;
}

FilePath stagingDirectory(const FilePath& entryPrefix)
{
    return entryPrefix.parentDirectory() / ".staging";
}

std::string hostId()
{
    static const auto id = sha256Hex(hostName()).substr(0, 12);
    return id;
}

TemporaryFile newStaging(const FilePath& entryPrefix)
{
    const auto directory = stagingDirectory(entryPrefix);
    Files::createDirectories(directory);
    return TemporaryFile {directory,
                          "." + std::to_string(currentProcessId()) + "-" + hostId()
                              + "." + std::to_string(unixSeconds())};
}

std::optional<StagingOwner> stagingOwner(const std::string& name)
{
    const auto last = name.rfind('.');

    if (last == std::string::npos)
        return std::nullopt;

    const auto createdAt = integerIn(std::string_view {name}.substr(last + 1));

    if (!createdAt)
        return std::nullopt;

    auto owner = StagingOwner {*createdAt, 0, {}};
    const auto previous = last == 0 ? std::string::npos : name.rfind('.', last - 1);

    if (previous == std::string::npos)
        return owner;

    const auto who =
        std::string_view {name}.substr(previous + 1, last - previous - 1);
    const auto dash = who.find('-');

    if (dash == std::string_view::npos)
        return owner;

    if (const auto processId = integerIn(who.substr(0, dash)))
    {
        owner.processId = *processId;
        owner.host = std::string {who.substr(dash + 1)};
    }

    return owner;
}

void touchStagingHeartbeat(const FilePath& directory)
{
    writeTextFile(directory / heartbeatFileName,
                  std::to_string(unixSeconds()) + "\n");
}

void removeStaleStaging(const FilePath& directory)
{
    auto options = Files::DirectoryOptions {};
    options.includeHidden = true;
    const auto now = unixSeconds();

    for (const auto& entry: Files::listDirectory(directory, options))
        if (const auto owner = stagingOwner(entry.path.fileName()))
            if (isStale(entry.path, *owner, now))
                Files::removeAll(entry.path);
}

std::optional<BinaryEntry> createEntry(const CacheRoot& cache,
                                       const std::string& name,
                                       const AbiFingerprint& fingerprint,
                                       const std::string& sourceId)
{
    const auto prefix = entryPrefix(cache, name, fingerprint);

    if (auto entry = readEntry(prefix))
        return entry;

    removeStaleStaging(stagingDirectory(prefix));

    // Checked again: another process may have created it meanwhile.
    if (Files::exists(prefix) && !readEntry(prefix))
    {
        // Moved aside first, so the path is free at once whatever deleting
        // the leftover takes; one that turns out valid, published by another
        // process meanwhile, goes back.
        const auto aside = newStaging(prefix);

        if (renameNoReplace(prefix, aside.path()) && readEntry(aside.path()))
            renameNoReplace(aside.path(), prefix);

        if (auto entry = readEntry(prefix))
            return entry;
    }

    {
        const auto skeleton = newStaging(prefix);
        const auto manifest =
            EntryManifest {entrySchema, fingerprint, sourceId, utcTimestamp()};

        if (!saveJsonFile(skeleton.path() / entryManifestFileName, manifest)
            || writeTargetsFile(skeleton.path())
            || !Files::createDirectories(skeleton.path() / "configs"))
            return std::nullopt;

        // Losing the race to another process is fine: its entry is the same.
        renameNoReplace(skeleton.path(), prefix);
    }

    removeStagingIfEmpty(prefix);
    return readEntry(prefix);
}

void removeStagingIfEmpty(const FilePath& entryPrefix)
{
    // A plain remove fails on a directory another process is staging in.
    auto notEmpty = std::error_code {};
    std::filesystem::remove(toStdPath(stagingDirectory(entryPrefix)), notEmpty);
}

FilePath exportFileFor(const FilePath& unit, const std::string& config)
{
    return unit / ("export-" + config + ".cmake");
}

bool hasExport(const FilePath& unit, const std::string& config)
{
    const auto exportFile = exportFileFor(unit, config);
    return Files::isFile(exportFile)
           && Files::isFile(companionOf(exportFile, ".names"));
}

StagedConfig describeStagedConfig(const FilePath& staged,
                                  const std::string& config,
                                  const Env& env)
{
    const auto unit = configDirectory(staged, config);
    const auto exportFile = exportFileFor(unit, config);
    const auto failure = [](std::string error)
    { return StagedConfig {std::nullopt, {}, std::move(error)}; };

    if (!hasExport(unit, config))
        return failure("no " + exportFile.str() + " (or its .names)");

    auto marker = ConfigMarker {};
    marker.config = config;
    marker.builtAt = utcTimestamp();
    marker.compilerId = env.cxxCompilerId;
    marker.compilerVersion = env.cxxCompilerVersion;
    marker.compilerPath = env.cxxCompiler;

    auto files = ConfigFiles {};
    auto options = Files::DirectoryOptions {};
    options.recursive = true;
    options.includeHidden = true;

    for (const auto& entry: Files::listDirectory(unit, options))
    {
        if (Files::isDirectory(entry.path) || isBookkeeping(entry.path, unit))
            continue;

        if (auto file = unitFile(entry.path, unit, marker.lto))
            files.files.add(std::move(*file));
        else
            return failure("cannot read " + entry.path.str());
    }

    for (const auto& path:
         pathsUnder(Files::readFile(exportFile), sharedRootVariable))
    {
        if (auto file = unitFile(staged / path, staged, marker.lto))
            files.shared.add(std::move(*file));
        else
            return failure(exportFile.fileName() + " names " + path
                           + ", which the harvest did not stage");
    }

    const auto byPath = [](const UnitFile& a, const UnitFile& b)
    { return a.path < b.path; };
    std::sort(files.files.begin(), files.files.end(), byPath);
    std::sort(files.shared.begin(), files.shared.end(), byPath);

    const auto text = jsonText(files);
    const auto filesFile = unit / std::string {configFilesName};

    if (!writeTextFile(filesFile, text))
        return failure("cannot write " + filesFile.str());

    marker.filesSha256 = sha256Hex(text);
    return {std::move(marker), std::move(files), {}};
}

Published publishConfig(const FilePath& prefix,
                        const FilePath& staged,
                        const ConfigMarker& marker)
{
    const auto& config = marker.config;
    const auto unit = configDirectory(staged, config);
    const auto target = configDirectory(prefix, config);
    const auto failure = [](std::string error)
    { return Published {PublishResult::failed, std::move(error)}; };

    if (hasMarkerFor(target, config))
        return {PublishResult::alreadyThere, {}};

    const auto loaded = loadConfigFiles(unit, marker);

    if (!loaded.files)
        return failure("the staged " + unit.str() + ": " + loaded.error);

    for (const auto& file: loaded.files->shared)
    {
        const auto to = prefix / file.path;

        if (!Files::isFile(to) && !renameNoReplace(staged / file.path, to)
            && !Files::isFile(to))
            return failure("cannot publish " + to.str());
    }

    if (!saveJsonFile(unit / std::string {configMarkerName}, marker))
        return failure("cannot write the marker of " + unit.str());

    if (renameNoReplace(unit, target))
        return {PublishResult::published, {}};

    if (hasMarkerFor(target, config))
        return {PublishResult::alreadyThere, {}};

    return failure("cannot rename " + unit.str() + " to " + target.str());
}
} // namespace epm
