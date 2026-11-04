#pragma once

#include <string_view>

// What `epm resolve --verbose` turns on: step lines on stdout and child process
// output streamed as it arrives rather than kept for a failure report.
namespace epm::Log
{
void setVerbose(bool verbose);
bool isVerbose();

// One line on stdout, only when verbose.
void step(std::string_view line);

// One line on stdout, verbose or not, for the slow work a consumer's configure
// would otherwise sit through in silence. epm.cmake passes stdout through, so
// `line` is styled as a CMake status message.
void progress(std::string_view line);

// A chunk of child output, written as is, only when verbose.
void stream(std::string_view chunk);
} // namespace epm::Log
