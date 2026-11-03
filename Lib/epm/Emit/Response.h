#pragma once

#include <eacp/Core/Utils/FilePath.h>

#include <string>
#include <string_view>

namespace epm
{
// Which way a package was resolved.
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

// Includes a binary entry's targets file and makes the imported targets
// global.
std::string prebuiltTargetsText(const eacp::FilePath& prefix);
} // namespace epm
