#include "BinaryCache.h"
#include "PackageLock.h"

#include <epm/Build/ChildBuild.h>
#include <epm/Build/Configs.h>
#include <epm/Build/Harvest.h>
#include <epm/Spec/JsonFile.h>
#include <epm/Util/Command.h>
#include <epm/Util/Log.h>
#include <epm/Util/Text.h>
#include <epm/Util/Timestamp.h>

#include <eacp/Core/Utils/Console.h>
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

Vector<FilePath> subdirectories(const FilePath& directory)
{
    auto directories = Vector<FilePath> {};

    for (const auto& entry: Files::listDirectory(directory))
        if (entry.kind == Files::EntryKind::directory)
            directories.add(entry.path);

    return directories;
}

std::optional<BinaryEntry> loadEntry(const FilePath& prefix)
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

// A fresh entry for `fingerprint`. Whatever is at its path has no usable
// manifest (an exact match would have been picked), so it is a leftover of an
// interrupted creation and goes.
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

// What an unmarked config may have left behind. gen/shared stays: its files
// are content-addressed and other configs point at them.
bool clearConfig(const FilePath& prefix, const std::string& config)
{
    const auto exportFile = prefix / ("export-" + config + ".cmake");
    const auto companion = [&](const std::string& extension)
    { return exportFile.sibling(exportFile.fileName() + extension); };

    return Files::removeAll(markerFor(prefix, config))
           && Files::removeAll(prefix / "lib" / config)
           && Files::removeAll(prefix / "gen" / config)
           && Files::removeAll(exportFile) && Files::removeAll(companion(".names"))
           && Files::removeAll(companion(".aliases"));
}

BinaryResult failure(std::string error, std::string details = {})
{
    auto result = BinaryResult {};
    result.error = std::move(error);
    result.details = std::move(details);
    return result;
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

Vector<BinaryEntry> findEntries(const CacheRoot& cache,
                                const std::string& name,
                                const std::string& platform)
{
    auto entries = Vector<BinaryEntry> {};

    for (const auto& prefix: subdirectories(cache.bin() / name / platform))
        if (auto entry = loadEntry(prefix))
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
    auto bestRank = std::tuple<bool, bool, int, std::string> {};

    for (const auto& entry: entries)
    {
        if (!abiMatches(entry.fingerprint, wanted, policy))
            continue;

        const auto missing = missingConfigs(entry.configs, configs).size();
        const auto rank =
            std::tuple {abiMatches(entry.fingerprint, wanted, AbiMatch::strict),
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

BinaryResult buildBinary(const CacheRoot& cache,
                         const Request& request,
                         const Env& env,
                         const SourceSnapshot& snapshot)
{
    const auto wanted = makeFingerprint(request, env, snapshot.sourceId);
    const auto policy =
        parseAbiMatch(request.abiMatch).value_or(AbiMatch::compatible);
    const auto configs = buildConfigsFor(request.configs);

    auto lock = PackageLock {cache, request.name, packageLockTimeout};

    if (!lock.isLocked())
        return failure("timed out waiting for another epm process using "
                       + request.name);

    auto entry = pickEntry(
        findEntries(cache, request.name, wanted.platform), wanted, policy, configs);

    if (!entry)
        entry = createEntry(cache, request.name, wanted, snapshot.sourceId);

    if (!entry)
        return failure("cannot create "
                       + entryPrefix(cache, request.name, wanted).str());

    const auto missing = missingConfigs(entry->configs, configs);

    if (missing.empty())
        return {true, *entry, {}, {}, {}};

    const auto prefix = entry->prefix;
    const auto buildRoot =
        cache.build() / request.name / wanted.platform / prefix.fileName();
    const auto generator = chooseGenerator(env, findExecutable("ninja"));
    auto keptBuildTree = false;

    Log::step("epm: building " + request.name + " (" + joinList(missing, ", ")
              + ") into " + prefix.str() + " with generator '" + generator.name
              + "'");

    for (const auto& config: missing)
    {
        auto build = ChildBuild {};
        build.request = request;
        build.env = env;
        build.generator = generator;
        build.sourceDirectory = snapshot.directory;
        build.buildDirectory = buildRoot / config;
        build.prefix = prefix;
        build.cpmCache = cache.cpm();
        build.config = config;

        if (!clearConfig(prefix, config) || !Files::removeAll(build.buildDirectory))
            return failure("cannot clear the leftovers of " + config + " in "
                           + prefix.str());

        const auto built = runChildBuild(build);

        if (!built.ok)
            return failure(built.error, built.details);

        const auto harvest = harvestConfig(build.buildDirectory, prefix, config);

        if (!harvest.ok)
            return failure("harvesting " + request.name + " (" + config
                           + "): " + harvest.error);

        if (!harvest.warning.empty())
        {
            Console::printError("epm: warning: " + harvest.warning);
            keptBuildTree = true;
        }

        const auto marker = ConfigMarker {
            config, utcTimestamp(), env.cxxCompiler, env.cxxCompilerVersion};

        if (!saveJsonFile(markerFor(prefix, config), marker))
            return failure("cannot write " + markerFor(prefix, config).str());

        entry->configs.add(config);
        entry->configs = canonicalConfigs(entry->configs);

        if (auto error = writeTargetsFile(prefix, entry->configs))
            return failure("generating the targets of " + request.name + ": "
                           + *error);
    }

    if (!keptBuildTree)
        Files::removeAll(buildRoot);

    return {true, *entry, missing, {}, {}};
}
} // namespace epm
