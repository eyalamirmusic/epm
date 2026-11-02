#pragma once

#include <eacp/Core/Utils/FilePath.h>

#include <string>

namespace epm
{
// What responses/<name>.cmake tells epm.cmake.
struct Response
{
    std::string name;

    // "prebuilt" or "source".
    std::string mode;

    eacp::FilePath sourceDirectory;

    // Prebuilt only.
    eacp::FilePath prefix;
};

std::string responseText(const Response& response);

bool writeResponse(const eacp::FilePath& path, const Response& response);
} // namespace epm
