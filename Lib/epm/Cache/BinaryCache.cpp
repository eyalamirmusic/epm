#include "BinaryCache.h"

#include <epm/Build/Configs.h>
#include <epm/Build/Harvest.h>
#include <epm/Spec/JsonFile.h>
#include <epm/Util/Timestamp.h>

#include <eacp/Core/Utils/Files.h>

#include <tuple>

namespace epm
{
using namespace eacp;

namespace
{
constexpr auto manifestFileName = "manifest.json";

FilePath markerFor(const FilePath& prefix, const std::string& config)
{
    return prefix / (config + ".json");
}

FilePath companionOf(const FilePath& exportFile, std::string_view extension)
{
    return exportFile.sibling(exportFile.fileName() + std::string {extension});
}

Vector<FilePath> subdirectories(const FilePath& directory)
{
    auto directories = Vector<FilePath> {};

    for (const auto& entry: Files::listDirectory(directory))
        if (entry.kind == Files::EntryKind::directory)
            directories.add(entry.path);

    return directories;
}
} // namespace

FilePath BinaryEntry::targetsFile() const
{
    return prefix / "epm-targets.cmake";
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
    const auto manifest = loadJsonFile<EntryManifest>(prefix / manifestFileName);

    if (!manifest || manifest->schema != EntryManifest {}.schema)
        return std::nullopt;

    auto entry =
        BinaryEntry {prefix, manifest->fingerprint, {}, manifest->createdAt};
    auto options = Files::DirectoryOptions {};
    options.pattern = "*.json";

    for (const auto& file: Files::listFiles(prefix, options))
    {
        if (file.fileName() == manifestFileName)
            continue;

        const auto marker = loadJsonFile<ConfigMarker>(file);

        if (marker && marker->config + ".json" == file.fileName())
            entry.configs.add(marker->config);
    }

    entry.configs = canonicalConfigs(entry.configs);
    return entry;
}

Vector<BinaryEntry> findEntries(const CacheRoot& cache,
                                const std::string& name,
                                const std::string& platform)
{
    auto entries = Vector<BinaryEntry> {};

    for (const auto& prefix: subdirectories(cache.bin() / name / platform))
        if (auto entry = readEntry(prefix))
            if (entry->fingerprint.platform == platform)
                entries.add(std::move(*entry));

    return entries;
}

Vector<BinaryEntry> allEntries(const CacheRoot& cache)
{
    auto entries = Vector<BinaryEntry> {};

    for (const auto& nameDirectory: subdirectories(cache.bin()))
        for (const auto& platformDirectory: subdirectories(nameDirectory))
            entries.addFrom(findEntries(
                cache, nameDirectory.fileName(), platformDirectory.fileName()));

    return entries;
}

std::optional<BinaryEntry> pickEntry(const Vector<BinaryEntry>& entries,
                                     const AbiFingerprint& wanted,
                                     AbiMatch policy,
                                     const Vector<std::string>& configs)
{
    const BinaryEntry* best = nullptr;
    auto bestRank = std::tuple<bool, bool, bool, int, std::string> {};

    for (const auto& entry: entries)
    {
        if (!abiMatches(entry.fingerprint, wanted, policy))
            continue;

        // An entry without a config is what a failed build leaves; any built
        // one outranks it, even exact.
        const auto missing = missingConfigs(entry.configs, configs).size();
        const auto rank =
            std::tuple {!entry.configs.empty(),
                        abiMatches(entry.fingerprint, wanted, AbiMatch::strict),
                        missing == 0,
                        configs.size() - missing,
                        entry.createdAt};

        if (best == nullptr || rank > bestRank)
        {
            best = &entry;
            bestRank = rank;
        }
    }

    if (best == nullptr)
        return std::nullopt;

    return *best;
}

std::optional<BinaryEntry> createEntry(const CacheRoot& cache,
                                       const std::string& name,
                                       const AbiFingerprint& fingerprint,
                                       const std::string& sourceId)
{
    const auto prefix = entryPrefix(cache, name, fingerprint);

    if (!Files::removeAll(prefix))
        return std::nullopt;

    const auto manifest = EntryManifest {2, fingerprint, sourceId, utcTimestamp()};

    if (!saveJsonFile(prefix / manifestFileName, manifest))
        return std::nullopt;

    return BinaryEntry {prefix, fingerprint, {}, manifest.createdAt};
}

FilePath exportFileFor(const FilePath& prefix, const std::string& config)
{
    return prefix / ("export-" + config + ".cmake");
}

bool hasExport(const FilePath& prefix, const std::string& config)
{
    const auto exportFile = exportFileFor(prefix, config);
    return Files::isFile(exportFile)
           && Files::isFile(companionOf(exportFile, ".names"));
}

bool clearConfig(const FilePath& prefix, const std::string& config)
{
    const auto exportFile = exportFileFor(prefix, config);

    return Files::removeAll(markerFor(prefix, config))
           && Files::removeAll(prefix / "lib" / config)
           && Files::removeAll(prefix / "gen" / config)
           && Files::removeAll(exportFile)
           && Files::removeAll(companionOf(exportFile, ".names"))
           && Files::removeAll(companionOf(exportFile, ".aliases"));
}

std::optional<std::string> addConfigToEntry(const FilePath& prefix,
                                            const std::string& config,
                                            const Env& env)
{
    const auto marker = ConfigMarker {
        config, utcTimestamp(), env.cxxCompiler, env.cxxCompilerVersion};

    if (!saveJsonFile(markerFor(prefix, config), marker))
        return "cannot write " + markerFor(prefix, config).str();

    const auto entry = readEntry(prefix);

    if (!entry)
        return "cannot read the entry " + prefix.str();

    return writeTargetsFile(prefix, entry->configs);
}
} // namespace epm
