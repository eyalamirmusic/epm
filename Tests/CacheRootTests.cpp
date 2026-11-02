#include <epm/Cache/CacheRoot.h>

#include <eacp/Core/Utils/Environment.h>
#include <eacp/Core/Utils/Files.h>

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace eacp;
using epm::CacheRoot;

namespace
{
// Puts EPM_CACHE back the way it was found.
class ScopedEnv
{
public:
    ScopedEnv(std::string nameToUse, std::string_view value)
        : name(std::move(nameToUse))
        , previous(getEnv(name))
    {
        setEnv(name, value);
    }

    ~ScopedEnv()
    {
        if (previous)
            setEnv(name, *previous);
        else
            unsetEnv(name);
    }

private:
    std::string name;
    std::optional<std::string> previous;
};

FilePath scratch()
{
    auto directory = FilePath::tempDirectory() / "epm-cache-root-test";
    Files::removeAll(directory);
    return directory;
}
} // namespace

auto tHonoursEnv = test("CacheRoot/honoursEpmCache") = []
{
    const auto directory = scratch();
    auto env = ScopedEnv {"EPM_CACHE", directory.str()};

    check(CacheRoot::fromEnvironment().path() == directory);
};

auto tDefault = test("CacheRoot/defaultsToPlatformCache") = []
{
    auto env = ScopedEnv {"EPM_CACHE", ""};

    check(CacheRoot::fromEnvironment().path() == FilePath::cacheDirectory() / "epm");
};

auto tCreatesOnDemand = test("CacheRoot/createsSubdirectoriesOnDemand") = []
{
    const auto directory = scratch();
    const auto cache = CacheRoot {directory};

    check(!Files::exists(directory));

    check(cache.src() == directory / "src");
    check(cache.bin() == directory / "bin");
    check(cache.build() == directory / "build");
    check(cache.cpm() == directory / "cpm");
    check(cache.tmp() == directory / "tmp");
    check(cache.locks() == directory / "locks");

    for (auto name: {"src", "bin", "build", "cpm", "tmp", "locks"})
        check(Files::isDirectory(directory / name));

    Files::removeAll(directory);
};
