#pragma once

#include <string>

namespace DependencyResolution
{
// URL_HASH <algorithm>=<hex>, in any algorithm CMake's file(<algorithm>) takes.
struct ArchiveHash
{
    std::string algorithm;
    std::string hex;
};
} // namespace DependencyResolution
