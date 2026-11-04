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

std::string prebuiltLoadText(const Response& response)
{
    auto text = "epm_load_package(" + cmakeQuoted(response.name)
                + " MODE prebuilt SOURCE_DIR "
                + cmakeQuoted(response.sourceDirectory.str()) + " TARGETS_FILE "
                + cmakeQuoted((response.prefix / "epm-targets.cmake").str())
                + " PREFIX " + cmakeQuoted(response.prefix.str()) + " PLATFORM "
                + cmakeQuoted(response.platform);

    if (!response.note.empty())
        text += " NOTE " + cmakeQuoted(response.note);

    return text + ")\n";
}
} // namespace epm
