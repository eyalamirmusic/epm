#pragma once

#include <eacp/Core/Utils/Containers.h>
#include <eacp/Core/Utils/Strings.h>
#include <eacp/Core/Utils/FilePath.h>

#include <string>
#include <string_view>

namespace DependencyResolution
{
// The non-empty lines of `text`, without their line endings (\n or \r\n).
EA::Vector<std::string_view> splitLines(std::string_view text);

// The non-empty items of a `separator`-separated list, such as a CMake list;
// eacp's Strings::split keeps the empty ones.
EA::Vector<std::string> splitList(std::string_view text, char separator = ';');

// `text` with every occurrence of `from` replaced by `to`.
std::string
    replaceAll(std::string_view text, std::string_view from, std::string_view to);
} // namespace DependencyResolution
