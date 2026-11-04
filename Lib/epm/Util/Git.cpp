#include "Git.h"
#include "Text.h"

#include <eacp/Core/Platform/Platform.h>

namespace epm::Git
{
using namespace eacp;

namespace
{
// No terminal prompt, no GUI askpass, ssh in batch mode: git runs with nobody
// to answer, so anything that would ask must fail instead.
EA::Vector<Processes::EnvironmentVariable> nonInteractiveEnvironment()
{
    using Variable = Processes::EnvironmentVariable;

    auto environment = EA::Vector<Variable> {};

    // Spelled out rather than braced: `{"a", "b"}` is ambiguous between one
    // variable and an initializer_list of two.
    environment.add(Variable {"GIT_TERMINAL_PROMPT", "0"});
    environment.add(Variable {"GIT_ASKPASS", ""});
    environment.add(Variable {"SSH_ASKPASS", ""});
    environment.add(Variable {"GIT_SSH_COMMAND", "ssh -oBatchMode=yes"});
    environment.add(Variable {"GIT_CONFIG_PARAMETERS", "'color.ui=never'"});

    return environment;
}

std::string firstLine(const std::string& text)
{
    return text.substr(0, text.find('\n'));
}

// Git for Windows refuses checkouts past MAX_PATH unless core.longpaths is set.
EA::Vector<std::string> platformArguments(const EA::Vector<std::string>& arguments)
{
    if (!Platform::isWindows())
        return arguments;

    auto withLongPaths = EA::Vector<std::string> {"-c", "core.longpaths=true"};
    withLongPaths.addFrom(arguments);
    return withLongPaths;
}
} // namespace

Result run(const EA::Vector<std::string>& arguments,
           const EA::Vector<Processes::EnvironmentVariable>& environment)
{
    auto options = Processes::ProcessOptions {};

    options.executable = "git";
    options.environment = nonInteractiveEnvironment();
    options.environment.addFrom(environment);

    // Never the project's own directory: git would read its .git first, and a
    // worktree whose .git points somewhere missing stops every command.
    options.workingDirectory = FilePath::tempDirectory().str();
    options.arguments.add("--no-optional-locks");
    options.arguments.addFrom(platformArguments(arguments));

    const auto result = Processes::run(std::move(options));

    return {result.launched && result.exited && result.exitCode == 0,
            result.output,
            result.launched ? result.errorOutput
                            : std::string {"could not run git - is it installed?"}};
}

bool isAvailable()
{
    static const auto available = run({"--version"}).ok;
    return available;
}

} // namespace epm::Git
