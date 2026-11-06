#include "Manifest.h"

#include <epm/Util/Paths.h>

#include <eacp/Core/Utils/Strings.h>

#include <cctype>

namespace epm
{
namespace
{
bool startsWithDriveLetter(std::string_view path)
{
    return path.size() >= 2 && std::isalpha(static_cast<unsigned char>(path[0]))
           && path[1] == ':';
}

// file:///srv/x as /srv/x, file:///C:/x as C:/x.
std::string withoutFileScheme(std::string_view url)
{
    auto path = url.substr(std::string_view {"file://"}.size());

    if (path.starts_with('/') && startsWithDriveLetter(path.substr(1)))
        path.remove_prefix(1);

    return std::string {path};
}

// Resolved only when absolute on this platform: a drive-letter path seen
// elsewhere stays as written.
std::string localRepositoryKey(const std::string& path)
{
    const auto filePath = eacp::FilePath {path};

    if (!filePath.isAbsolute())
        return path;

    return resolvedPath(filePath).str();
}

std::string remoteRepositoryKey(std::string key)
{
    const auto scheme = key.find("://");

    if (scheme != std::string::npos)
        key = key.substr(scheme + 3);
    else if (const auto colon = key.find(':'); colon != std::string::npos)
        key[colon] = '/';

    if (const auto at = key.find('@'); at != std::string::npos && at < key.find('/'))
        key = key.substr(at + 1);

    return eacp::Strings::toLower(key);
}
} // namespace

bool isLocalRepository(std::string_view git)
{
    if (git.starts_with('/'))
        return true;

    return git.size() >= 3 && startsWithDriveLetter(git)
           && (git[2] == '/' || git[2] == '\\');
}

std::string expandGitUrl(std::string_view git)
{
    const auto isShorthand = git.find("://") == std::string_view::npos
                             && !git.starts_with("git@") && !isLocalRepository(git);

    if (!isShorthand || git.empty())
        return std::string {git};

    return "https://github.com/" + std::string {git} + ".git";
}

std::string repositoryKey(std::string_view url)
{
    const auto path =
        url.starts_with("file://") ? withoutFileScheme(url) : std::string {url};

    auto key = isLocalRepository(path) ? localRepositoryKey(path)
                                       : remoteRepositoryKey(path);

    while (key.ends_with('/'))
        key.pop_back();

    if (key.ends_with(".git"))
        key.resize(key.size() - 4);

    return key;
}
} // namespace epm
