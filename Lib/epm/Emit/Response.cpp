#include "Response.h"

#include <epm/Util/Text.h>

namespace epm
{
std::string cmakeQuoted(std::string_view text)
{
    auto quoted = std::string {"\""};

    for (const auto c: text)
    {
        if (c == '\\' || c == '"' || c == '$')
            quoted += '\\';

        quoted += c;
    }

    return quoted + "\"";
}

std::string responseText(const Response& response)
{
    const auto variable = "epm_" + response.name + "_";
    const auto set = [&](std::string_view suffix, const std::string& quotedValue)
    { return "set(" + variable + std::string {suffix} + " " + quotedValue + ")\n"; };

    auto text = set("MODE", cmakeQuoted(response.mode))
                + set("SOURCE_DIR", cmakeQuoted(response.sourceDirectory.str()));

    if (response.mode != "prebuilt")
        return text;

    text += set("PREFIX", cmakeQuoted(response.prefix.str()))
            + set("TARGETS_FILE", "\"${" + variable + "PREFIX}/epm-targets.cmake\"")
            + set("PLATFORM", cmakeQuoted(response.platform))
            + set("CONFIGS", cmakeQuoted(joinList(response.configs, ";")));

    if (!response.note.empty())
        text += set("NOTE", cmakeQuoted(response.note));

    return text;
}

bool writeResponse(const eacp::FilePath& path, const Response& response)
{
    return writeTextFile(path, responseText(response));
}
} // namespace epm
