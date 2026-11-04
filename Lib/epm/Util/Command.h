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

    // stdout and stderr, interleaved as the child wrote them (stdout alone
    // from runCommandKeepingErrors).
    std::string output;

    // stderr, from runCommandKeepingErrors only.
    std::string errorOutput;

    // Why it failed, one line: could not launch, or the exit code.
    std::string error;

    // -1 when it did not launch. eacp::Processes reports a child killed by
    // signal N as 128 + N, as a shell does. (Upstream candidate: a signal
    // field in eacp's ProcessResult, which cannot tell the two apart.)
    int exitCode = -1;
};

// When a child's output reaches stdout as it arrives (Log::stream or
// Log::echo). The result keeps all of it, but only its last 64 KiB with
// `always`, whose output is on the screen already. (Upstream candidate: an
// eacp::Processes option to keep no output() while onOutput takes it, which
// would bound the child's buffer too.)
enum class Echo
{
    whenVerbose,
    always
};

// Runs a child to completion with stdout and stderr merged, in
// `workingDirectory`, or in this process's own when it is empty.
CommandResult runCommand(const std::string& executable,
                         const eacp::Vector<std::string>& arguments,
                         const eacp::FilePath& workingDirectory = {},
                         Echo echo = Echo::whenVerbose);

// As runCommand with Echo::always, but stderr is not merged: it comes back
// whole in errorOutput, for a child whose stderr is the failure it reports
// (epm configure's recipe executable).
CommandResult runCommandKeepingErrors(const std::string& executable,
                                      const eacp::Vector<std::string>& arguments,
                                      const eacp::FilePath& workingDirectory = {});

// Why `result` failed, one line, naming the signal that killed the child.
std::string failureReason(const std::string& executable,
                          const CommandResult& result);

// The last `count` lines of `text`, for a failure report that stays readable.
std::string lastLines(std::string_view text, int count);

// The first file called `name` (plus .exe on Windows) in a PATH directory
// that the user may execute.
std::optional<eacp::FilePath> findExecutable(std::string_view name);
} // namespace epm
