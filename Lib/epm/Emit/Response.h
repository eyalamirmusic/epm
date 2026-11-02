#pragma once

#include <eacp/Core/Utils/FilePath.h>

#include <string>
#include <string_view>

namespace epm
{
// What responses/<name>.cmake tells epm.cmake.
struct Response
{
    std::string name;

    // "prebuilt" or "source".
    std::string mode;

    eacp::FilePath sourceDirectory;

    // Prebuilt only; the targets file is <prefix>/epm-targets.cmake.
    eacp::FilePath prefix;
};

// `text` as a double-quoted CMake argument.
std::string cmakeQuoted(std::string_view text);

std::string responseText(const Response& response);

bool writeResponse(const eacp::FilePath& path, const Response& response);
} // namespace epm
