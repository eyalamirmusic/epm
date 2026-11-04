#include "Project.h"

#include <set>

namespace epm
{
ManifestPackage& Project::add(std::string name, std::string git, std::string tag)
{
    auto& package = packages.create();
    package.name = std::move(name);
    package.git = std::move(git);
    package.tag = std::move(tag);
    return package;
}

ManifestPackage&
    setOption(ManifestPackage& package, std::string key, std::string value)
{
    package.options[std::move(key)] = std::move(value);
    return package;
}

ManifestPackage& addConfig(ManifestPackage& package, std::string config)
{
    package.configs.add(std::move(config));
    return package;
}

ManifestPackage& setMode(ManifestPackage& package, std::string mode)
{
    package.mode = std::move(mode);
    return package;
}

ManifestPackage& setAbiMatch(ManifestPackage& package, std::string abiMatch)
{
    package.abiMatch = std::move(abiMatch);
    return package;
}

std::optional<std::string> projectError(const Project& project)
{
    auto seen = std::set<std::string> {};

    for (const auto& package: project.packages)
    {
        if (package.name.empty())
            return "a package in packages has no name";

        if (!seen.insert(package.name).second)
            return package.name + " is declared twice in packages";
    }

    return std::nullopt;
}
} // namespace epm
