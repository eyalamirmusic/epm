#include "GraphResponse.h"
#include "Response.h"

#include <epm/Spec/JsonFile.h>
#include <epm/Util/Text.h>

namespace epm
{
using namespace eacp;

namespace
{
Vector<std::string> optionList(const std::map<std::string, std::string>& options)
{
    auto list = Vector<std::string> {};

    for (const auto& [key, value]: options)
        list.add(key + "=" + value);

    return list;
}

std::string packageText(const ResolvedPackage& package)
{
    const auto variable = "epm_" + package.name + "_";
    const auto set = [&](std::string_view suffix, const std::string& value)
    { return "set(" + variable + std::string {suffix} + " " + value + ")\n"; };

    auto text =
        set("MODE", package.mode) + set("SOURCE_DIR", cmakeQuoted(package.sourceDir))
        + set("DEPS", cmakeQuotedList(package.deps))
        + set("OPTIONS", cmakeQuotedList(optionList(package.effectiveOptions())));

    if (package.mode != "prebuilt")
        return text;

    return text + set("PREFIX", cmakeQuoted(package.prefix))
           + set("TARGETS_FILE", "\"${" + variable + "PREFIX}/epm-targets.cmake\"")
           + set("PLATFORM", cmakeQuoted(package.platform))
           + set("CONFIGS", cmakeQuotedList(package.configs))
           + set("NOTE", cmakeQuoted(package.note));
}
} // namespace

std::string graphCMakeText(const ResolvedGraph& graph)
{
    auto names = Vector<std::string> {};

    for (const auto& package: graph.packages)
        names.add(package.name);

    auto text = "set(epm_PACKAGES " + cmakeQuotedList(names) + ")\n";

    for (const auto& package: graph.packages)
        text += packageText(package);

    return text + "set(epm_NOTES " + cmakeQuotedList(graph.notes) + ")\n";
}

bool writeGraphCMake(const FilePath& path, const ResolvedGraph& graph)
{
    return writeTextFile(path, graphCMakeText(graph));
}

bool writeGraphJson(const FilePath& path, const ResolvedGraph& graph)
{
    return saveJsonFile(path, graph);
}

std::optional<ResolvedGraph> loadGraphJson(const FilePath& path)
{
    return loadJsonFile<ResolvedGraph>(path);
}
} // namespace epm
