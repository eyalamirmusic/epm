#pragma once

#include <eacp/Core/Process/Process.h>
#include <eacp/Core/Utils/Containers.h>
#include <eacp/Core/Utils/FilePath.h>

#include <string>
#include <string_view>

namespace epm::Git
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

bool isAvailable();

struct RemoteRef
{
    std::string ref;
    std::string sha;
};

struct RemoteRefs
{
    bool ok = false;
    EA::Vector<RemoteRef> refs;
    std::string error;
};

// Parses `git ls-remote <url>` output, in the order git printed it.
EA::Vector<RemoteRef> parseLsRemote(std::string_view output);

RemoteRefs lsRemote(const std::string& url);
} // namespace epm::Git
