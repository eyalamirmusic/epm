#pragma once

#include <eacp/Core/Utils/FilePath.h>

#include <Miro/Reflect.h>

#include <map>
#include <optional>
#include <string>

namespace epm
{
// One entry of epm.json's overrides: Go's `replace`. Empty fields keep what
// the package's requirements say.
struct PackageOverride
{
    std::string url;
    std::string commit;
    std::string tag;

    MIRO_REFLECT(url, commit, tag)
};

// epm.json beside the top-level CMakeLists.txt. Only the root project's
// counts; a dependency's is never read.
struct Manifest
{
    std::map<std::string, PackageOverride> overrides;

    MIRO_REFLECT(overrides)

    std::optional<PackageOverride> findOverride(const std::string& name) const;
};

// An empty manifest when the file does not exist; throws when it is not JSON.
Manifest readManifest(const eacp::FilePath& path);
} // namespace epm
