#pragma once

#include "CacheRoot.h"

#include <epm/Spec/Env.h>
#include <epm/Spec/Request.h>

#include <map>
#include <optional>
#include <string>

namespace epm
{
// Everything that goes into a binary key. Serialised canonically, hashed, and
// written beside the entry as manifest.json.
struct BuildManifest
{
    int schema = 1;
    std::string name;
    std::string sourceId;
    std::string cxxCompiler;
    std::string cxxCompilerId;
    std::string cxxCompilerVersion;
    std::string systemName;
    std::string systemProcessor;
    std::string osxArchitectures;
    std::string osxDeploymentTarget;
    std::string cxxStandard;
    std::map<std::string, std::string> options;
    eacp::Vector<std::string> configs {"Debug", "Release"};

    MIRO_REFLECT(schema,
                 name,
                 sourceId,
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
                           const std::string& sourceId);

// sha256 of the canonical manifest JSON, full hex; directories use the first 12.
std::string binaryKey(const BuildManifest& manifest);

struct BinaryEntry
{
    eacp::FilePath prefix;
    eacp::FilePath targetsFile;
};

// The entry at bin/<name>/<key12>, only once its completeness marker exists.
std::optional<BinaryEntry> findBinary(const CacheRoot& cache,
                                      const std::string& name,
                                      const std::string& key);
} // namespace epm
