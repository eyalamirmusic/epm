#pragma once

#include <eacp/Core/Utils/Containers.h>
#include <eacp/Core/Utils/FilePath.h>

#include <optional>
#include <string>
#include <string_view>

namespace epm
{
// The target names in an export file's `.names` companion, one per line.
eacp::Vector<std::string> parseTargetNames(std::string_view namesText);

// Every distinct path in the export file that lies under `buildDirectory`,
// relative to it, in order of first appearance. A path ends at the quote,
// list separator or generator-expression bracket that closes it.
eacp::Vector<std::string> buildTreePaths(std::string_view exportFileText,
                                         std::string_view buildDirectory);

// The export file with every `buildDirectory` prefix replaced by `replacement`.
std::string relocateBuildTree(std::string_view exportFileText,
                              std::string_view buildDirectory,
                              std::string_view replacement);

// The export file with each whole path `from` replaced by `to`; a longer path
// that merely starts with `from` is left alone.
std::string replacePath(std::string_view exportFileText,
                        std::string_view from,
                        std::string_view to);

// True when the export file still points into the child build tree.
bool referencesBuildTree(std::string_view exportFileText,
                         std::string_view buildDirectory);

bool mentionsThreads(std::string_view exportFileText);

// The buildTreePaths that appear in INTERFACE_SOURCES properties: files the
// consumer compiles itself (ResEmbed registration .cpp files).
eacp::Vector<std::string> interfaceSourcePaths(std::string_view exportFileText,
                                               std::string_view buildDirectory);

// Where, relative to the prefix, an INTERFACE_SOURCES file with `contents`
// is kept: gen/shared/<sha256(contents)[:12]>/<relative>. Content-addressed,
// so every config that generated the same file names the same path. Xcode
// rejects a target whose sources vary by configuration, and a config added to
// the entry later must not change a path an earlier config already uses.
std::string sharedSourcePath(std::string_view contents, std::string_view relative);

// The epm-targets.cmake facade over the export-<Config>.cmake of every
// config in `configs`; each target links the per-config one that
// configSelector picks.
std::string targetsFileText(const eacp::Vector<std::string>& targetNames,
                            bool needsThreads,
                            const eacp::Vector<std::string>& configs);

// Rewrites <prefix>/epm-targets.cmake for `configs`, reading each config's
// export file and .names. Returns the error when one is unreadable.
std::optional<std::string>
    writeTargetsFile(const eacp::FilePath& prefix,
                     const eacp::Vector<std::string>& configs);

struct HarvestResult
{
    bool ok = false;
    eacp::Vector<std::string> targetNames;
    bool needsThreads = false;

    // What was copied into <prefix>/gen/<config>, relative to it.
    eacp::Vector<std::string> relocatedPaths;

    // What went to <prefix>/gen/shared, relative to the prefix.
    eacp::Vector<std::string> sharedPaths;

    // Set when the build tree had to be kept (a reference survived the rewrite).
    std::string warning;
    std::string error;
};

// After a child build of `config`: copies the INTERFACE_SOURCES files
// export-<config>.cmake takes from the build tree to their sharedSourcePath,
// relocates the rest of what it takes from there into <prefix>/gen/<config>,
// rewrites the export file to match, and deletes the build tree.
HarvestResult harvestConfig(const eacp::FilePath& buildDirectory,
                            const eacp::FilePath& prefix,
                            const std::string& config);
} // namespace epm
