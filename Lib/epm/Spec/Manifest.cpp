#include <epm/Spec/Manifest.h>
#include <epm/Spec/JsonFile.h>

#include <eacp/Core/Utils/Files.h>

#include <stdexcept>

namespace epm
{
std::optional<PackageOverride> Manifest::findOverride(const std::string& name) const
{
    if (auto found = overrides.find(name); found != overrides.end())
        return found->second;

    return std::nullopt;
}

Manifest readManifest(const eacp::FilePath& path)
{
    if (path.empty() || !eacp::Files::isFile(path))
        return {};

    auto manifest = loadJsonFile<Manifest>(path);

    if (!manifest)
        throw std::runtime_error(path.str() + " does not hold a JSON object");

    return *manifest;
}
} // namespace epm
