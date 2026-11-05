#include "Manifest.h"

#include <cctype>

namespace epm
{
bool isLocalRepository(std::string_view git)
{
    if (git.starts_with('/'))
        return true;

    return git.size() >= 3 && std::isalpha(static_cast<unsigned char>(git[0]))
           && git[1] == ':' && (git[2] == '/' || git[2] == '\\');
}

std::string expandGitUrl(std::string_view git)
{
    const auto isShorthand = git.find("://") == std::string_view::npos
                             && !git.starts_with("git@") && !isLocalRepository(git);

    if (!isShorthand || git.empty())
        return std::string {git};

    return "https://github.com/" + std::string {git} + ".git";
}
} // namespace epm
