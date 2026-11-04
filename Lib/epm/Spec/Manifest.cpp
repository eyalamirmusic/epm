#include "Manifest.h"

namespace epm
{
std::string expandGitUrl(std::string_view git)
{
    const auto isShorthand = git.find("://") == std::string_view::npos
                             && !git.starts_with("git@") && !git.starts_with('/');

    if (!isShorthand || git.empty())
        return std::string {git};

    return "https://github.com/" + std::string {git} + ".git";
}
} // namespace epm
