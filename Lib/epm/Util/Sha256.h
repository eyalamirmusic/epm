#pragma once

#include <eacp/Core/Utils/FilePath.h>

#include <optional>
#include <string>
#include <string_view>

namespace epm
{
// FIPS 180-4 SHA-256 as 64 lowercase hex characters.
std::string sha256Hex(std::string_view bytes);

// nullopt when the file cannot be opened.
std::optional<std::string> sha256HexOfFile(const eacp::FilePath& path);
} // namespace epm
