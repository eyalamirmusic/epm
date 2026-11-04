#pragma once

#include <eacp/Core/Utils/Containers.h>
#include <eacp/Core/Utils/FilePath.h>

#include <optional>
#include <string>
#include <string_view>

namespace epm
{
struct CommandResult
{
    bool ok = false;

    // stdout and stderr, interleaved as the child wrote them.
    std::string output;

    // Why it failed, one line: could not launch, or the exit code.
    std::string error;
};

// Runs a child to completion with stdout and stderr merged. Its output is
// streamed live when Log::isVerbose(), and always kept in the result.
CommandResult runCommand(const std::string& executable,
                         const EA::Vector<std::string>& arguments);

// The last `count` lines of `text`, for a failure report that stays readable.
std::string lastLines(std::string_view text, int count);

// The first file called `name` (plus .exe on Windows) in a PATH directory
// that the user may execute.
std::optional<eacp::FilePath> findExecutable(std::string_view name);
} // namespace epm
