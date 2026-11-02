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

// The epm-targets.cmake facade over export-Debug.cmake and export-Release.cmake.
std::string targetsFileText(const eacp::Vector<std::string>& targetNames,
                            bool needsThreads);

struct HarvestResult
{
    bool ok = false;
    eacp::Vector<std::string> targetNames;
    bool needsThreads = false;

    // What was copied into <prefix>/gen/<config>, relative to it.
    eacp::Vector<std::string> relocatedPaths;

    // Set when the build tree had to be kept (a reference survived the rewrite).
    std::string warning;
    std::string error;
};

// After a child build of `config`: relocates what export-<config>.cmake still
// takes from the build tree into <prefix>/gen/<config>, rewrites the export
// file to match, and deletes the build tree.
HarvestResult harvestConfig(const eacp::FilePath& buildDirectory,
                            const eacp::FilePath& prefix,
                            const std::string& config);

// After every config is harvested: each relocated file that came out
// byte-identical in all of them moves to <prefix>/gen/common, and every export
// file points there. Xcode rejects a target whose sources vary by config, and
// the registration .cpp files in INTERFACE_SOURCES are otherwise one path per
// config. Returns the error when a file cannot be moved or rewritten.
std::optional<std::string>
    shareIdenticalFiles(const eacp::FilePath& prefix,
                        const eacp::Vector<std::string>& configs,
                        const eacp::Vector<std::string>& relocatedPaths);
} // namespace epm
