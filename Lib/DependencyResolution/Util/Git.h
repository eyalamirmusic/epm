#pragma once

#include <eacp/Core/Process/Process.h>
#include <eacp/Core/Utils/Containers.h>
#include <eacp/Core/Utils/FilePath.h>

#include <string>
#include <string_view>

namespace DependencyResolution::Git
{
struct Result
{
    bool ok = false;
    std::string output;
    std::string error;

    const std::string& message() const
    {
        return ok ? output : (error.empty() ? output : error);
    }
};

// Runs git from the temp directory and waits. Never prompts for credentials:
// a remote that needs them fails instead of hanging.
Result run(const EA::Vector<std::string>& arguments,
           const EA::Vector<eacp::Processes::EnvironmentVariable>& environment = {});

// run's trimmed stdout; throws, with git's stderr, when git fails.
std::string runOrThrow(
    const EA::Vector<std::string>& arguments,
    const EA::Vector<eacp::Processes::EnvironmentVariable>& environment = {});

// Whether git-lfs is installed, so `git lfs` runs.
bool isLfsInstalled();

// The patch.exe Git for Windows installs beside itself, or an empty path when
// git is missing.
eacp::FilePath findPatchBesideGit();

} // namespace DependencyResolution::Git
