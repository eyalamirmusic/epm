#include "BinaryCache.h"
#include "PackageLock.h"

#include <epm/Build/ChildBuild.h>
#include <epm/Build/Harvest.h>
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

BinaryResult failure(std::string error, std::string details = {})
{
    auto result = BinaryResult {};
    result.error = std::move(error);
    result.details = std::move(details);
    return result;
}
} // namespace

BuildManifest
    makeManifest(const Request& request, const Env& env, const std::string& sourceId)
{
    auto manifest = BuildManifest {};
    manifest.name = request.name;
    manifest.sourceId = sourceId;
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
    return sha256Hex(canonicalManifest(manifest));
}

std::optional<BinaryEntry> findBinary(const CacheRoot& cache,
                                      const std::string& name,
                                      const std::string& key)
{
    if (!Files::isFile(markerFor(cache, name, key)))
        return std::nullopt;

    const auto prefix = prefixFor(cache, name, key);
    return BinaryEntry {prefix, prefix / "epm-targets.cmake"};
}

BinaryResult buildBinary(const CacheRoot& cache,
                         const Request& request,
                         const Env& env,
                         const SourceSnapshot& snapshot)
{
    const auto manifest = makeManifest(request, env, snapshot.sourceId);
    const auto key = binaryKey(manifest);

    auto lock = PackageLock {cache, request.name, packageLockTimeout};

    if (!lock.isLocked())
        return failure("timed out waiting for another epm process using "
                       + request.name);

    if (auto existing = findBinary(cache, request.name, key))
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
        build.config = config;

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

    const auto marker = BinaryMarker {key, snapshot.commit, utcTimestamp()};

    if (!saveJsonFile(markerFor(cache, request.name, key), marker))
        return failure("cannot write the marker for " + prefix.str());

    return {true, entry, {}, {}};
}
} // namespace epm
