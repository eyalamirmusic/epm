#include "Manifest.h"
#include "JsonFile.h"

#include <set>

namespace epm
{
using namespace eacp;

namespace
{
ProfileSelection selected(Profile profile)
{
    return {true, std::move(profile), {}};
}

ProfileSelection profileError(std::string error)
{
    return {false, {}, std::move(error)};
}

ManifestLoad manifestLoadError(std::string error)
{
    return {false, {}, std::move(error)};
}

std::string profileNames(const Manifest& manifest)
{
    auto names = std::string {};

    for (const auto& [name, value]: manifest.profiles)
        names += (names.empty() ? "" : ", ") + name;

    return names.empty() ? "none" : names;
}

// Miro reads an object where it expects an array as an empty array, so the
// name-keyed shape of earlier manifests would otherwise declare nothing.
bool packagesIsArray(const Miro::JSON& json)
{
    const auto* packages = Miro::Json::find(json.asObject(), "packages");
    return packages == nullptr || packages->isArray();
}

std::string notAnArray(const std::string& where)
{
    return where + " must be an array of package objects, each with a name";
}

std::string profilePackages(const std::string& name)
{
    return "the packages of profile " + name;
}

template <typename Package>
std::optional<std::string> packageNamesError(const Vector<Package>& packages,
                                             const std::string& where)
{
    auto seen = std::set<std::string> {};

    for (const auto& package: packages)
    {
        if (package.name.empty())
            return "a package in " + where + " has no name";

        if (!seen.insert(package.name).second)
            return package.name + " is declared twice in " + where;
    }

    return std::nullopt;
}

ProfileSelection profileFromObject(const std::string& name, const Miro::JSON& json)
{
    if (!packagesIsArray(json))
        return profileError(notAnArray(profilePackages(name)));

    auto profile = Profile {};

    try
    {
        profile = Miro::createFromJSON<Profile>(json);
    }
    catch (const std::exception& error)
    {
        return profileError("profile " + name + ": " + error.what());
    }

    if (auto problem = packageNamesError(profile.packages, profilePackages(name)))
        return profileError(*problem);

    return selected(std::move(profile));
}

ProfileSelection profileFromFile(const std::string& name, const FilePath& path)
{
    const auto json = loadJsonObject(path);

    if (!json)
        return profileError("profile " + name + ": cannot read " + path.str());

    return profileFromObject(name, *json);
}
} // namespace

std::string expandGitUrl(std::string_view git)
{
    const auto isShorthand = git.find("://") == std::string_view::npos
                             && !git.starts_with("git@") && !git.starts_with('/');

    if (!isShorthand || git.empty())
        return std::string {git};

    return "https://github.com/" + std::string {git} + ".git";
}

std::optional<std::string> manifestError(const Manifest& manifest)
{
    if (auto problem = packageNamesError(manifest.packages, "packages"))
        return problem;

    for (const auto& [name, value]: manifest.profiles)
    {
        if (!value.isObject())
            continue;

        if (const auto profile = profileFromObject(name, value); !profile.ok)
            return profile.error;
    }

    return std::nullopt;
}

ManifestLoad manifestFromJSON(const Miro::JSON& json)
{
    if (!json.isObject())
        return manifestLoadError("not a JSON object");

    if (!packagesIsArray(json))
        return manifestLoadError(notAnArray("packages"));

    auto manifest = Manifest {};

    try
    {
        manifest = Miro::createFromJSON<Manifest>(json);
    }
    catch (const std::exception& error)
    {
        return manifestLoadError(error.what());
    }

    if (auto problem = manifestError(manifest))
        return manifestLoadError(*problem);

    return {true, std::move(manifest), {}};
}

ManifestLoad loadManifest(const FilePath& path)
{
    const auto json = loadJsonObject(path);

    if (!json)
        return manifestLoadError("missing, or not a JSON object");

    return manifestFromJSON(*json);
}

ProfileSelection selectProfile(const Manifest& manifest,
                               const std::string& name,
                               const FilePath& manifestDirectory)
{
    if (name.empty())
        return selected({});

    const auto relativeToManifest = [&](const std::string& path)
    {
        const auto file = FilePath {path};
        return file.isAbsolute() ? file : manifestDirectory / path;
    };

    const auto found = manifest.profiles.find(name);

    if (found == manifest.profiles.end())
    {
        const auto file = relativeToManifest(name);

        if (!Files::isFile(file))
            return profileError("unknown profile '" + name
                                + "' (the manifest defines " + profileNames(manifest)
                                + ")");

        return profileFromFile(name, file);
    }

    const auto& value = found->second;

    if (value.isString())
        return profileFromFile(name, relativeToManifest(value.asString()));

    if (!value.isObject())
        return profileError("profile " + name
                            + " is neither an object nor a file path");

    return profileFromObject(name, value);
}
} // namespace epm
