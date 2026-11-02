#include "Command.h"
#include "Log.h"

#include <eacp/Core/Platform/Platform.h>
#include <eacp/Core/Process/Process.h>
#include <eacp/Core/Utils/Environment.h>
#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/StdPath.h>

#include <filesystem>

namespace epm
{
using namespace eacp;

CommandResult runCommand(const std::string& executable,
                         const Vector<std::string>& arguments)
{
    auto options = Processes::ProcessOptions {};
    options.executable = executable;
    options.arguments = arguments;
    options.mergeErrorOutput = true;

    if (Log::isVerbose())
        options.onOutput = [](std::string_view chunk) { Log::stream(chunk); };

    const auto result = Processes::run(std::move(options));

    auto commandResult = CommandResult {};
    commandResult.output = result.output;

    if (!result.launched)
        commandResult.error = "could not run " + executable;
    else if (!result.exited)
        commandResult.error = executable + " did not exit normally";
    else if (result.exitCode != 0)
        commandResult.error =
            executable + " exited with code " + std::to_string(result.exitCode);
    else
        commandResult.ok = true;

    return commandResult;
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
