#pragma once

#include <eacp/Core/Utils/Containers.h>

#include <Miro/Reflect.h>

#include <map>
#include <string>
#include <string_view>

namespace epm
{
// One dependency an epm.cpp declares (Recipe/Project.h). Unset strings and
// lists are empty.
struct ManifestPackage
{
    // The find_package name; unique within one project.
    std::string name;

    // owner/repo (GitHub), an https or scp-like URL, or an absolute path.
    std::string git;
    std::string tag;
    std::map<std::string, std::string> options;
    EA::Vector<std::string> configs;

    // "auto", "prebuilt", "binary-only" or "source".
    std::string mode;

    // "strict", "compatible" or "any".
    std::string abiMatch;

    // Found by CMake's own find_package search, never fetched: the explicit
    // opt-in a find_package of a name outside the graph needs.
    bool system = false;

    MIRO_REFLECT(name, git, tag, options, configs, mode, abiMatch, system)
};

// https://github.com/<owner>/<repo>.git for an owner/repo shorthand: anything
// without "://", without a "git@" prefix and not starting with '/'. Any
// other spelling is returned as is.
std::string expandGitUrl(std::string_view git);

// The entry named `name` of `packages`; nullptr when there is none.
template <typename Package>
const Package* findPackage(const EA::Vector<Package>& packages,
                           std::string_view name)
{
    return packages.findIf([name](const Package& package)
                           { return package.name == name; });
}
} // namespace epm
