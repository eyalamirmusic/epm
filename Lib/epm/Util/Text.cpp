#include "Text.h"

#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/Strings.h>

namespace epm
{
using namespace eacp;

EA::Vector<std::string_view> splitLines(std::string_view text)
{
    auto result = EA::Vector<std::string_view> {};

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

EA::Vector<std::string> splitList(std::string_view text, char separator)
{
    auto items = EA::Vector<std::string> {};

    while (!text.empty())
    {
        const auto end = text.find(separator);
        const auto item = text.substr(0, end);

        if (!item.empty())
            items.add(std::string {item});

        if (end == std::string_view::npos)
            break;

        text.remove_prefix(end + 1);
    }

    return items;
}

} // namespace epm
