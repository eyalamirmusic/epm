#include "CacheRoot.h"

#include <eacp/Core/Utils/Environment.h>
#include <eacp/Core/Utils/Files.h>

namespace epm
{
using namespace eacp;

CacheRoot::CacheRoot(FilePath rootToUse)
    : root(std::move(rootToUse))
{
}

CacheRoot CacheRoot::fromEnvironment()
{
    if (auto fromEnv = getEnvValue("EPM_CACHE"); !fromEnv.empty())
        return CacheRoot {FilePath {fromEnv}};

    return CacheRoot {FilePath::cacheDirectory() / "epm"};
}

const FilePath& CacheRoot::path() const
{
    return root;
}

FilePath CacheRoot::src() const
{
    return subdirectory("src");
}

FilePath CacheRoot::git() const
{
    return subdirectory("git");
}

FilePath CacheRoot::downloads() const
{
    return subdirectory("downloads");
}

FilePath CacheRoot::bin() const
{
    return subdirectory("bin");
}

FilePath CacheRoot::build() const
{
    return subdirectory("build");
}

FilePath CacheRoot::tmp() const
{
    return subdirectory("tmp");
}

FilePath CacheRoot::locks() const
{
    return subdirectory("locks");
}

FilePath CacheRoot::recipe() const
{
    return subdirectory("recipe");
}

FilePath CacheRoot::subdirectory(std::string_view name) const
{
    auto directory = root / name;
    Files::createDirectories(directory);
    return directory;
}
} // namespace epm
