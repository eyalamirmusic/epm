#include "BinaryCache.h"
#include "PackageLock.h"

#include <epm/Build/ChildBuild.h>
#include <epm/Build/Harvest.h>
#include <epm/Lock/LockFile.h>
#include <epm/Lock/Session.h>
#include <epm/Spec/JsonFile.h>
#include <epm/Util/Command.h>
#include <epm/Util/Log.h>
#include <epm/Util/Sha256.h>
#include <epm/Util/Text.h>
#include <epm/Util/Timestamp.h>

#include <eacp/Core/Utils/Console.h>
#include <eacp/Core/Utils/Files.h>

namespace epm
{
using namespace eacp;

namespace
{
std::string shortKey(const std::string& key)
{
    return key.substr(0, 12);
}

FilePath prefixFor(const CacheRoot& cache,
                   const std::string& name,
                   const std::string& key)
{
    return cache.bin() / name / shortKey(key);
}

FilePath markerFor(const CacheRoot& cache,
                   const std::string& name,
                   const std::string& key)
{
    return cache.bin() / name / (shortKey(key) + ".json");
}

bool isBuiltWithLockedDependencies(const BinaryMarker& marker, const LockFile& lock)
{
    for (const auto& [name, hash]: marker.dependencies)
    {
        auto locked = lock.findPackage(name);

        if (!locked || locked->hash != hash)
            return false;
    }

    return true;
}

// What the child build's epm.cmake added, from its session.
void collectChildDependencies(const FilePath& buildDirectory,
                              std::map<std::string, std::string>& dependencies)
{
    for (const auto& package: readSession(buildDirectory).packages)
    {
        if (!package.hash.empty())
            dependencies[package.name] = package.hash;
    }
}

BinaryResult failure(std::string error, std::string details = {})
{
    auto result = BinaryResult {};
    result.error = std::move(error);
    result.details = std::move(details);
    return result;
}
} // namespace

BuildManifest makeManifest(const Request& request,
                           const Env& env,
                           const std::string& sourceHash)
{
    auto manifest = BuildManifest {};
    manifest.name = request.name;
    manifest.sourceHash = sourceHash;
    manifest.cxxCompiler = env.cxxCompiler;
    manifest.cxxCompilerId = env.cxxCompilerId;
    manifest.cxxCompilerVersion = env.cxxCompilerVersion;
    manifest.systemName = env.systemName;
    manifest.systemProcessor = env.systemProcessor;
    manifest.osxArchitectures = env.osxArchitectures;
    manifest.osxDeploymentTarget = env.osxDeploymentTarget;
    manifest.cxxStandard = env.cxxStandard;
    manifest.options = request.options;
    return manifest;
}

std::string canonicalManifest(const BuildManifest& manifest)
{
    return Miro::toJSONString(manifest);
}

std::string binaryKey(const BuildManifest& manifest)
{
    return computeSha256Hex(canonicalManifest(manifest));
}

std::optional<BinaryEntry> findBinary(const CacheRoot& cache,
                                      const std::string& name,
                                      const std::string& key,
                                      const FilePath& lock)
{
    const auto marker = loadJsonFile<BinaryMarker>(markerFor(cache, name, key));

    if (!marker)
        return std::nullopt;

    if (!lock.empty() && !isBuiltWithLockedDependencies(*marker, readLockFile(lock)))
        return std::nullopt;

    const auto prefix = prefixFor(cache, name, key);
    return BinaryEntry {prefix, prefix / "epm-targets.cmake"};
}

BinaryResult buildBinary(const CacheRoot& cache,
                         const Request& request,
                         const Env& env,
                         const SourceSnapshot& snapshot,
                         const LockForChildBuilds& childLock)
{
    const auto manifest = makeManifest(request, env, snapshot.hash);
    const auto key = binaryKey(manifest);

    auto lock = PackageLock {cache, request.name, packageLockTimeout};

    if (!lock.isLocked())
        return failure("timed out waiting for another epm process using "
                       + request.name);

    if (auto existing = findBinary(cache, request.name, key, childLock.lock))
        return {true, *existing, {}, {}};

    const auto prefix = prefixFor(cache, request.name, key);
    const auto buildRoot = cache.build() / request.name / shortKey(key);

    // Left over from an interrupted build: never marked, so never used.
    if (!Files::removeAll(prefix) || !Files::removeAll(buildRoot))
        return failure("cannot clear " + prefix.str() + " or " + buildRoot.str());

    const auto generator = chooseGenerator(env, findExecutable("ninja"));
    auto targetNames = Vector<std::string> {};
    auto relocatedPaths = Vector<std::string> {};
    auto needsThreads = false;
    auto keptBuildTree = false;
    auto dependencies = std::map<std::string, std::string> {};

    Log::step("epm: building " + request.name + " into " + prefix.str()
              + " with generator '" + generator.name + "'");

    for (const auto& config: manifest.configs)
    {
        auto build = ChildBuild {};
        build.request = request;
        build.env = env;
        build.generator = generator;
        build.sourceDirectory = snapshot.directory;
        build.buildDirectory = buildRoot / config;
        build.prefix = prefix;
        build.cpmCache = cache.cpm();
        build.cacheRoot = cache.path();
        build.childLock = childLock;
        build.config = config;

        const auto built = runChildBuild(build);

        if (!built.ok)
            return failure(built.error, built.details);

        collectChildDependencies(build.buildDirectory, dependencies);

        const auto harvest = harvestConfig(build.buildDirectory, prefix, config);

        if (!harvest.ok)
            return failure("harvesting " + request.name + " (" + config
                           + "): " + harvest.error);

        if (!harvest.warning.empty())
        {
            Console::printError("epm: warning: " + harvest.warning);
            keptBuildTree = true;
        }

        for (const auto& name: harvest.targetNames)
            targetNames.addIfNotThere(name);

        for (const auto& path: harvest.relocatedPaths)
            relocatedPaths.addIfNotThere(path);

        needsThreads = needsThreads || harvest.needsThreads;
    }

    if (auto error = shareIdenticalFiles(prefix, manifest.configs, relocatedPaths))
        return failure("harvesting " + request.name + ": " + *error);

    if (!keptBuildTree)
        Files::removeAll(buildRoot);

    const auto entry = BinaryEntry {prefix, prefix / "epm-targets.cmake"};

    if (!writeTextFile(entry.targetsFile,
                       targetsFileText(targetNames, needsThreads)))
        return failure("cannot write " + entry.targetsFile.str());

    if (!saveJsonFile(prefix / "manifest.json", manifest))
        return failure("cannot write " + (prefix / "manifest.json").str());

    const auto marker =
        BinaryMarker {key, snapshot.commit, utcTimestamp(), dependencies};

    if (!saveJsonFile(markerFor(cache, request.name, key), marker))
        return failure("cannot write the marker for " + prefix.str());

    return {true, entry, {}, {}};
}
} // namespace epm
