#include "CacheRoot.h"

#include <epm/Util/Text.h>

#include <eacp/Core/Platform/Platform.h>
#include <eacp/Core/Utils/Environment.h>
#include <eacp/Core/Utils/Files.h>

namespace epm
{
using namespace eacp;

CacheRoot::CacheRoot(FilePath rootToUse, Vector<FilePath> storesToUse)
    : root(std::move(rootToUse))
    , storeRoots(std::move(storesToUse))
{
}

CacheRoot CacheRoot::fromEnvironment()
{
    if (auto fromEnv = getEnvValue("EPM_CACHE"); !fromEnv.empty())
        return CacheRoot {FilePath {fromEnv}, storesFromEnvironment()};

    return CacheRoot {FilePath::cacheDirectory() / "epm", storesFromEnvironment()};
}

Vector<FilePath> CacheRoot::storesFromEnvironment()
{
    return storeList(getEnvValue("EPM_CACHE_STORES"));
}

Vector<FilePath> CacheRoot::storeList(std::string_view text)
{
    constexpr auto separator = Platform::isWindows() ? ';' : ':';
    auto stores = Vector<FilePath> {};

    for (const auto& item: splitList(text, separator))
        stores.add(FilePath {item});

    return stores;
}

const FilePath& CacheRoot::path() const
{
    return root;
}

const Vector<FilePath>& CacheRoot::stores() const
{
    return storeRoots;
}

FilePath CacheRoot::src() const
{
    return subdirectory("src");
}

FilePath CacheRoot::bin() const
{
    return subdirectory("bin");
}

FilePath CacheRoot::build() const
{
    return subdirectory("build");
}

FilePath CacheRoot::cpm() const
{
    return subdirectory("cpm");
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
