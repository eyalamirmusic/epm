#pragma once

#include <string>

namespace DependencyResolution
{
// Writes "epm: <line>" to stderr, where CMake shows it during configure.
void printStatus(const std::string& line);
} // namespace DependencyResolution
