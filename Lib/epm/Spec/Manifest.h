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
    eacp::Vector<std::string> configs;

    // "auto", "prebuilt", "binary-only" or "source".
    std::string mode;

    // "strict", "compatible" or "any".
    std::string abiMatch;

    // Declared with Recipe::add<T>: the recipe has build steps of its own for
    // the package (`epm-recipe build --package <name>`).
    bool customBuild = false;

    MIRO_REFLECT(name, git, tag, options, configs, mode, abiMatch, customBuild)
};

// Whether `git` names a directory rather than a remote: an absolute POSIX
// path, or a Windows one with a drive letter (C:/..., C:\...).
bool isLocalRepository(std::string_view git);

// https://github.com/<owner>/<repo>.git for an owner/repo shorthand: anything
// without "://", without a "git@" prefix and not a local path. Any other
// spelling is returned as is.
std::string expandGitUrl(std::string_view git);

// One spelling per repository, whatever way a machine reaches it: scheme,
// user, a trailing slash and ".git" dropped, scp-like "host:path" as
// "host/path", lower case, so https, ssh and git URLs of one repository give
// "github.com/owner/repo". A local path (or file:// URL) is made absolute and
// resolved, keeping its case: local repositories are machine-specific anyway.
// It names a repository in the graph's notes and in the sourceId; fetching
// always uses the URL as written.
std::string repositoryKey(std::string_view url);

// The entry named `name` of `packages`; nullptr when there is none.
template <typename Package>
const Package* findPackage(const eacp::Vector<Package>& packages,
                           std::string_view name)
{
    return packages.findIf([name](const Package& package)
                           { return package.name == name; });
}
} // namespace epm
