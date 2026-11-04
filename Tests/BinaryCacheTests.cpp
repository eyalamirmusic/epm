#include "TestDirectory.h"

#include <epm/Build/ChildBuild.h>
#include <epm/Build/Configs.h>
#include <epm/Cache/BinaryCache.h>
#include <epm/Lock/LockFile.h>
#include <epm/Spec/JsonFile.h>
#include <epm/Util/Text.h>

#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/TemporaryFile.h>

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace epm;

namespace
{
using Configs = eacp::Vector<std::string>;

AbiFingerprint cacheFingerprint(std::string compilerPath = "/usr/bin/c++",
                                std::string version = "17.0.0.17000013")
{
    auto fingerprint = AbiFingerprint {};
    fingerprint.platform = "macos-arm64";
    fingerprint.compilerId = "AppleClang";
    fingerprint.compilerVersion = version;
    fingerprint.compilerMajor = compilerMajor(version);
    fingerprint.compilerPath = std::move(compilerPath);
    fingerprint.systemName = "Darwin";
    fingerprint.systemProcessor = "arm64";
    fingerprint.options = {{"EACP_ENABLE_TESTS", "OFF"}};
    fingerprint.sourceHash = "abc";
    return fingerprint;
}

AbiFingerprint xcodeFingerprint()
{
    return cacheFingerprint("/Applications/Xcode.app/clang++", "17.0.0.17000319");
}

BinaryEntry cacheEntry(std::string directory,
                       AbiFingerprint fingerprint,
                       Configs configs,
                       std::string createdAt = "2026-11-01T00:00:00Z")
{
    return {eacp::FilePath {"/cache/bin/eacp/macos-arm64"} / directory,
            std::move(fingerprint),
            std::move(configs),
            {},
            std::move(createdAt)};
}

using Entries = eacp::Vector<BinaryEntry>;

constexpr auto compatible = AbiMatch::compatible;

// The directory name of the entry pickEntry chooses, or "none".
std::string chosen(const Entries& entries,
                   const AbiFingerprint& wanted,
                   AbiMatch policy,
                   const Configs& configs)
{
    const auto picked = pickEntry(entries, wanted, policy, configs);
    return picked ? picked->prefix.fileName() : std::string {"none"};
}
} // namespace

auto tPickCompatible = test("BinaryCache/pick/compatibleUsesOtherCompiler") = []
{
    const auto both = Configs {"Debug", "Release"};
    const auto entries = Entries {cacheEntry("ninja", cacheFingerprint(), both)};

    check(chosen(entries, xcodeFingerprint(), compatible, both) == "ninja");
    check(chosen(entries, xcodeFingerprint(), AbiMatch::strict, both) == "none");
    check(chosen(entries, xcodeFingerprint(), AbiMatch::any, both) == "ninja");
};

auto tPickExact = test("BinaryCache/pick/exactPreferred") = []
{
    const auto both = Configs {"Debug", "Release"};
    const auto entries =
        Entries {cacheEntry("ninja", cacheFingerprint(), both, "2026-11-03"),
                 cacheEntry("xcode", xcodeFingerprint(), both, "2026-11-01")};

    check(chosen(entries, xcodeFingerprint(), compatible, {"Debug"}) == "xcode");
    check(chosen(entries, cacheFingerprint(), compatible, {"Debug"}) == "ninja");
    check(chosen(entries, xcodeFingerprint(), AbiMatch::any, {"Debug"}) == "xcode");

    // Exact even when it still lacks a config the compatible one has.
    const auto partial =
        Entries {cacheEntry("ninja", cacheFingerprint(), both),
                 cacheEntry("xcode", xcodeFingerprint(), {"Release"})};

    check(chosen(partial, xcodeFingerprint(), compatible, {"Debug"}) == "xcode");
};

auto tPickComplete = test("BinaryCache/pick/thenCompleteThenNewest") = []
{
    const auto entries = Entries {
        cacheEntry("full", cacheFingerprint("/opt/clang++"), {"Debug", "Release"}),
        cacheEntry("newer", xcodeFingerprint(), {"Release"}, "2026-11-02")};

    check(chosen(entries, cacheFingerprint(), compatible, {"Debug", "Release"})
          == "full");
    check(chosen(entries, cacheFingerprint(), compatible, {"Release"}) == "newer");
};

auto tPickPolicyFilters = test("BinaryCache/pick/policyFilters") = []
{
    const auto newerMajor = cacheFingerprint("/opt/clang++", "18.1.0");
    const auto entries = Entries {cacheEntry("eighteen", newerMajor, {"Release"})};

    check(chosen(entries, cacheFingerprint(), compatible, {"Release"}) == "none");
    check(chosen(entries, cacheFingerprint(), AbiMatch::any, {"Release"})
          == "eighteen");
    check(chosen({}, cacheFingerprint(), AbiMatch::any, {"Release"}) == "none");
};

auto tAdditivePlan = test("BinaryCache/additive/onlyMissingConfigsAreBuilt") = []
{
    // Ninja Release, then Ninja Debug, then Xcode (Debug + Release).
    const auto release = cacheEntry("e", cacheFingerprint(), {"Release"});
    const auto both = cacheEntry("e", cacheFingerprint(), {"Debug", "Release"});

    check(missingConfigs(release.configs, buildConfigsFor({"Debug"}))
          == Configs {"Debug"});
    check(
        missingConfigs(both.configs, buildConfigsFor({"Debug", "Release"})).empty());
    check(missingConfigs(both.configs, buildConfigsFor({"RelWithDebInfo"})).empty());
};

auto tEntriesOnDisk = test("BinaryCache/entries/readFromDisk") = []
{
    auto root = eacp::TemporaryFile {};
    const auto cache = CacheRoot {root.path()};
    const auto fingerprint = cacheFingerprint();
    const auto prefix = entryPrefix(cache, "eacp", fingerprint);

    check(prefix.parentDirectory() == cache.bin() / "eacp" / "macos-arm64");
    check(prefix.fileName() == fingerprintId(fingerprint).substr(0, 12));

    const auto manifest =
        EntryManifest {EntryManifest {}.schema, fingerprint, "abc", "2026-11-02"};
    const auto release = ConfigMarker {"Release", "2026-11-02", {}, {}};

    check(saveJsonFile(prefix / "manifest.json", manifest));
    check(saveJsonFile(prefix / "Release.json", release));

    // A marker whose name and contents disagree does not count.
    check(saveJsonFile(prefix / "Debug.json", release));

    const auto entries = findEntries(cache, "eacp", "macos-arm64");

    check(entries.size() == 1);
    check(entries[0].prefix == prefix);
    check(entries[0].configs == Configs {"Release"});
    check(entries[0].fingerprint.compilerPath == "/usr/bin/c++");
    check(entries[0].targetsFile() == prefix / "epm-targets.cmake");
    check(allEntries(cache).size() == 1);
    check(findEntries(cache, "eacp", "macos-x64").empty());
};

namespace BinaryCacheLockTests
{
constexpr auto dependencyName = "Miro";
constexpr auto builtDependencyHash = "1111";
constexpr auto changedDependencyHash = "2222";

eacp::FilePath writeLockWithDependencyHash(const eacp::FilePath& directory,
                                           const std::string& hash)
{
    auto package = LockedPackage {};
    package.name = dependencyName;
    package.hash = hash;

    const auto path = directory / lockFileName;
    auto lock = LockFile {};
    lock.storePackage(package);
    writeLockFile(path, lock);
    return path;
}

BinaryEntry entryBuiltWithDependency()
{
    auto entry = cacheEntry("aaa", cacheFingerprint(), {"Debug", "Release"});
    entry.lockedDependencies["Debug"] = {{dependencyName, builtDependencyHash}};
    entry.lockedDependencies["Release"] = {{dependencyName, changedDependencyHash}};
    return entry;
}

ChildBuild createLockedChildBuild()
{
    auto build = ChildBuild {};
    build.request.name = "eacp";
    build.sourceDirectory = eacp::FilePath {"/c/src/eacp"};
    build.buildDirectory = eacp::FilePath {"/c/build/eacp/Debug"};
    build.prefix = eacp::FilePath {"/c/bin/eacp/macos-arm64/f685faf3d9f4"};
    build.cacheRoot = eacp::FilePath {"/c"};
    build.epmExecutable = eacp::FilePath {"/epm/bin/epm"};
    build.childLock = {eacp::FilePath {"/project/epm-lock.json"},
                       "offline",
                       eacp::FilePath {"/project/epm.json"}};
    build.config = "Debug";
    return build;
}
} // namespace BinaryCacheLockTests

auto tConfigsMatchTheLock =
    test("BinaryCache/lock/configsBuiltAgainstOtherHashesAreMissing") = []
{
    using namespace BinaryCacheLockTests;
    const auto directory = createEmptyTestDirectory("binary-cache-lock");
    const auto lock = writeLockWithDependencyHash(directory, builtDependencyHash);
    const auto entry = withConfigsMatchingTheLock(entryBuiltWithDependency(), lock);

    check(entry.configs == Configs {"Debug"});
    check(withConfigsMatchingTheLock(entryBuiltWithDependency(),
                                     directory / "missing.lock")
              .configs.empty());
};

auto tChildLockArguments = test("BinaryCache/childBuild/injectsEpmAndTheLock") = []
{
    using namespace BinaryCacheLockTests;
    const auto arguments = configureArguments(createLockedChildBuild());

    check(
        arguments.contains("-DCMAKE_PROJECT_TOP_LEVEL_INCLUDES=/c/build/eacp/Debug/"
                           "epm-export.cmake;/c/build/eacp/Debug/epm.cmake"));
    check(arguments.contains("-DEPM_NESTED=ON"));
    check(arguments.contains("-DEPM_EXECUTABLE=/epm/bin/epm"));
    check(arguments.contains("-DEPM_LOCK_FILE=/project/epm-lock.json"));
    check(arguments.contains("-DEPM_LOCK_MODE=offline"));
    check(arguments.contains("-DEPM_MANIFEST_FILE=/project/epm.json"));
    check(arguments.contains("-DEPM_ROOT_NAME=eacp"));
    check(arguments.contains("-DEPM_CACHE=/c"));
};
