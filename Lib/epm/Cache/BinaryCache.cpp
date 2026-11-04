#include "BinaryCache.h"
#include <epm/Cache/CrossProcessLock.h>

#include <epm/Build/ChildBuild.h>
#include <epm/Build/Configs.h>
#include <epm/Build/Harvest.h>
#include <epm/Lock/LockFile.h>
#include <epm/Lock/Session.h>
#include <epm/Spec/JsonFile.h>
#include <epm/Util/Command.h>
#include <epm/Util/Log.h>
#include <epm/Util/Text.h>
#include <epm/Util/Timestamp.h>

#include <eacp/Core/Utils/Console.h>
#include <eacp/Core/Utils/Strings.h>
#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/Time.h>

#include <tuple>

namespace epm
{
using namespace eacp;

namespace
{
constexpr auto manifestFileName = "manifest.json";
constexpr auto jsonIndent = 2;

std::string createTimestamp()
{
    return Time::formatIso8601(Time::sinceEpoch());
}

FilePath markerFor(const FilePath& prefix, const std::string& config)
{
    return prefix / (config + ".json");
}

EA::Vector<FilePath> subdirectories(const FilePath& directory)
{
    auto directories = EA::Vector<FilePath> {};

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
        BinaryEntry {prefix, manifest->fingerprint, {}, {}, manifest->createdAt};
    auto options = Files::DirectoryOptions {};
    options.pattern = "*.json";

    for (const auto& file: Files::listFiles(prefix, options))
    {
        if (file.fileName() == manifestFileName)
            continue;

        const auto marker = loadJsonFile<ConfigMarker>(file);

        if (marker && marker->config + ".json" == file.fileName())
        {
            entry.configs.add(marker->config);
            entry.lockedDependencies[marker->config] = marker->lockedDependencies;
        }
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
                                       const std::string& sourceHash)
{
    const auto prefix = entryPrefix(cache, name, fingerprint);

    if (!Files::removeAll(prefix))
        return std::nullopt;

    const auto manifest = EntryManifest {
        EntryManifest {}.schema, fingerprint, sourceHash, createTimestamp()};

    Files::writeFileAtomically(prefix / manifestFileName,
                               Miro::toJSONString(manifest, jsonIndent) + "\n");

    return BinaryEntry {prefix, fingerprint, {}, {}, manifest.createdAt};
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

bool isBuiltWithLockedDependencies(const std::map<std::string, std::string>& built,
                                   const LockFile& lock)
{
    for (const auto& [name, hash]: built)
    {
        auto locked = lock.findPackage(name);

        if (!locked || locked->hash != hash)
            return false;
    }

    return true;
}

// What the child build's epm.cmake took from the lock, from its session.
std::map<std::string, std::string>
    readLockedDependencies(const FilePath& buildDirectory)
{
    auto dependencies = std::map<std::string, std::string> {};

    for (const auto& package: readSession(buildDirectory).packages)
    {
        if (!package.hash.empty())
            dependencies[package.name] = package.hash;
    }

    return dependencies;
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

EA::Vector<BinaryEntry> findEntries(const CacheRoot& cache,
                                    const std::string& name,
                                    const std::string& platform)
{
    auto entries = EA::Vector<BinaryEntry> {};

    for (const auto& prefix: subdirectories(cache.bin() / name / platform))
        if (auto entry = loadEntry(prefix))
            if (entry->fingerprint.platform == platform)
                entries.add(std::move(*entry));

    return entries;
}

BinaryEntry withConfigsMatchingTheLock(BinaryEntry entry, const FilePath& lock)
{
    const auto lockFile = readLockFile(lock);
    auto configs = EA::Vector<std::string> {};

    for (const auto& config: entry.configs)
    {
        if (isBuiltWithLockedDependencies(entry.lockedDependencies[config],
                                          lockFile))
            configs.add(config);
    }

    entry.configs = configs;
    return entry;
}

EA::Vector<BinaryEntry> allEntries(const CacheRoot& cache)
{
    auto entries = EA::Vector<BinaryEntry> {};

    for (const auto& nameDirectory: subdirectories(cache.bin()))
        for (const auto& platformDirectory: subdirectories(nameDirectory))
            entries.addFrom(findEntries(
                cache, nameDirectory.fileName(), platformDirectory.fileName()));

    return entries;
}

std::optional<BinaryEntry> pickEntry(const EA::Vector<BinaryEntry>& entries,
                                     const AbiFingerprint& wanted,
                                     AbiMatch policy,
                                     const EA::Vector<std::string>& configs)
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
                         const SourceSnapshot& snapshot,
                         const LockForChildBuilds& childLock,
                         const std::string& dependencyGraph)
{
    const auto wanted = makeFingerprint(request, env, snapshot.hash);
    const auto policy =
        parseAbiMatch(request.abiMatch).value_or(AbiMatch::compatible);
    const auto configs = buildConfigsFor(request.configs);

    auto lock = CrossProcessLock {cache, request.name, crossProcessLockTimeout};

    if (!lock.isLocked())
        return failure("timed out waiting for another epm process using "
                       + request.name);

    auto entries = EA::Vector<BinaryEntry> {};

    for (const auto& found: findEntries(cache, request.name, wanted.platform))
        entries.add(withConfigsMatchingTheLock(found, childLock.lock));

    auto entry = pickEntry(entries, wanted, policy, configs);

    if (!entry)
        entry = createEntry(cache, request.name, wanted, snapshot.hash);

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

    Log::step("epm: building " + request.name + " ("
              + eacp::Strings::join(missing, ", ") + ") into " + prefix.str()
              + " with generator '" + generator.name + "'");

    for (const auto& config: missing)
    {
        auto build = ChildBuild {};
        build.request = request;
        build.env = env;
        build.generator = generator;
        build.sourceDirectory = snapshot.directory;
        build.buildDirectory = buildRoot / config;
        build.prefix = prefix;
        build.config = config;
        build.dependencyGraph = dependencyGraph;
        build.epmExecutable = Files::executablePath();
        build.cacheRoot = cache.path();
        build.childLock = childLock;

        if (!clearConfig(prefix, config) || !Files::removeAll(build.buildDirectory))
            return failure("cannot clear the leftovers of " + config + " in "
                           + prefix.str());

        Log::progress("epm: building " + request.name
                      + " from source into the cache (" + config + ", "
                      + wanted.platform + "), this can take a while");

        const auto stopwatch = Stopwatch {};
        const auto built = runChildBuild(build);

        if (!built.ok)
            return failure(built.error, built.details);

        Log::progress("epm: built " + request.name + " (" + config + ") in "
                      + formatDuration(stopwatch.elapsedSeconds()));

        const auto harvest = harvestConfig(build.buildDirectory, prefix, config);

        if (!harvest.ok)
            return failure("harvesting " + request.name + " (" + config
                           + "): " + harvest.error);

        if (!harvest.warning.empty())
        {
            Console::printError("epm: warning: " + harvest.warning);
            keptBuildTree = true;
        }

        const auto marker =
            ConfigMarker {config,
                          createTimestamp(),
                          env.cxxCompiler,
                          env.cxxCompilerVersion,
                          readLockedDependencies(build.buildDirectory)};

        Files::writeFileAtomically(markerFor(prefix, config),
                                   Miro::toJSONString(marker, jsonIndent) + "\n");

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
