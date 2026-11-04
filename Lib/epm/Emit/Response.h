#pragma once

#include <eacp/Core/Utils/Containers.h>
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

    // Prebuilt only: the entry's platform and the configs it holds.
    std::string platform;
    EA::Vector<std::string> configs;

    // Prebuilt only: set when the entry was built with a toolchain that only
    // matches the current one under the request's abiMatch (abiNote).
    std::string note;
};

// `text` as a double-quoted CMake argument.
std::string cmakeQuoted(std::string_view text);

// The epm_load_package call (epm-provider.cmake) that includes a prebuilt
// entry's targets file, makes its imported targets global and prints the
// package's status line.
std::string prebuiltLoadText(const Response& response);
} // namespace epm
