#include "TestDirectory.h"

#include <epm/Cache/BinaryCache.h>
#include <epm/Lock/LockFile.h>
#include <epm/Spec/JsonFile.h>

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace epm;

namespace
{
Env keyEnv()
{
    auto env = Env {};
    env.cxxCompiler = "/usr/bin/c++";
    env.cxxCompilerId = "AppleClang";
    env.cxxCompilerVersion = "17.0.0.17000013";
    env.systemName = "Darwin";
    env.systemProcessor = "arm64";
    env.osxDeploymentTarget = "11.0";
    env.cxxStandard = "20";
    return env;
}

Request keyRequest()
{
    auto request = Request {};
    request.name = "eacp";
    request.gitUrl = "https://github.com/eyalamirmusic/eacp.git";
    request.gitRef = "main";
    return request;
}

std::string
    keyWith(std::initializer_list<std::pair<std::string, std::string>> options)
{
    auto request = keyRequest();

    for (const auto& [name, value]: options)
        request.options[name] = value;

    return binaryKey(makeManifest(request, keyEnv(), "abc123"));
}
} // namespace

auto tKeyOptionOrder = test("BinaryCache/key/optionOrderDoesNotMatter") = []
{
    const auto forward = keyWith({{"EACP_BUILD_GRAPHICS", "OFF"},
                                  {"EACP_ENABLE_TESTS", "OFF"},
                                  {"A_FIRST", "ON"}});
    const auto backward = keyWith({{"A_FIRST", "ON"},
                                   {"EACP_ENABLE_TESTS", "OFF"},
                                   {"EACP_BUILD_GRAPHICS", "OFF"}});

    check(forward == backward);
    check(forward.size() == 64);
};

auto tKeyOptionValue = test("BinaryCache/key/changedOptionChangesKey") = []
{
    check(keyWith({{"EACP_ENABLE_TESTS", "OFF"}})
          != keyWith({{"EACP_ENABLE_TESTS", "ON"}}));
    check(keyWith({}) != keyWith({{"EACP_ENABLE_TESTS", "OFF"}}));
};

auto tKeyToolchain = test("BinaryCache/key/toolchainChangesKey") = []
{
    const auto request = keyRequest();
    auto other = keyEnv();
    other.cxxCompilerVersion = "18.0.0";

    check(binaryKey(makeManifest(request, keyEnv(), "abc123"))
          != binaryKey(makeManifest(request, other, "abc123")));
    check(binaryKey(makeManifest(request, keyEnv(), "abc123"))
          != binaryKey(makeManifest(request, keyEnv(), "def456")));
};

auto tKeyIgnoresMode = test("BinaryCache/key/ignoresModeAndRef") = []
{
    auto request = keyRequest();
    const auto before = binaryKey(makeManifest(request, keyEnv(), "abc123"));

    request.mode = "prebuilt";
    request.gitRef = "v1.0";
    request.buildMissingBinaries = false;

    check(binaryKey(makeManifest(request, keyEnv(), "abc123")) == before);
};

auto tCanonicalForm = test("BinaryCache/key/canonicalFormIsCompactSorted") = []
{
    auto request = keyRequest();
    request.options = {{"B", "2"}, {"A", "1"}};

    const auto text = canonicalManifest(makeManifest(request, keyEnv(), "abc123"));

    check(text.find('\n') == std::string::npos);
    check(text.find("\"options\":{\"A\":\"1\",\"B\":\"2\"}") != std::string::npos);
    check(text.find("\"configs\":[\"Debug\",\"Release\"]") != std::string::npos);
};

auto tKeySourceHash = test("BinaryCache/key/sourceHashChangesKey") = []
{
    const auto request = keyRequest();
    const auto manifest = makeManifest(request, keyEnv(), "abc123");

    check(manifest.schema == 2);
    check(manifest.sourceHash == "abc123");
    check(binaryKey(manifest)
          != binaryKey(makeManifest(request, keyEnv(), "abc124")));
};

namespace BinaryCacheTests
{
constexpr auto packageName = "eacp";
constexpr auto dependencyName = "Miro";
constexpr auto builtDependencyHash = "1111";
constexpr auto changedDependencyHash = "2222";

LockedPackage createLockedDependency(const std::string& hash)
{
    auto package = LockedPackage {};
    package.name = dependencyName;
    package.hash = hash;
    return package;
}

eacp::FilePath writeLockWithDependencyHash(const eacp::FilePath& directory,
                                           const std::string& hash)
{
    const auto path = directory / lockFileName;
    auto lock = LockFile {};
    lock.storePackage(createLockedDependency(hash));
    writeLockFile(path, lock);
    return path;
}

std::string writeMarkerBuiltWithDependency(const CacheRoot& cache)
{
    const auto key = binaryKey(makeManifest(keyRequest(), keyEnv(), "abc123"));
    auto marker = BinaryMarker {};
    marker.key = key;
    marker.dependencies[dependencyName] = builtDependencyHash;
    saveJsonFile(cache.bin() / packageName / (key.substr(0, 12) + ".json"), marker);
    return key;
}

ChildBuild createLockedChildBuild()
{
    auto build = ChildBuild {};
    build.request = keyRequest();
    build.env = keyEnv();
    build.env.module = "/epm/share/epm/cmake/epm.cmake";
    build.sourceDirectory = eacp::FilePath {"/c/src/eacp"};
    build.buildDirectory = eacp::FilePath {"/c/build/eacp/Debug"};
    build.prefix = eacp::FilePath {"/c/bin/eacp/f685faf3d9f4"};
    build.cacheRoot = eacp::FilePath {"/c"};
    build.childLock = {eacp::FilePath {"/project/epm.lock"}, "offline"};
    build.config = "Debug";
    return build;
}

bool hasArgument(const EA::Vector<std::string>& arguments,
                 const std::string& argument)
{
    return arguments.contains(argument);
}

bool hasDefinitionNamed(const EA::Vector<std::string>& arguments,
                        const std::string& name)
{
    const auto prefix = "-D" + name + "=";

    for (const auto& argument: arguments)
    {
        if (argument.starts_with(prefix))
            return true;
    }

    return false;
}
} // namespace BinaryCacheTests

auto tFindDependencyHash =
    test("BinaryCache/find/missesWhenLockedDependencyHashChanges") = []
{
    using namespace BinaryCacheTests;
    const auto directory = createEmptyTestDirectory("binary-cache-find");
    const auto cache = CacheRoot {directory / "cache"};
    const auto key = writeMarkerBuiltWithDependency(cache);

    const auto sameLock =
        writeLockWithDependencyHash(directory / "same", builtDependencyHash);
    const auto changedLock =
        writeLockWithDependencyHash(directory / "changed", changedDependencyHash);

    check(findBinary(cache, packageName, key).has_value());
    check(findBinary(cache, packageName, key, sameLock).has_value());
    check(!findBinary(cache, packageName, key, changedLock).has_value());
    check(!findBinary(cache, packageName, key, directory / "missing.lock")
               .has_value());
};

auto tChildLockArguments = test("BinaryCache/childBuild/injectsEpmAndTheLock") = []
{
    using namespace BinaryCacheTests;
    const auto build = createLockedChildBuild();
    const auto arguments = configureArguments(build);

    check(hasArgument(arguments,
                      "-DCMAKE_PROJECT_TOP_LEVEL_INCLUDES=/c/build/eacp/Debug/"
                      "epm-export.cmake;/epm/share/epm/cmake/epm.cmake"));
    check(hasArgument(arguments, "-DEPM_LOCK_FILE=/project/epm.lock"));
    check(hasArgument(arguments, "-DEPM_LOCK_MODE=offline"));
    check(hasArgument(arguments, "-DEPM_CACHE=/c"));
    check(hasArgument(arguments,
                      "-DEPM_EXECUTABLE=" + eacp::Files::executablePath().str()));
};

auto tChildNoLockArguments =
    test("BinaryCache/childBuild/leavesEpmOutWithoutALock") = []
{
    using namespace BinaryCacheTests;
    auto build = createLockedChildBuild();
    build.childLock = {};
    const auto arguments = configureArguments(build);

    check(hasArgument(
        arguments,
        "-DCMAKE_PROJECT_TOP_LEVEL_INCLUDES=/c/build/eacp/Debug/epm-export.cmake"));

    for (const auto* name:
         {"EPM_EXECUTABLE", "EPM_LOCK_FILE", "EPM_LOCK_MODE", "EPM_CACHE"})
        check(!hasDefinitionNamed(arguments, name));
};
