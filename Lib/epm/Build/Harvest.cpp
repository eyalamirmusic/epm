#include "Harvest.h"
#include "Configs.h"

#include <epm/Util/Log.h>
#include <eacp/Core/Utils/Sha256.h>
#include <epm/Util/Text.h>

#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/Strings.h>

namespace epm
{
using namespace eacp;

namespace
{
// What closes a path inside an export file: the end of a quoted string, a
// list separator, a generator expression's closing bracket, a line end.
constexpr auto pathTerminators = std::string_view {"\";>\n\r"};

std::string buildTreePrefix(std::string_view buildDirectory)
{
    auto prefix = std::string {buildDirectory};

    if (!prefix.empty() && prefix.back() != '/')
        prefix += '/';

    return prefix;
}

bool copyEntry(const FilePath& from, const FilePath& to)
{
    if (Files::isDirectory(from))
        return Files::copyDirectory(from, to);

    return Files::copyFile(from, to);
}

HarvestResult failure(std::string error)
{
    auto result = HarvestResult {};
    result.error = std::move(error);
    return result;
}
} // namespace

EA::Vector<std::string> parseTargetNames(std::string_view namesText)
{
    auto names = EA::Vector<std::string> {};

    for (const auto line: splitLines(namesText))
    {
        auto name = Strings::trim(line);

        if (!name.empty())
            names.addIfNotThere(name);
    }

    return names;
}

EA::Vector<TargetAlias> parseTargetAliases(std::string_view aliasesText)
{
    auto aliases = EA::Vector<TargetAlias> {};

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

EA::Vector<std::string> buildTreePaths(std::string_view exportFileText,
                                       std::string_view buildDirectory)
{
    const auto prefix = buildTreePrefix(buildDirectory);
    auto paths = EA::Vector<std::string> {};

    for (auto start = exportFileText.find(prefix); start != std::string_view::npos;
         start = exportFileText.find(prefix, start))
    {
        start += prefix.size();
        const auto end = exportFileText.find_first_of(pathTerminators, start);
        auto path = exportFileText.substr(start, end - start);

        while (!path.empty() && path.back() == '/')
            path.remove_suffix(1);

        if (!path.empty())
            paths.addIfNotThere(std::string {path});

        if (end == std::string_view::npos)
            break;

        start = end;
    }

    return paths;
}

std::string relocateBuildTree(std::string_view exportFileText,
                              std::string_view buildDirectory,
                              std::string_view replacement)
{
    return replaceAll(exportFileText,
                      buildTreePrefix(buildDirectory),
                      buildTreePrefix(replacement));
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
        const auto wholePath =
            end == exportFileText.size()
            || pathTerminators.find(exportFileText[end]) != std::string_view::npos;

        if (!wholePath)
            continue;

        result.append(exportFileText.substr(position, found - position));
        result.append(to);
        position = end;
    }

    result.append(exportFileText.substr(position));
    return result;
}

bool referencesBuildTree(std::string_view exportFileText,
                         std::string_view buildDirectory)
{
    return exportFileText.find(buildDirectory) != std::string_view::npos;
}

bool mentionsThreads(std::string_view exportFileText)
{
    return exportFileText.find("Threads::Threads") != std::string_view::npos;
}

EA::Vector<std::string> interfaceSourcePaths(std::string_view exportFileText,
                                             std::string_view buildDirectory)
{
    auto paths = EA::Vector<std::string> {};

    for (const auto line: splitLines(exportFileText))
    {
        if (Strings::trim(line).rfind("INTERFACE_SOURCES ", 0) != 0)
            continue;

        for (const auto& path: buildTreePaths(line, buildDirectory))
            paths.addIfNotThere(path);
    }

    return paths;
}

std::string sharedSourcePath(std::string_view contents, std::string_view relative)
{
    return "gen/shared/" + eacp::computeSha256Hex(contents).substr(0, 12) + "/"
           + std::string {relative};
}

std::string targetsFileText(const EA::Vector<std::string>& targetNames,
                            const EA::Vector<TargetAlias>& aliases,
                            bool needsThreads,
                            const EA::Vector<std::string>& configs)
{
    const auto available = canonicalConfigs(configs);
    auto text = std::string {"# Generated by epm: facade targets over the "
                             + joinList(available, ", ")
                             + " exports.\n"
                               "# Regenerated whenever a config is added.\n\n"};

    if (needsThreads)
        text += "find_package(Threads REQUIRED)\n\n";

    for (const auto& config: available)
        text +=
            "include(\"${CMAKE_CURRENT_LIST_DIR}/export-" + config + ".cmake\")\n";

    text += "\nforeach (name IN ITEMS";

    for (const auto& name: targetNames)
        text += "\n        " + name;

    text += ")\n"
            "    if (NOT TARGET ${name})\n"
            "        add_library(${name} INTERFACE IMPORTED GLOBAL)\n"
            "        target_link_libraries(${name} INTERFACE\n"
            "                \""
            + configSelector(available, "${name}")
            + "\")\n"
              "    endif ()\n"
              "endforeach ()\n";

    for (const auto& alias: aliases)
    {
        if (!targetNames.contains(alias.target))
            continue;

        text += "\nif (NOT TARGET " + alias.name + ")\n    add_library(" + alias.name
                + " ALIAS " + alias.target + ")\nendif ()\n";
    }

    return text;
}

std::optional<std::string> writeTargetsFile(const FilePath& prefix,
                                            const EA::Vector<std::string>& configs)
{
    auto targetNames = EA::Vector<std::string> {};
    auto aliasesText = std::string {};
    auto needsThreads = false;

    for (const auto& config: configs)
    {
        const auto exportFile = prefix / ("export-" + config + ".cmake");
        const auto namesFile = prefix / ("export-" + config + ".cmake.names");
        const auto aliasesFile = prefix / ("export-" + config + ".cmake.aliases");

        if (!Files::isFile(exportFile) || !Files::isFile(namesFile))
            return "missing " + exportFile.str() + " or its .names";

        for (const auto& name: parseTargetNames(Files::readFile(namesFile)))
            targetNames.addIfNotThere(name);

        if (Files::isFile(aliasesFile))
            aliasesText += Files::readFile(aliasesFile) + "\n";

        needsThreads = needsThreads || mentionsThreads(Files::readFile(exportFile));
    }

    const auto targetsFile = prefix / "epm-targets.cmake";

    const auto text = targetsFileText(
        targetNames, parseTargetAliases(aliasesText), needsThreads, configs);

    if (!writeTextFile(targetsFile, text))
        return "cannot write " + targetsFile.str();

    return std::nullopt;
}

HarvestResult harvestConfig(const FilePath& buildDirectory,
                            const FilePath& prefix,
                            const std::string& config)
{
    const auto exportFile = prefix / ("export-" + config + ".cmake");
    const auto namesFile = prefix / ("export-" + config + ".cmake.names");

    if (!Files::isFile(exportFile) || !Files::isFile(namesFile))
        return failure("the " + config + " build wrote no " + exportFile.fileName()
                       + " (or its .names) - was epm-export.cmake injected?");

    auto text = Files::readFile(exportFile);
    const auto generated = prefix / "gen" / config;
    auto result = HarvestResult {};

    // Spelled as the export file spells them, so replacePath finds them.
    const auto treePrefix = buildTreePrefix(buildDirectory.str());

    for (const auto& relative: interfaceSourcePaths(text, buildDirectory.str()))
    {
        const auto from = buildDirectory / relative;

        if (!Files::isFile(from))
            return failure(exportFile.fileName() + " lists " + from.str()
                           + " in INTERFACE_SOURCES, which the build did not "
                             "produce as a file");

        const auto shared = sharedSourcePath(Files::readFile(from), relative);
        const auto to = prefix.str() + "/" + shared;

        if (!Files::isFile(FilePath {to}) && !Files::copyFile(from, FilePath {to}))
            return failure("cannot copy " + from.str() + " to " + to);

        text = replacePath(text, treePrefix + relative, to);
        Log::step("epm: shared " + shared);
        result.sharedPaths.add(shared);
    }

    for (const auto& relative: buildTreePaths(text, buildDirectory.str()))
    {
        const auto from = buildDirectory / relative;

        if (!Files::exists(from))
            return failure(exportFile.fileName() + " references " + from.str()
                           + ", which the build did not produce");

        if (!copyEntry(from, generated / relative))
            return failure("cannot copy " + from.str() + " into " + generated.str());

        Log::step("epm: relocated " + relative);
        result.relocatedPaths.add(relative);
    }

    const auto relocated =
        relocateBuildTree(text, buildDirectory.str(), generated.str());

    if (!writeTextFile(exportFile, relocated))
        return failure("cannot rewrite " + exportFile.str());

    result.targetNames = parseTargetNames(Files::readFile(namesFile));
    result.needsThreads = mentionsThreads(relocated);

    if (result.targetNames.empty())
        return failure(namesFile.str() + " lists no targets");

    if (referencesBuildTree(relocated, buildDirectory.str()))
        result.warning = exportFile.fileName() + " still references "
                         + buildDirectory.str() + "; keeping that build tree";
    else if (!Files::removeAll(buildDirectory))
        result.warning = "cannot delete " + buildDirectory.str();

    result.ok = true;
    return result;
}
} // namespace epm
