#include <DependencyResolution/Source/Submodules.h>

namespace DependencyResolution
{
namespace
{
constexpr auto parentPrefix = std::string_view {"../"};
constexpr auto currentPrefix = std::string_view {"./"};

std::string removeLastComponent(std::string url)
{
    while (!url.empty() && url.back() == '/')
        url.pop_back();

    auto separator = url.find_last_of("/:");

    if (separator == std::string::npos)
        return {};

    return url.substr(0, separator);
}
} // namespace

bool isSameSelection(const SubmoduleSelection& first,
                     const SubmoduleSelection& second)
{
    return first.isRestricted == second.isRestricted && first.paths == second.paths
           && first.isRecursive == second.isRecursive;
}

bool isSameSubmodule(const LockedSubmodule& first, const LockedSubmodule& second)
{
    return first.path == second.path && first.url == second.url
           && first.commit == second.commit;
}

std::string resolveSubmoduleUrl(const std::string& parentUrl, const std::string& url)
{
    if (!url.starts_with(parentPrefix) && !url.starts_with(currentPrefix))
        return url;

    auto base = parentUrl;

    while (!base.empty() && base.back() == '/')
        base.pop_back();

    auto rest = std::string_view {url};

    while (true)
    {
        if (rest.starts_with(parentPrefix))
        {
            base = removeLastComponent(base);
            rest.remove_prefix(parentPrefix.size());
        }
        else if (rest.starts_with(currentPrefix))
        {
            rest.remove_prefix(currentPrefix.size());
        }
        else
        {
            break;
        }
    }

    auto separator = std::string {"/"};

    if (!base.empty() && base.back() == ':')
        separator.clear();

    return base + separator + std::string {rest};
}
} // namespace DependencyResolution
