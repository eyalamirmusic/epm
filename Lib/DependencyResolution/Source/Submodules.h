#pragma once

#include <Miro/Reflect.h>

#include <string>
#include <eacp/Core/Utils/Containers.h>

namespace DependencyResolution
{
// Which submodules a git package checks out: every one by default, as CPM
// does, or only `paths` when GIT_SUBMODULES named them (none when it was
// given empty).
struct SubmoduleSelection
{
    bool isRestricted = false;
    EA::Vector<std::string> paths;
    bool isRecursive = true;

    MIRO_REFLECT(isRestricted, paths, isRecursive)
};

struct LockedSubmodule
{
    std::string path;
    std::string url;
    std::string commit;

    MIRO_REFLECT(path, url, commit)
};

bool isSameSelection(const SubmoduleSelection& first,
                     const SubmoduleSelection& second);
bool isSameSubmodule(const LockedSubmodule& first, const LockedSubmodule& second);

// A .gitmodules URL made absolute: "../b.git" or "./b" resolved against the
// URL of the repository that lists it, as git does.
std::string resolveSubmoduleUrl(const std::string& parentUrl,
                                const std::string& url);
} // namespace DependencyResolution
