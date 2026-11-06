#pragma once

#include <eacp/Core/Utils/Containers.h>
#include <eacp/Core/Utils/FilePath.h>

#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace epm
{
// What a config's CMake files write for the config's own directory, which
// holds them, so the entry works wherever it is moved or copied.
constexpr auto entryDirectory = std::string_view {"${CMAKE_CURRENT_LIST_DIR}"};

// What an export file writes for the entry holding its config, for the
// files the configs share (gen/shared): the line sharedRootLine sets it.
constexpr auto sharedRootVariableName = std::string_view {"_epm_entry_dir"};
constexpr auto sharedRootVariable = std::string_view {"${_epm_entry_dir}"};

// The line a harvest puts first in an export file naming gen/shared: the
// entry is two levels up (configs/<Config>/), and the path is normalised so
// every config names a shared file by the same spelling, which Xcode needs.
std::string sharedRootLine();

// <entry>/configs/<config>: one config's unit, which appears in the entry by
// a single rename once complete and is never modified afterwards
// (Docs/PoC.md §3).
eacp::FilePath configDirectory(const eacp::FilePath& entry,
                               const std::string& config);

// The target names in an export file's `.names` companion, one per line.
eacp::Vector<std::string> parseTargetNames(std::string_view namesText);

// An ALIAS target the child build defined for one of its exported targets.
struct TargetAlias
{
    std::string name;
    std::string target;

    bool operator==(const TargetAlias&) const = default;
};

// The aliases in an export file's `.aliases` companion, one `<alias> <target>`
// per line; malformed lines are skipped.
eacp::Vector<TargetAlias> parseTargetAliases(std::string_view aliasesText);

// Every distinct path in the export file that is `directory` or lies under
// it, relative to it ("" for the directory itself), trailing separators
// dropped, in order of first appearance. A path starts at the start of the
// text, after a quote, `;`, `:`, `=`, `,`, `>` or whitespace, or right after
// an option that takes its path joined (-I, -isystem, -iquote, -idirafter,
// -iframework, -isysroot, -include, -imacros, -L, -F), and ends at the quote,
// list separator, generator-expression comma or bracket that closes it, or a
// line end; in a SHELL: option also at a space.
eacp::Vector<std::string> pathsUnder(std::string_view exportFileText,
                                     std::string_view directory);

// One path pathsUnder finds, with what names it: the property its line sets
// (or the file-set keyword, BASE_DIRS), empty when the line sets none, and
// whether it names an include directory: through an include property
// (INTERFACE_INCLUDE_DIRECTORIES, INTERFACE_SYSTEM_INCLUDE_DIRECTORIES,
// BASE_DIRS) or an include option (-I, -isystem, -iquote, -idirafter).
struct PathMention
{
    std::string relative;
    std::string property;
    bool namesIncludeDirectory = false;

    bool operator==(const PathMention&) const = default;
};

// Every distinct PathMention under `directory`, in order of appearance.
eacp::Vector<PathMention> pathMentionsUnder(std::string_view exportFileText,
                                            std::string_view directory);

// The export file with every path pathsUnder finds replaced by what
// `replacement` makes of its relative part.
std::string rewritePathsUnder(
    std::string_view exportFileText,
    std::string_view directory,
    const std::function<std::string(std::string_view relative)>& replacement);

// The export file with each whole path `from` replaced by `to`; a longer path
// that merely starts with `from` is left alone.
std::string replacePath(std::string_view exportFileText,
                        std::string_view from,
                        std::string_view to);

// The first of `paths` the text names, as a whole path or as a directory
// above one, where pathsUnder would find it; a longer name that merely
// starts or ends with it does not count.
std::optional<std::string>
    firstMentionedPath(std::string_view text,
                       const eacp::Vector<std::string>& paths);

bool mentionsThreads(std::string_view exportFileText);

// The pathsUnder `buildDirectory` that appear in INTERFACE_SOURCES
// properties: files the consumer compiles itself (ResEmbed registration .cpp
// files).
eacp::Vector<std::string> interfaceSourcePaths(std::string_view exportFileText,
                                               std::string_view buildDirectory);

// Whether a file inside an include directory the export file names is copied
// into the entry. Header trees are copied by exclusion, since headers need not
// have an extension (Eigen's do not): everything but what only a build reads
// (build scripts and project files, logs, dependency files), compiled output
// (objects, archives, libraries, debug databases, precompiled headers) and,
// unless `keepSources`, sources (.c, .cpp, .m, .mm, assembly, ...). Hidden
// entries and CMakeFiles directories are never walked into. `keepSources` is
// for a tree the consumer compiles from (it names a source file inside), as
// a module whose interface sources include their siblings. A directory named
// otherwise (link directories, libraries, options, a framework) is copied
// whole, but for version-control and CMakeFiles directories.
bool isCopiedIntoEntry(const eacp::FilePath& file, bool keepSources);

// The snapshot `sourceDirectory` lies in, <cache>/src/<name>/<id>, as written
// or with its symlinks resolved: a package may be built from a subdirectory
// of it (zstd's and lz4's build/cmake) and name files anywhere in it. Else
// `sourceDirectory` itself (a local checkout, an empty cache).
eacp::FilePath sourceRootOf(const eacp::FilePath& cache,
                            const eacp::FilePath& sourceDirectory);

// Where, relative to the entry, an INTERFACE_SOURCES file with `contents` is
// kept: gen/shared/<sha256(contents)[:12]>/<relative>. Content-addressed, so
// every config that generated the same file names the same path, outside
// every config's unit. Xcode rejects a target whose sources vary by
// configuration, and a config added to the entry later must not change a
// path an earlier config already uses. Add-only: a file there is never
// replaced by different contents.
std::string sharedSourcePath(std::string_view contents, std::string_view relative);

// The configs a facade looks for: every one a request can build
// (buildConfigFor), in canonical order.
inline constexpr std::string_view facadeConfigs[] = {
    "Debug", "Release", "RelWithDebInfo"};

// epm-targets.cmake: the same text in every entry, written once when the
// entry is created and never rewritten. When it is loaded it finds the
// configs published in configs/ (their marker, epm-config.json), includes
// each export-<Config>.cmake, defines one INTERFACE target per name in the
// .names files, linking the per-config target configSelector picks for the
// configs found, and an ALIAS per line of the .aliases files; with
// find_package(Threads) first when an export file links Threads::Threads.
std::string targetsFileText();

// Writes targetsFileText() to <entry>/epm-targets.cmake. Returns the error.
std::optional<std::string> writeTargetsFile(const eacp::FilePath& entry);

// The directories outside its build tree that a child build reads from. What
// the export file takes from them is copied into the entry, and no CMake file
// of the entry may name any of them (nor the build tree, nor the prefix).
// An empty one is not checked. `source` is the snapshot's root (sourceRootOf),
// not necessarily the directory the child build configured.
struct HarvestRoots
{
    eacp::FilePath cache;
    eacp::FilePath source;
    eacp::FilePath cpm;
};

struct HarvestResult
{
    bool ok = false;
    eacp::Vector<std::string> targetNames;
    bool needsThreads = false;

    // What was copied into <unit>/gen/<config>, relative to it.
    eacp::Vector<std::string> relocatedPaths;

    // What went to <entry>/gen/shared, relative to the entry.
    eacp::Vector<std::string> sharedPaths;

    // What was copied from outside the build tree, relative to the unit:
    // include/src/... from the source, include/cpm/... from the CPM cache,
    // include/dep/<name>/... from a dependency's snapshot and dep/<name>/...
    // from a dependency's entry.
    eacp::Vector<std::string> includedPaths;

    // Set when the build tree could not be deleted.
    std::string warning;
    std::string error;
};

// After a child build of `config` into the unit configDirectory(entry,
// config) of a staged entry (a private directory laid out as the entry, which
// the build step publishes afterwards: BinaryCache.h), makes the unit's
// export-<config>.cmake name only files inside the entry: the INTERFACE_SOURCES
// files it takes from the build tree go to <entry>/<sharedSourcePath>, named
// as sharedRootVariable/<path> (with sharedRootLine first in the file), and
// everything else into the unit, named as entryDirectory/<path>: the rest of
// what it takes from the build tree to gen/<config>, what it takes from the
// source and the CPM cache to include/src and include/cpm, and paths into the
// unit become relative; then what it takes from a dependency, as a nested
// child build sees it, to include/dep/<name> from its snapshot
// (<cache>/src/<name>/<id>, the dependency's <name>_SOURCE_DIR) and to
// dep/<name> from its entry (<cache>/bin/<name>/<platform>/<fp12>).
// Directories are copied as isCopiedIntoEntry says, keeping their sources when
// the export file names a source file inside; a source file the export file
// names outside every directory it names brings its own directory along, with
// its sources, for what it includes by relative path. Fails when the
// rewritten file still names the staged entry, the build tree or one of
// `roots`, naming the line; otherwise deletes the build tree.
HarvestResult harvestConfig(const eacp::FilePath& buildDirectory,
                            const eacp::FilePath& entry,
                            const std::string& config,
                            const HarvestRoots& roots);
} // namespace epm
