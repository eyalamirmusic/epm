#include "Git.h"
#include "Text.h"

#include <eacp/Core/Platform/Platform.h>
#include <eacp/Core/Utils/Strings.h>

#include <stdexcept>

namespace DependencyResolution::Git
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

std::string runOrThrow(const Vector<std::string>& arguments,
                       const Vector<Processes::EnvironmentVariable>& environment)
{
    auto result = run(arguments, environment);

    if (!result.ok)
        throw std::runtime_error("git " + Strings::join(arguments, " ")
                                 + " failed:\n" + Strings::trim(result.error));

    return Strings::trim(result.output);
}

bool isLfsInstalled()
{
    return run({"lfs", "version"}).ok;
}

FilePath findPatchBesideGit()
{
    // Git for Windows ships patch.exe in usr/bin beside libexec/git-core, which
    // is where CPM looks too.
    constexpr auto levelsAboveGitInstall = 3;
    auto execPath = run({"--exec-path"});

    if (!execPath.ok)
        return {};

    auto install = FilePath {Strings::trim(execPath.output)};

    for (auto level = 0; level < levelsAboveGitInstall; ++level)
        install = install.parentDirectory();

    return install / "usr" / "bin" / "patch.exe";
}
} // namespace DependencyResolution::Git
