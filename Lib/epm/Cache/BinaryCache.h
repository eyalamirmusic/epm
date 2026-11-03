#pragma once

#include "CacheRoot.h"
#include "SourceCache.h"

#include <epm/Build/ChildBuild.h>
#include <epm/Spec/Env.h>
#include <epm/Spec/Request.h>

#include <map>
#include <optional>
#include <string>

namespace epm
{
// Everything that goes into a binary key, written beside the entry as
// manifest.json.
//
// The canonical form that is hashed is Miro's compact output of this struct,
// `Miro::toJSONString(manifest)`: Miro objects are std::maps, so keys come out
// sorted (options included) with no whitespace, whatever order the request
// listed them in.
struct BuildManifest
{
    // 2: the Merkle hash of the sources replaced sourceId (url + commit).
    int schema = 2;
    std::string name;
    std::string sourceHash;
    std::string cxxCompiler;
    std::string cxxCompilerId;
    std::string cxxCompilerVersion;
    std::string systemName;
    std::string systemProcessor;
    std::string osxArchitectures;
    std::string osxDeploymentTarget;
    std::string cxxStandard;
    std::map<std::string, std::string> options;
    EA::Vector<std::string> configs {"Debug", "Release"};

    MIRO_REFLECT(schema,
                 name,
                 sourceHash,
                 cxxCompiler,
                 cxxCompilerId,
                 cxxCompilerVersion,
                 systemName,
                 systemProcessor,
                 osxArchitectures,
                 osxDeploymentTarget,
                 cxxStandard,
                 options,
                 configs)
};

BuildManifest makeManifest(const Request& request,
                           const Env& env,
                           const std::string& sourceHash);

std::string canonicalManifest(const BuildManifest& manifest);

// sha256 of the canonical manifest JSON, full hex; directories use the first 12.
std::string binaryKey(const BuildManifest& manifest);

// bin/<name>/<key12>.json, written last: the entry exists once this does.
struct BinaryMarker
{
    std::string key;
    std::string commit;
    std::string builtAt;

    // The Merkle hash of every package a child build added through the lock,
    // by name. Empty when the child's CPM fetched them itself.
    std::map<std::string, std::string> dependencies;

    MIRO_REFLECT(key, commit, builtAt, dependencies)
};

struct BinaryEntry
{
    eacp::FilePath prefix;
    eacp::FilePath targetsFile;
};

// The entry at bin/<name>/<key12>, only once its completeness marker exists
// and while every dependency it was built with is still locked at the hash it
// recorded.
std::optional<BinaryEntry> findBinary(const CacheRoot& cache,
                                      const std::string& name,
                                      const std::string& key,
                                      const eacp::FilePath& lock);

struct BinaryResult
{
    bool ok = false;
    BinaryEntry entry;
    std::string error;
    std::string details;
};

// Builds Debug and Release of the snapshot into bin/<name>/<key12> and
// harvests them, under the package lock. Returns the existing entry when
// another process finished it first.
BinaryResult buildBinary(const CacheRoot& cache,
                         const Request& request,
                         const Env& env,
                         const SourceSnapshot& snapshot,
                         const LockForChildBuilds& childLock = {});
} // namespace epm
