#include "Harvest.h"
#include "Configs.h"

#include <epm/Util/Log.h>
#include <epm/Util/Paths.h>
#include <epm/Util/Sha256.h>
#include <epm/Util/Text.h>

#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/Strings.h>

#include <algorithm>
#include <cctype>
#include <map>
#include <tuple>

namespace epm
{
using namespace eacp;

namespace
{
// What closes a path inside an export file: the end of a quoted string, a
// list separator, a generator expression's argument separator or closing
// bracket, a line end. Inside a SHELL: option a space does too.
constexpr auto pathTerminators = std::string_view {"\";>,\n\r"};

// What may come right before a path: the start of a quoted string, a list
// separator, a generator expression's colon, comma or closing bracket, the
// `=` of an option's value, whitespace.
constexpr auto pathStarters = std::string_view {"\"';:=,> \t\n\r"};

// Options written with their path joined, as -I<path>.
constexpr std::string_view pathOptions[] = {"-isystem",
                                            "-iquote",
                                            "-idirafter",
                                            "-iframework",
                                            "-isysroot",
                                            "-include",
                                            "-imacros",
                                            "-I",
                                            "-L",
                                            "-F"};

constexpr std::string_view includeOptions[] = {
    "-isystem", "-iquote", "-idirafter", "-I"};

// The properties (and the file-set keyword) that name include directories:
// what a consumer only reads headers from.
constexpr std::string_view includeProperties[] = {
    "INTERFACE_INCLUDE_DIRECTORIES",
    "INTERFACE_SYSTEM_INCLUDE_DIRECTORIES",
    "INCLUDE_DIRECTORIES",
    "BASE_DIRS"};

constexpr auto interfaceSourcesProperty = std::string_view {"INTERFACE_SOURCES"};

constexpr std::string_view versionControlDirectories[] = {".git", ".hg", ".svn"};

constexpr auto sourceExtensions =
    std::string_view {"c; cc; cp; cpp; cxx; c++; m; mm; s; asm; cu; swift"};

constexpr auto compiledExtensions = std::string_view {
    "o; obj; a; lib; so; dylib; dll; exe; pdb; ilk; exp; idb; pch; gch; pcm; ifc"};

constexpr auto buildExtensions = std::string_view {
    "cmake; ninja; mk; make; bzl; gn; gni; sln; vcxproj; filters; pbxproj; log; "
    "tlog; d"};

bool isBuildScript(const FilePath& file)
{
    static const auto names = Vector<std::string> {"cmakelists.txt",
                                                   "cmakecache.txt",
                                                   "makefile",
                                                   "gnumakefile",
                                                   "makefile.am",
                                                   "makefile.in",
                                                   "configure",
                                                   "configure.ac",
                                                   "meson.build",
                                                   "meson_options.txt",
                                                   "build",
                                                   "build.bazel",
                                                   "workspace",
                                                   "workspace.bazel",
                                                   "module.bazel",
                                                   "sconstruct",
                                                   "sconscript"};

    return names.contains(Strings::toLower(file.fileName()))
           || file.hasExtension(buildExtensions);
}

bool isSourceFile(const FilePath& file)
{
    return file.hasExtension(sourceExtensions);
}

template <typename List>
bool listContains(const List& list, std::string_view value)
{
    return std::ranges::find(list, value) != std::ranges::end(list);
}

// Whether `found` lies in a SHELL: list item, whose words a space separates.
bool isInShellOption(std::string_view text, std::size_t found)
{
    const auto before = text.substr(0, found);
    const auto itemStart = before.find_last_of("\";\n");
    const auto item =
        itemStart == std::string_view::npos ? before : before.substr(itemStart + 1);

    return item.find("SHELL:") != std::string_view::npos;
}

bool endsPathAt(std::string_view text, std::size_t position, bool inShellOption)
{
    return position >= text.size()
           || pathTerminators.find(text[position]) != std::string_view::npos
           || (inShellOption && text[position] == ' ');
}

bool isPathStarter(std::string_view text, std::size_t position)
{
    return position == 0
           || pathStarters.find(text[position - 1]) != std::string_view::npos;
}

// The option written right before `found` with its path joined (-I<path>),
// itself where a path could start; else empty.
std::string_view optionBefore(std::string_view text, std::size_t found)
{
    for (const auto option: pathOptions)
    {
        if (found < option.size())
            continue;

        const auto start = found - option.size();

        if (text.substr(start, option.size()) == option
            && isPathStarter(text, start))
            return option;
    }

    return {};
}

// False where `found` continues a longer name: /var inside /private/var,
// /opt/epm inside /usr/local/opt/epm.
bool startsPathAt(std::string_view text, std::size_t found)
{
    return isPathStarter(text, found) || !optionBefore(text, found).empty();
}

std::string withoutTrailingSeparators(std::string_view path)
{
    while (!path.empty() && (path.back() == '/' || path.back() == '\\'))
        path.remove_suffix(1);

    return std::string {path};
}

// A path the text names at `found`, where `root` starts: its relative part
// and where it ends, or nullopt when `root` there is only the start of a
// longer name.
struct NamedPath
{
    std::string relative;
    std::size_t end = 0;
};

std::optional<NamedPath>
    namedPathAt(std::string_view text, std::size_t found, std::string_view root)
{
    if (!startsPathAt(text, found))
        return std::nullopt;

    const auto start = found + root.size();
    const auto inShellOption = isInShellOption(text, found);

    if (endsPathAt(text, start, inShellOption))
        return NamedPath {{}, start};

    if (text[start] != '/')
        return std::nullopt;

    auto end = start;

    while (!endsPathAt(text, end, inShellOption))
        ++end;

    return NamedPath {
        withoutTrailingSeparators(text.substr(start + 1, end - start - 1)), end};
}

// The property a line of an export file sets (`  INTERFACE_LINK_LIBRARIES
// "..."`) or the file-set keyword it holds (`BASE_DIRS "..."`), else empty.
std::string_view propertyOfLine(std::string_view line)
{
    const auto start = line.find_first_not_of(" \t");

    if (start == std::string_view::npos)
        return {};

    const auto rest = line.substr(start);
    const auto end = rest.find_first_of(" \t");

    if (end == std::string_view::npos || end == 0
        || !std::isupper(static_cast<unsigned char>(rest[0])))
        return {};

    const auto name = rest.substr(0, end);
    const auto isNameCharacter = [](char c)
    {
        const auto byte = static_cast<unsigned char>(c);
        return std::isupper(byte) || std::isdigit(byte) || c == '_';
    };

    return std::ranges::all_of(name, isNameCharacter) ? name : std::string_view {};
}

// The path as written and with its symlinks resolved, when they differ: a
// child build may spell a directory either way (/var and /private/var).
Vector<std::string> spellingsOf(const FilePath& path)
{
    auto spellings = Vector<std::string> {};

    if (path.empty())
        return spellings;

    for (const auto& spelling: {path.str(), resolvedPath(path).str()})
        if (auto trimmed = withoutTrailingSeparators(spelling); !trimmed.empty())
            spellings.addIfNotThere(trimmed);

    return spellings;
}

// `relative` without "." or "..", or nullopt when it climbs out of its root.
std::optional<std::string> normalRelative(std::string_view relative)
{
    auto normal = withoutTrailingSeparators(
        FilePath {std::string {relative}}.lexicallyNormal().str());

    if (normal == ".")
        normal.clear();

    if (normal == ".." || normal.starts_with("../"))
        return std::nullopt;

    return normal;
}

FilePath under(const FilePath& directory, const std::string& relative)
{
    return relative.empty() ? directory : directory / relative;
}

std::string inEntry(std::string_view location, const std::string& relative)
{
    auto path = std::string {location};

    if (!relative.empty())
        path += "/" + relative;

    return path;
}

bool isInside(const std::string& path, const std::string& directory)
{
    return path != directory
           && (directory.empty() || path.starts_with(directory + "/"));
}

std::string lineContaining(std::string_view text, std::string_view needle)
{
    for (const auto line: splitLines(text))
        if (line.find(needle) != std::string_view::npos)
            return std::string {Strings::trim(line)};

    return {};
}

// What names a path, for an error: "INTERFACE_LINK_DIRECTORIES names <path>".
std::string namedBy(std::string_view property, const std::string& path)
{
    return (property.empty() ? std::string {"it"} : std::string {property})
           + " names " + path;
}

std::string parentOf(const std::string& relative)
{
    const auto slash = relative.rfind('/');
    return slash == std::string::npos ? std::string {} : relative.substr(0, slash);
}

// How much of a directory the export file names is copied, each keeping all
// the one before it does.
enum class CopyFilter
{
    // An include directory: isCopiedIntoEntry without sources.
    headers,

    // An include directory the consumer compiles a source file from.
    headersAndSources,

    // Anything else (link directories, libraries, frameworks, options):
    // all of it but version-control and CMakeFiles directories.
    everything
};

void widen(CopyFilter& filter, CopyFilter atLeast)
{
    filter = std::max(filter, atLeast);
}

bool isSkippedDirectory(const FilePath& directory)
{
    const auto name = directory.fileName();
    return name == "CMakeFiles" || listContains(versionControlDirectories, name);
}

// Copies the files under `from` that `filter` keeps into `to`, leaving what is
// already there: an earlier config, or a directory around this one, copied
// the same files.
bool copyTree(const FilePath& from, const FilePath& to, CopyFilter filter)
{
    if (!Files::createDirectories(to))
        return false;

    auto options = Files::DirectoryOptions {};
    options.recursive = true;
    options.symlinks = Files::Symlinks::follow;
    options.includeHidden = filter == CopyFilter::everything;
    auto copied = true;

    Files::forEachEntry(
        from,
        options,
        [&](const Files::DirectoryEntry& entry)
        {
            if (Files::isDirectory(entry.path))
                return isSkippedDirectory(entry.path) ? Files::Visit::skipChildren
                                                      : Files::Visit::next;

            if (!Files::isFile(entry.path))
                return Files::Visit::next;

            if (filter != CopyFilter::everything
                && !isCopiedIntoEntry(entry.path,
                                      filter == CopyFilter::headersAndSources))
                return Files::Visit::next;

            const auto target = to / entry.path.relativeTo(from).str();

            if (Files::isFile(target) || Files::copyFile(entry.path, target))
                return Files::Visit::next;

            copied = false;
            return Files::Visit::stop;
        });

    return copied;
}

// A directory outside the entry that the export file may take paths from,
// under any of its `spellings`: they are copied to `to`, which the export file
// names as `named`.
struct TreeCopy
{
    Vector<std::string> spellings;
    FilePath from;
    FilePath to;
    std::string named;
};

// What the text names under a TreeCopy, across all its spellings: the
// directories with how much of each to copy, the files, and the files the
// consumer compiles (INTERFACE_SOURCES, sources).
struct TreeContents
{
    std::map<std::string, CopyFilter> directories;
    Vector<std::string> files;
    Vector<std::string> compiledFiles;
};

std::optional<std::string> collectContents(std::string_view text,
                                           const TreeCopy& tree,
                                           TreeContents& contents)
{
    for (const auto& spelling: tree.spellings)
    {
        for (const auto& mention: pathMentionsUnder(text, spelling))
        {
            const auto relative = normalRelative(mention.relative);
            const auto named =
                namedBy(mention.property, inEntry(spelling, mention.relative));

            if (!relative)
                return named + ", outside " + spelling;

            const auto source = under(tree.from, *relative);

            if (Files::isDirectory(source))
            {
                // Inserted as headers, the least, then widened.
                auto& filter = contents.directories[*relative];
                widen(filter,
                      mention.namesIncludeDirectory ? CopyFilter::headers
                                                    : CopyFilter::everything);
            }
            else if (Files::isFile(source))
            {
                contents.files.addIfNotThere(*relative);

                if (mention.property == interfaceSourcesProperty
                    || isSourceFile(source))
                    contents.compiledFiles.addIfNotThere(*relative);
            }
            else
            {
                return named + ", which does not exist";
            }
        }
    }

    // A file the consumer compiles may include its siblings by relative path:
    // the directories it lies in keep their sources, and one outside every
    // named directory brings its own.
    for (const auto& file: contents.compiledFiles)
    {
        auto inNamedDirectory = false;

        for (auto& [directory, filter]: contents.directories)
        {
            if (isInside(file, directory))
            {
                widen(filter, CopyFilter::headersAndSources);
                inNamedDirectory = true;
            }
        }

        if (!inNamedDirectory)
            widen(contents.directories[parentOf(file)],
                  CopyFilter::headersAndSources);
    }

    return std::nullopt;
}

// Copies what the text names under tree.spellings into tree.to, adds each
// path copied to `copied` relative to tree.to, and points the text at the
// copies. Returns the error.
std::optional<std::string> copyIntoEntry(std::string& text,
                                         const TreeCopy& tree,
                                         Vector<std::string>& copied)
{
    auto contents = TreeContents {};

    if (auto error = collectContents(text, tree, contents))
        return error;

    const auto& directories = contents.directories;

    for (const auto& [directory, filter]: directories)
    {
        // A directory around this one copies all this one would.
        const auto coveredBy = [&](const auto& other)
        { return isInside(directory, other.first) && other.second >= filter; };

        if (std::ranges::any_of(directories, coveredBy))
            continue;

        if (!copyTree(
                under(tree.from, directory), under(tree.to, directory), filter))
            return "cannot copy " + under(tree.from, directory).str() + " into "
                   + tree.to.str();

        copied.addIfNotThere(directory);
    }

    for (const auto& file: contents.files)
    {
        const auto target = under(tree.to, file);

        if (!Files::isFile(target)
            && !Files::copyFile(under(tree.from, file), target))
            return "cannot copy " + under(tree.from, file).str() + " into "
                   + tree.to.str();

        copied.addIfNotThere(file);
    }

    for (const auto& spelling: tree.spellings)
        text = rewritePathsUnder(
            text,
            spelling,
            [&](std::string_view named)
            { return inEntry(tree.named, *normalRelative(named)); });

    return std::nullopt;
}

// The roots of the dependency trees the text names under `directory`, one of
// the cache's src or bin: the first `depth` components of each path there
// (<name>/<id> for a snapshot, <name>/<platform>/<fp12> for an entry),
// leaving out what lies in `skipped` (the entry being harvested). Returns the
// error for a path that stops short of one.
std::optional<std::string> dependencyRoots(std::string_view text,
                                           const std::string& directory,
                                           int depth,
                                           const Vector<std::string>& skipped,
                                           Vector<std::string>& roots)
{
    for (const auto& mention: pathMentionsUnder(text, directory))
    {
        const auto path = inEntry(directory, mention.relative);

        if (skipped.findIf([&](const std::string& entry)
                           { return path == entry || isInside(path, entry); }))
            continue;

        const auto relative = normalRelative(mention.relative);
        const auto components =
            relative ? splitList(*relative, '/') : Vector<std::string> {};

        if (components.size() < depth)
            return namedBy(mention.property, path) + ", in " + directory
                   + " but in no package's " + (depth == 2 ? "snapshot" : "entry");

        auto root = components[0];

        for (auto i = 1; i < depth; ++i)
            root += "/" + components[i];

        roots.addIfNotThere(root);
    }

    return std::nullopt;
}

HarvestResult failure(std::string error)
{
    auto result = HarvestResult {};
    result.error = std::move(error);
    return result;
}
} // namespace

Vector<std::string> parseTargetNames(std::string_view namesText)
{
    auto names = Vector<std::string> {};

    for (const auto line: splitLines(namesText))
    {
        auto name = Strings::trim(line);

        if (!name.empty())
            names.addIfNotThere(name);
    }

    return names;
}

Vector<TargetAlias> parseTargetAliases(std::string_view aliasesText)
{
    auto aliases = Vector<TargetAlias> {};

    for (const auto line: splitLines(aliasesText))
    {
        const auto fields = splitList(Strings::trim(line), ' ');

        if (fields.size() != 2)
            continue;

        const auto& name = fields[0];

        if (!aliases.findIf([&](const TargetAlias& alias)
                            { return alias.name == name; }))
            aliases.add({name, fields[1]});
    }

    return aliases;
}

Vector<PathMention> pathMentionsUnder(std::string_view exportFileText,
                                      std::string_view directory)
{
    const auto root = withoutTrailingSeparators(directory);
    auto mentions = Vector<PathMention> {};

    if (root.empty())
        return mentions;

    for (const auto line: splitLines(exportFileText))
    {
        const auto property = propertyOfLine(line);
        const auto isIncludeProperty = listContains(includeProperties, property);

        for (auto found = line.find(root); found != std::string_view::npos;
             found = line.find(root, found + root.size()))
        {
            const auto named = namedPathAt(line, found, root);

            if (!named)
                continue;

            const auto isInclude =
                isIncludeProperty
                || listContains(includeOptions, optionBefore(line, found));

            mentions.addIfNotThere(
                {named->relative, std::string {property}, isInclude});
        }
    }

    return mentions;
}

Vector<std::string> pathsUnder(std::string_view exportFileText,
                               std::string_view directory)
{
    auto paths = Vector<std::string> {};

    for (const auto& mention: pathMentionsUnder(exportFileText, directory))
        paths.addIfNotThere(mention.relative);

    return paths;
}

std::string rewritePathsUnder(
    std::string_view exportFileText,
    std::string_view directory,
    const std::function<std::string(std::string_view relative)>& replacement)
{
    const auto root = withoutTrailingSeparators(directory);

    if (root.empty())
        return std::string {exportFileText};

    auto result = std::string {};
    auto position = std::size_t {0};
    auto found = exportFileText.find(root);

    while (found != std::string_view::npos)
    {
        const auto named = namedPathAt(exportFileText, found, root);

        if (!named)
        {
            found = exportFileText.find(root, found + root.size());
            continue;
        }

        result.append(exportFileText.substr(position, found - position));
        result.append(replacement(named->relative));
        position = named->end;
        found = exportFileText.find(root, position);
    }

    result.append(exportFileText.substr(position));
    return result;
}

std::string replacePath(std::string_view exportFileText,
                        std::string_view from,
                        std::string_view to)
{
    auto result = std::string {};
    auto position = std::size_t {0};

    for (auto found = exportFileText.find(from); found != std::string_view::npos;
         found = exportFileText.find(from, found + from.size()))
    {
        const auto end = found + from.size();

        if (!startsPathAt(exportFileText, found)
            || !endsPathAt(
                exportFileText, end, isInShellOption(exportFileText, found)))
            continue;

        result.append(exportFileText.substr(position, found - position));
        result.append(to);
        position = end;
    }

    result.append(exportFileText.substr(position));
    return result;
}

std::optional<std::string> firstMentionedPath(std::string_view text,
                                              const Vector<std::string>& paths)
{
    for (const auto& path: paths)
    {
        const auto root = withoutTrailingSeparators(path);

        if (root.empty())
            continue;

        for (auto found = text.find(root); found != std::string_view::npos;
             found = text.find(root, found + root.size()))
            if (namedPathAt(text, found, root))
                return root;
    }

    return std::nullopt;
}

bool mentionsThreads(std::string_view exportFileText)
{
    return exportFileText.find("Threads::Threads") != std::string_view::npos;
}

Vector<std::string> interfaceSourcePaths(std::string_view exportFileText,
                                         std::string_view buildDirectory)
{
    auto paths = Vector<std::string> {};

    for (const auto& mention: pathMentionsUnder(exportFileText, buildDirectory))
        if (mention.property == interfaceSourcesProperty
            && !mention.relative.empty())
            paths.addIfNotThere(mention.relative);

    return paths;
}

bool isCopiedIntoEntry(const FilePath& file, bool keepSources)
{
    if (isBuildScript(file) || file.hasExtension(compiledExtensions))
        return false;

    return keepSources || !isSourceFile(file);
}

FilePath sourceRootOf(const FilePath& cache, const FilePath& sourceDirectory)
{
    for (const auto& cacheSpelling: spellingsOf(cache))
    {
        const auto snapshots = cacheSpelling + "/src";

        for (const auto& spelling: spellingsOf(sourceDirectory))
        {
            if (!isInside(spelling, snapshots))
                continue;

            const auto components =
                splitList(std::string_view {spelling}.substr(snapshots.size()), '/');

            if (components.size() >= 2)
                return FilePath {snapshots + "/" + components[0] + "/"
                                 + components[1]};
        }
    }

    return sourceDirectory;
}

std::string sharedSourcePath(std::string_view contents, std::string_view relative)
{
    return "gen/shared/" + sha256Hex(contents).substr(0, 12) + "/"
           + std::string {relative};
}

std::string sharedRootLine()
{
    return "get_filename_component(" + std::string {sharedRootVariableName}
           + " \"${CMAKE_CURRENT_LIST_DIR}/../..\" ABSOLUTE)\n";
}

FilePath configDirectory(const FilePath& entry, const std::string& config)
{
    return entry / "configs" / config;
}

std::string targetsFileText()
{
    auto configs = Vector<std::string> {};

    for (const auto config: facadeConfigs)
        configs.add(std::string {config});

    auto text = std::string {
        "# Generated by epm: the facade of a binary cache entry. The same in\n"
        "# every entry and never rewritten: it loads the configs published under\n"
        "# configs/ as it finds them, so adding one changes nothing here; with\n"
        "# EPM_FACADE_CONFIGS set, only those of them (the ones the consumer can\n"
        "# link: a config of LTO objects of another compiler is left out).\n"
        "\n"
        "set(_epm_configs \"\")\n"
        "set(_epm_targets \"\")\n"
        "set(_epm_aliases \"\")\n"
        "set(_epm_threads OFF)\n"
        "\n"
        "foreach (_epm_config IN ITEMS "
        + joinList(configs, " ")
        + ")\n"
          "    set(_epm_unit \"${CMAKE_CURRENT_LIST_DIR}/configs/${_epm_config}\")\n"
          "    set(_epm_export \"${_epm_unit}/export-${_epm_config}.cmake\")\n"
          "\n"
          "    if (NOT EXISTS \"${_epm_unit}/epm-config.json\")\n"
          "        continue ()\n"
          "    endif ()\n"
          "\n"
          "    if (DEFINED EPM_FACADE_CONFIGS)\n"
          "        list(FIND EPM_FACADE_CONFIGS \"${_epm_config}\" _epm_index)\n"
          "\n"
          "        if (_epm_index EQUAL -1)\n"
          "            continue ()\n"
          "        endif ()\n"
          "    endif ()\n"
          "\n"
          "    list(APPEND _epm_configs ${_epm_config})\n"
          "    file(STRINGS \"${_epm_export}.names\" _epm_lines)\n"
          "    list(APPEND _epm_targets ${_epm_lines})\n"
          "\n"
          "    if (EXISTS \"${_epm_export}.aliases\")\n"
          "        file(STRINGS \"${_epm_export}.aliases\" _epm_lines)\n"
          "        list(APPEND _epm_aliases ${_epm_lines})\n"
          "    endif ()\n"
          "\n"
          "    file(STRINGS \"${_epm_export}\" _epm_lines REGEX \"Threads::Threads\")\n"
          "\n"
          "    if (_epm_lines)\n"
          "        set(_epm_threads ON)\n"
          "    endif ()\n"
          "endforeach ()\n"
          "\n"
          "if (NOT _epm_configs)\n"
          "    message(FATAL_ERROR \"epm: the entry ${CMAKE_CURRENT_LIST_DIR} holds "
          "no config\")\n"
          "endif ()\n"
          "\n"
          "list(REMOVE_DUPLICATES _epm_targets)\n"
          "\n"
          "if (_epm_threads)\n"
          "    find_package(Threads REQUIRED)\n"
          "endif ()\n"
          "\n"
          "foreach (_epm_config IN LISTS _epm_configs)\n"
          "    include(\"${CMAKE_CURRENT_LIST_DIR}/configs/${_epm_config}/"
          "export-${_epm_config}.cmake\")\n"
          "endforeach ()\n"
          "\n"
          "foreach (_epm_target IN LISTS _epm_targets)\n"
          "    if (TARGET ${_epm_target})\n"
          "        continue ()\n"
          "    endif ()\n"
          "\n"
          "    add_library(${_epm_target} INTERFACE IMPORTED GLOBAL)\n"
          "\n"};

    // One branch per set of configs an entry can hold, each linking what
    // configSelector picks for that set.
    for (auto mask = 1u; mask < 1u << configs.size(); ++mask)
    {
        auto present = Vector<std::string> {};

        for (auto i = 0; i < configs.size(); ++i)
            if ((mask & 1u << i) != 0)
                present.add(configs[i]);

        text += std::string {mask == 1u ? "    if" : "    elseif"}
                + " (_epm_configs STREQUAL \"" + joinList(present, ";")
                + "\")\n"
                  "        target_link_libraries(${_epm_target} INTERFACE\n"
                  "                \""
                + configSelector(present, "${_epm_target}") + "\")\n";
    }

    text += "    endif ()\n"
            "endforeach ()\n"
            "\n"
            "foreach (_epm_line IN LISTS _epm_aliases)\n"
            "    string(REPLACE \" \" \";\" _epm_alias \"${_epm_line}\")\n"
            "    list(LENGTH _epm_alias _epm_fields)\n"
            "\n"
            "    if (NOT _epm_fields EQUAL 2)\n"
            "        continue ()\n"
            "    endif ()\n"
            "\n"
            "    list(GET _epm_alias 0 _epm_name)\n"
            "    list(GET _epm_alias 1 _epm_target)\n"
            "\n"
            "    if (_epm_target IN_LIST _epm_targets AND NOT TARGET ${_epm_name})\n"
            "        add_library(${_epm_name} ALIAS ${_epm_target})\n"
            "    endif ()\n"
            "endforeach ()\n"
            "\n"
            "unset(_epm_configs)\n"
            "unset(_epm_index)\n"
            "unset(_epm_targets)\n"
            "unset(_epm_aliases)\n"
            "unset(_epm_threads)\n";

    return text;
}

std::optional<std::string> writeTargetsFile(const FilePath& entry)
{
    const auto targetsFile = entry / "epm-targets.cmake";

    if (!writeTextFile(targetsFile, targetsFileText()))
        return "cannot write " + targetsFile.str();

    return std::nullopt;
}

HarvestResult harvestConfig(const FilePath& buildDirectory,
                            const FilePath& entry,
                            const std::string& config,
                            const HarvestRoots& roots)
{
    const auto prefix = configDirectory(entry, config);
    const auto exportFile = prefix / ("export-" + config + ".cmake");
    const auto namesFile = prefix / ("export-" + config + ".cmake.names");

    if (!Files::isFile(exportFile) || !Files::isFile(namesFile))
        return failure("the " + config + " build wrote no " + exportFile.fileName()
                       + " (or its .names) - was epm-export.cmake injected?");

    auto text = Files::readFile(exportFile);
    auto result = HarvestResult {};
    const auto rejected = [&](const std::string& reason)
    { return failure(exportFile.fileName() + ": " + reason); };

    for (const auto& tree: spellingsOf(buildDirectory))
    {
        for (const auto& relative: interfaceSourcePaths(text, tree))
        {
            const auto from = buildDirectory / relative;

            if (!Files::isFile(from))
                return rejected("it lists " + from.str()
                                + " in INTERFACE_SOURCES, which the build did not "
                                  "produce as a file");

            const auto shared = sharedSourcePath(Files::readFile(from), relative);
            const auto to = entry / shared;

            if (!Files::isFile(to) && !Files::copyFile(from, to))
                return failure("cannot copy " + from.str() + " to " + to.str());

            text = replacePath(
                text, tree + "/" + relative, inEntry(sharedRootVariable, shared));
            Log::step("epm: shared " + shared);
            result.sharedPaths.add(shared);
        }
    }

    const auto treeInUnit = [&](const FilePath& from, const std::string& relative)
    {
        return TreeCopy {spellingsOf(from),
                         from,
                         prefix / relative,
                         inEntry(entryDirectory, relative)};
    };

    const auto copy = [&](const TreeCopy& tree) -> std::optional<std::string>
    {
        auto copied = Vector<std::string> {};

        if (auto error = copyIntoEntry(text, tree, copied))
            return error;

        const auto isBuildTree = tree.from == buildDirectory;

        for (const auto& path: copied)
        {
            const auto inPrefix = inEntry(tree.to.relativeTo(prefix).str(), path);
            Log::step("epm: copied " + inPrefix);

            if (isBuildTree)
                result.relocatedPaths.addIfNotThere(path);
            else
                result.includedPaths.addIfNotThere(inPrefix);
        }

        return std::nullopt;
    };

    for (const auto& tree: {treeInUnit(buildDirectory, "gen/" + config),
                            treeInUnit(roots.source, "include/src"),
                            treeInUnit(roots.cpm, "include/cpm")})
        if (auto error = copy(tree))
            return rejected(*error);

    for (const auto& spelling: spellingsOf(prefix))
    {
        for (const auto& mention: pathMentionsUnder(text, spelling))
            if (!normalRelative(mention.relative))
                return rejected(
                    namedBy(mention.property, inEntry(spelling, mention.relative))
                    + ", outside the entry");

        text = rewritePathsUnder(
            text,
            spelling,
            [](std::string_view named)
            { return inEntry(entryDirectory, *normalRelative(named)); });
    }

    // A nested child build sees its dependencies' snapshots (<name>_SOURCE_DIR)
    // and entries (their imported targets' paths): what it takes from them is
    // copied too, a package's under include/dep/<name> and dep/<name>.
    const auto dependencyTrees = {std::tuple {"src", 2, "include/dep"},
                                  std::tuple {"bin", 3, "dep"}};
    const auto entrySpellings = spellingsOf(entry);

    for (const auto& [kind, depth, into]: dependencyTrees)
    {
        auto treeRoots = Vector<std::string> {};

        for (const auto& spelling: spellingsOf(roots.cache))
        {
            const auto directory = spelling + "/" + kind;

            if (auto error = dependencyRoots(
                    text, directory, depth, entrySpellings, treeRoots))
                return rejected(*error);
        }

        for (const auto& treeRoot: treeRoots)
        {
            const auto from = roots.cache / kind / treeRoot;
            const auto name = treeRoot.substr(0, treeRoot.find('/'));

            if (auto error = copy(treeInUnit(from, std::string {into} + "/" + name)))
                return rejected(*error);
        }
    }

    auto foreign = Vector<std::string> {};

    for (const auto& path:
         {roots.cache, roots.source, roots.cpm, buildDirectory, prefix, entry})
        for (const auto& spelling: spellingsOf(path))
            foreign.addIfNotThere(spelling);

    if (const auto named = firstMentionedPath(text, foreign))
    {
        const auto line = lineContaining(text, *named);
        const auto property = propertyOfLine(line);
        const auto in =
            property.empty() ? std::string {} : " in " + std::string {property};

        return rejected("it still names " + *named + in
                        + " after the harvest, so the entry would only work where "
                          "it was built: "
                        + line);
    }

    if (!result.sharedPaths.empty())
        text = sharedRootLine() + text;

    if (!writeTextFile(exportFile, text))
        return failure("cannot rewrite " + exportFile.str());

    result.targetNames = parseTargetNames(Files::readFile(namesFile));
    result.needsThreads = mentionsThreads(text);

    if (result.targetNames.empty())
        return failure(namesFile.str() + " lists no targets");

    if (!Files::removeAll(buildDirectory))
        result.warning = "cannot delete " + buildDirectory.str();

    result.ok = true;
    return result;
}
} // namespace epm
