#include "Text.h"

#include <eacp/Core/Utils/Files.h>

namespace epm
{
using namespace eacp;

bool writeTextFile(const FilePath& path, std::string_view text)
{
    if (!Files::createDirectories(path.parentDirectory()))
        return false;

    try
    {
        Files::writeFileAtomically(path, text);
    }
    catch (const std::exception&)
    {
        return false;
    }

    return true;
}

Vector<std::string_view> splitLines(std::string_view text)
{
    auto result = Vector<std::string_view> {};

    while (!text.empty())
    {
        const auto end = text.find('\n');
        auto line = text.substr(0, end);

        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);

        if (!line.empty())
            result.add(line);

        if (end == std::string_view::npos)
            break;

        text.remove_prefix(end + 1);
    }

    return result;
}

std::string
    replaceAll(std::string_view text, std::string_view from, std::string_view to)
{
    if (from.empty())
        return std::string {text};

    auto result = std::string {};
    auto position = std::size_t {0};

    for (auto found = text.find(from); found != std::string_view::npos;
         found = text.find(from, position))
    {
        result.append(text.substr(position, found - position));
        result.append(to);
        position = found + from.size();
    }

    result.append(text.substr(position));
    return result;
}
} // namespace epm
