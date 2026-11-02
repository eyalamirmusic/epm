#include "Git.h"
#include "Text.h"

#include <eacp/Core/Process/Process.h>

namespace epm::Git
{
using namespace eacp;

namespace
{
// No terminal prompt, no GUI askpass, ssh in batch mode: git runs with nobody
// to answer, so anything that would ask must fail instead.
Vector<Processes::EnvironmentVariable> nonInteractiveEnvironment()
{
    using Variable = Processes::EnvironmentVariable;

    auto environment = Vector<Variable> {};

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
} // namespace

Result run(const FilePath& workingDirectory, const Vector<std::string>& arguments)
{
    auto options = Processes::ProcessOptions {};

    options.executable = "git";
    options.workingDirectory = workingDirectory.str();
    options.environment = nonInteractiveEnvironment();
    options.arguments.add("--no-optional-locks");

    for (const auto& argument: arguments)
        options.arguments.add(argument);

    const auto result = Processes::run(std::move(options));

    return {result.launched && result.exited && result.exitCode == 0,
            result.output,
            result.launched ? result.errorOutput
                            : std::string {"could not run git - is it installed?"}};
}

bool isAvailable()
{
    static const auto available = run({}, {"--version"}).ok;
    return available;
}

Vector<RemoteRef> parseLsRemote(std::string_view output)
{
    auto refs = Vector<RemoteRef> {};

    for (const auto line: splitLines(output))
    {
        const auto tab = line.find('\t');

        if (tab == std::string_view::npos || tab == 0)
            continue;

        refs.add(
            {std::string {line.substr(tab + 1)}, std::string {line.substr(0, tab)}});
    }

    return refs;
}

RemoteRefs lsRemote(const std::string& url)
{
    auto refs = RemoteRefs {};

    // No working directory: a checkout's config must not rewrite the URL asked.
    const auto result = run({}, {"ls-remote", url});

    if (!result.ok)
    {
        refs.error = firstLine(result.message());

        if (refs.error.empty())
            refs.error = "git ls-remote failed for " + url;

        return refs;
    }

    refs.refs = parseLsRemote(result.output);
    refs.ok = true;

    return refs;
}
} // namespace epm::Git
