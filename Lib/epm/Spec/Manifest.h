#pragma once

#include <eacp/Core/Utils/Containers.h>
#include <eacp/Core/Utils/FilePath.h>

#include <Miro/Json.h>
#include <Miro/Reflect.h>

#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace epm
{
// One entry of a manifest's "packages". Unset strings and lists are empty.
struct ManifestPackage
{
    // The find_package name; unique within one manifest.
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

    MIRO_REFLECT(name, git, tag, options, configs, mode, abiMatch)
};

// A profile's entry for one package: everything but where it comes from.
struct ProfilePackage
{
    std::string name;
    std::map<std::string, std::string> options;
    EA::Vector<std::string> configs;
    std::string mode;
    std::string abiMatch;

    MIRO_REFLECT(name, options, configs, mode, abiMatch)
};

// A named partial manifest, selected with EPM_PROFILE.
struct Profile
{
    EA::Vector<std::string> configs;
    std::string mode;
    std::string abiMatch;
    EA::Vector<ProfilePackage> packages;

    MIRO_REFLECT(configs, mode, abiMatch, packages)
};

// One entry of epm.json's "overrides": Go's `replace`. It replaces every
// requirement on the package, whoever declares it; empty fields keep what the
// requirements say. Only the root's overrides count.
struct ManifestOverride
{
    std::string url;
    std::string commit;
    std::string tag;

    MIRO_REFLECT(url, commit, tag)
};

// epm.json. `packages` is an array so that the file's
// order, which decides among dependencies, survives loading.
struct Manifest
{
    int schema = 1;
    EA::Vector<ManifestPackage> packages;

    // Each value is a profile object, or the path of a JSON file holding one
    // (relative to the manifest's directory).
    std::map<std::string, Miro::JSON> profiles;

    // Settles, by name, what minimal version selection cannot.
    std::map<std::string, ManifestOverride> overrides;

    MIRO_REFLECT(schema, packages, profiles, overrides)

    std::optional<ManifestOverride> findOverride(const std::string& name) const;
};

// https://github.com/<owner>/<repo>.git for an owner/repo shorthand: anything
// without "://", without a "git@" prefix and not starting with '/'. Any
// other spelling is returned as is.
std::string expandGitUrl(std::string_view git);

// The entry named `name` of a manifest's or a profile's packages; nullptr
// when there is none.
template <typename Package>
const Package* findPackage(const EA::Vector<Package>& packages,
                           std::string_view name)
{
    return packages.findIf([name](const Package& package)
                           { return package.name == name; });
}

struct ManifestLoad
{
    bool ok = false;
    Manifest manifest;
    std::string error;
};

// The manifest held by `json`: fails when it is not an object, when its
// "packages" (or an inline profile's) is not an array, or on what
// manifestError finds.
ManifestLoad manifestFromJSON(const Miro::JSON& json);

// manifestFromJSON of the file; fails when it is missing or malformed.
ManifestLoad loadManifest(const eacp::FilePath& path);

// The root's overrides: an empty manifest when the file does not exist;
// throws when it is malformed.
Manifest readRootManifest(const eacp::FilePath& path);

// The first problem of a parsed manifest: an empty or repeated package name,
// in its packages or in an inline profile's, or an inline profile that does
// not parse. nullopt when there is none.
std::optional<std::string> manifestError(const Manifest& manifest);

struct ProfileSelection
{
    bool ok = false;
    Profile profile;
    std::string error;
};

// The profile `name` of `manifest`, reading a path-valued profile relative
// to `manifestDirectory`. A name the manifest does not define is taken as the
// path of a profile file itself. An empty name selects an empty profile.
// Fails, as manifestFromJSON does, on a "packages" that is not an array and
// on an empty or repeated package name.
ProfileSelection selectProfile(const Manifest& manifest,
                               const std::string& name,
                               const eacp::FilePath& manifestDirectory);
} // namespace epm
