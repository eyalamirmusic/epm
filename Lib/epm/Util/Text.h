#pragma once

#include <eacp/Core/Utils/Containers.h>
#include <eacp/Core/Utils/FilePath.h>

#include <string>
#include <string_view>

namespace epm
{
// Files::writeFileAtomically for text, creating parent directories; false
// instead of throwing.
bool writeTextFile(const eacp::FilePath& path, std::string_view text);

// The non-empty lines of `text`, without their line endings (\n or \r\n).
EA::Vector<std::string_view> splitLines(std::string_view text);

// `text` with every occurrence of `from` replaced by `to`.
std::string
    replaceAll(std::string_view text, std::string_view from, std::string_view to);
} // namespace epm
