#include "Command.h"
#include "Log.h"

#include <eacp/Core/Platform/Platform.h>
#include <eacp/Core/Process/Process.h>
#include <eacp/Core/Utils/Environment.h>
#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/StdPath.h>

#include <cstdio>
#include <filesystem>

namespace epm
{
using namespace eacp;

namespace
{
// What runCommand keeps of output it echoed, which is on the screen already:
// enough for a failure report's last lines.
constexpr auto echoedOutputKept = std::size_t {64 * 1024};

// The end of `text`, at most `limit` bytes, from a line start.
std::string keptTail(std::string_view text, std::size_t limit)
{
    if (text.size() <= limit)
        return std::string {text};

    auto tail = text.substr(text.size() - limit);

    if (const auto newline = tail.find('\n'); newline != std::string_view::npos)
        tail.remove_prefix(newline + 1);

    return std::string {tail};
}

Processes::ProcessOptions processOptions(const std::string& executable,
                                         const Vector<std::string>& arguments,
                                         const FilePath& workingDirectory)
{
    auto options = Processes::ProcessOptions {};
    options.executable = executable;
    options.arguments = arguments;
    options.workingDirectory = workingDirectory.str();
    return options;
}

CommandResult commandResultFor(const std::string& executable,
                               const Processes::ProcessResult& result,
                               std::string output)
{
    auto commandResult = CommandResult {};
    commandResult.output = std::move(output);
    commandResult.errorOutput = result.errorOutput;
    commandResult.exitCode = result.launched ? result.exitCode : -1;

    if (!result.launched)
        commandResult.error = "could not run " + executable;
    else if (result.exitCode != 0)
        commandResult.error =
            executable + " exited with code " + std::to_string(result.exitCode);
    else
        commandResult.ok = true;

    return commandResult;
}
} // namespace

CommandResult runCommand(const std::string& executable,
                         const Vector<std::string>& arguments,
                         const FilePath& workingDirectory,
                         Echo echo)
{
    auto options = processOptions(executable, arguments, workingDirectory);
    options.mergeErrorOutput = true;

    if (echo == Echo::always)
        options.onOutput = [](std::string_view chunk) { Log::echo(chunk); };
    else if (Log::isVerbose())
        options.onOutput = [](std::string_view chunk) { Log::stream(chunk); };

    const auto result = Processes::run(std::move(options));
    auto output = echo == Echo::always ? keptTail(result.output, echoedOutputKept)
                                       : result.output;
    return commandResultFor(executable, result, std::move(output));
}

CommandResult runCommandKeepingErrors(const std::string& executable,
                                      const Vector<std::string>& arguments,
                                      const FilePath& workingDirectory)
{
    auto options = processOptions(executable, arguments, workingDirectory);
    options.onOutput = [](std::string_view chunk) { Log::echo(chunk); };

    const auto result = Processes::run(std::move(options));
    return commandResultFor(
        executable, result, keptTail(result.output, echoedOutputKept));
}

// Above 128 is how eacp::Processes reports a signal (or a shell's 128 + N).
// On Windows a process that crashed (an abort, an access violation) exits
// with an NT status, 0xC0000000 and up.
std::string failureReason(const std::string& executable, const CommandResult& result)
{
    constexpr auto signalBase = 128;
    constexpr auto errorStatus = 0xC0000000u;

    if (Platform::isWindows())
    {
        const auto status = static_cast<unsigned>(result.exitCode);

        if (status < errorStatus)
            return result.error;

        char text[16];
        std::snprintf(text, sizeof text, "0x%08X", status);
        return executable + " crashed (exit status " + text + ")";
    }

    if (result.exitCode > signalBase)
        return executable + " was terminated by signal "
               + std::to_string(result.exitCode - signalBase) + " (exit code "
               + std::to_string(result.exitCode) + ")";

    return result.error;
}

std::string lastLines(std::string_view text, int count)
{
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
        text.remove_suffix(1);

    auto start = text.size();

    for (auto found = 0; found < count && start > 0; ++found)
    {
        const auto newline = text.rfind('\n', start - 1);

        if (newline == std::string_view::npos)
        {
            start = 0;
            break;
        }

        start = newline;
    }

    if (start > 0)
        ++start;

    return std::string {text.substr(start)};
}

std::optional<FilePath> findExecutable(std::string_view name)
{
    constexpr auto separator = Platform::isWindows() ? ';' : ':';
    const auto fileName =
        std::string {name} + (Platform::isWindows() ? ".exe" : std::string {});

    const auto pathText = getEnvValue("PATH");
    auto path = std::string_view {pathText};

    while (!path.empty())
    {
        const auto end = path.find(separator);
        const auto directory = path.substr(0, end);

        if (!directory.empty())
        {
            const auto candidate = FilePath {directory} / fileName;
            auto failure = std::error_code {};
            const auto permissions =
                std::filesystem::status(toStdPath(candidate), failure).permissions();

            using std::filesystem::perms;
            const auto executableBits =
                perms::owner_exec | perms::group_exec | perms::others_exec;

            if (!failure && Files::isFile(candidate)
                && (Platform::isWindows()
                    || (permissions & executableBits) != perms::none))
                return candidate;
        }

        if (end == std::string_view::npos)
            break;

        path.remove_prefix(end + 1);
    }

    return std::nullopt;
}
} // namespace epm
