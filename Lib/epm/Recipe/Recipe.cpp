#include "Recipe.h"

#include <epm/Spec/Platform.h>

namespace epm
{
PackageDecl::PackageDecl(Project& projectToUse, int indexToUse, const Env& envToUse)
    : project(&projectToUse)
    , index(indexToUse)
    , env(&envToUse)
{
}

PackageDecl& PackageDecl::option(std::string key, std::string value)
{
    setOption(package(), std::move(key), std::move(value));
    return *this;
}

PackageDecl& PackageDecl::config(std::string config)
{
    addConfig(package(), std::move(config));
    return *this;
}

PackageDecl& PackageDecl::mode(std::string mode)
{
    setMode(package(), std::move(mode));
    return *this;
}

PackageDecl& PackageDecl::abiMatch(std::string abiMatch)
{
    setAbiMatch(package(), std::move(abiMatch));
    return *this;
}

PackageDecl& PackageDecl::releaseIfPossible()
{
    if (!msvcAbi(*env))
        addConfig(package(), "Release");

    return *this;
}

ManifestPackage& PackageDecl::package() const
{
    return project->packages[index];
}

PackageDecl Recipe::add(std::string name, std::string git, std::string tag)
{
    described.add(std::move(name), std::move(git), std::move(tag));
    return {described, described.packages.size() - 1, env()};
}

const Project& Recipe::project() const
{
    return described;
}

std::unique_ptr<Package> Recipe::createPackage(std::string_view name) const
{
    const auto found = factories.find(name);
    return found == factories.end() ? nullptr : found->second();
}
} // namespace epm
