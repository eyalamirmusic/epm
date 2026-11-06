#include <epm/Build/Configs.h>
#include <epm/Build/Harvest.h>
#include <epm/Build/Lto.h>
#include <epm/Cache/BinaryCache.h>
#include <epm/Cache/CacheVerify.h>
#include <epm/Spec/JsonFile.h>
#include <epm/Util/Command.h>
#include <epm/Util/ProcessInfo.h>
#include <epm/Util/Sha256.h>
#include <epm/Util/Text.h>

#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/TemporaryFile.h>

#include <NanoTest/NanoTest.h>

#include <chrono>
#include <thread>
#include <vector>

using namespace nano;
using namespace epm;

namespace
{
using Configs = eacp::Vector<std::string>;

AbiFingerprint cacheFingerprint(std::string version = "17.0.0.17000013")
{
    auto fingerprint = AbiFingerprint {};
    fingerprint.platform = "macos-arm64";
    fingerprint.compilerId = "AppleClang";
    fingerprint.compilerVersion = version;
    fingerprint.compilerMajor = majorVersion(version);
    fingerprint.systemName = "Darwin";
    fingerprint.systemProcessor = "arm64";
    fingerprint.options = {{"EACP_ENABLE_TESTS", "OFF"}};
    fingerprint.sourceId = "abc";
    return fingerprint;
}

AbiFingerprint xcodeFingerprint()
{
    return cacheFingerprint("17.0.0.17000319");
}

// An entry whose configs were built by its own compiler; with `lto`, as LTO
// objects of that kind.
BinaryEntry cacheEntry(std::string directory,
                       AbiFingerprint fingerprint,
                       Configs configs,
                       std::string createdAt = "2026-11-01T00:00:00Z",
                       std::string lto = {})
{
    auto markers = eacp::Vector<ConfigMarker> {};

    for (const auto& config: configs)
    {
        auto marker = ConfigMarker {};
        marker.config = config;
        marker.compilerId = fingerprint.compilerId;
        marker.compilerVersion = fingerprint.compilerVersion;
        marker.lto = lto;
        markers.add(marker);
    }

    return {eacp::FilePath {"/cache/bin/eacp/macos-arm64"} / directory,
            std::move(fingerprint),
            std::move(configs),
            std::move(createdAt),
            std::move(markers)};
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

    // A complete compatible entry wins over an exact one lacking a config, so
    // nothing is built (or fails as binary-only) that the cache holds.
    const auto partial =
        Entries {cacheEntry("ninja", cacheFingerprint(), both),
                 cacheEntry("xcode", xcodeFingerprint(), {"Release"})};

    check(chosen(partial, xcodeFingerprint(), compatible, {"Debug"}) == "ninja");
    check(chosen(partial, xcodeFingerprint(), compatible, both) == "ninja");
    check(chosen(partial, xcodeFingerprint(), compatible, {"Release"}) == "xcode");

    // With none complete, exact wins, to build the missing configs into.
    const auto neither =
        Entries {cacheEntry("ninja", cacheFingerprint(), {"Release"}),
                 cacheEntry("xcode", xcodeFingerprint(), {"Debug"})};
    check(chosen(neither, xcodeFingerprint(), compatible, both) == "xcode");

    // A config the consumer cannot link (LTO objects of another compiler) is
    // not complete: the exact entry is picked, not the LTO one.
    const auto pinned =
        Entries {cacheEntry("ninja", cacheFingerprint(), both, {}, "llvm-bitcode"),
                 cacheEntry("xcode", xcodeFingerprint(), {"Debug"})};
    check(chosen(pinned, xcodeFingerprint(), compatible, both) == "xcode");
};

auto tPickBuiltFirst = test("BinaryCache/pick/anEmptyEntryComesLast") = []
{
    const auto both = Configs {"Debug", "Release"};

    // An exact entry a failed build left without configs loses to a
    // compatible one that holds them.
    const auto entries = Entries {cacheEntry("ninja", cacheFingerprint(), both),
                                  cacheEntry("xcode", xcodeFingerprint(), {})};

    check(chosen(entries, xcodeFingerprint(), compatible, both) == "ninja");

    // Alone, it is still picked, to build into.
    check(chosen(Entries {cacheEntry("xcode", xcodeFingerprint(), {})},
                 xcodeFingerprint(),
                 compatible,
                 both)
          == "xcode");

    // Among entries holding configs, a complete one wins over an exact one.
    const auto partial =
        Entries {cacheEntry("ninja", cacheFingerprint(), both),
                 cacheEntry("xcode", xcodeFingerprint(), {"Debug"})};
    check(chosen(partial, xcodeFingerprint(), compatible, both) == "ninja");
    check(chosen(partial, xcodeFingerprint(), compatible, {"Debug"}) == "xcode");
};

auto tPickComplete = test("BinaryCache/pick/thenCompleteThenLowestFp12") = []
{
    const auto entries = Entries {
        cacheEntry("bbbb", xcodeFingerprint(), {"Release"}, "2026-11-02"),
        cacheEntry(
            "cccc", cacheFingerprint("17.0.0.17000100"), {"Debug", "Release"})};

    check(chosen(entries, cacheFingerprint(), compatible, {"Debug", "Release"})
          == "cccc");

    // Equal ranks: the lowest fp12, however the entries' clocks ran, in
    // whatever order they are listed.
    check(chosen(entries, cacheFingerprint(), compatible, {"Release"}) == "bbbb");

    const auto later = Entries {
        cacheEntry("bbbb", xcodeFingerprint(), {"Release"}, "2020-01-01"),
        cacheEntry(
            "aaaa", cacheFingerprint("17.0.0.17000100"), {"Release"}, "2030")};
    const auto reversed = Entries {later[1], later[0]};

    check(chosen(later, cacheFingerprint(), compatible, {"Release"}) == "aaaa");
    check(chosen(reversed, cacheFingerprint(), compatible, {"Release"}) == "aaaa");
};

auto tPickPolicyFilters = test("BinaryCache/pick/policyFilters") = []
{
    const auto newerMajor = cacheFingerprint("18.1.0");
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
    check(missingConfigs(both.configs, buildConfigsFor({"MinSizeRel"})).empty());
    check(missingConfigs(both.configs, buildConfigsFor({"RelWithDebInfo"}))
          == Configs {"RelWithDebInfo"});
};

auto tEntriesOnDisk = test("BinaryCache/entries/readFromDisk") = []
{
    auto root = eacp::TemporaryFile {};
    const auto cache = CacheRoot {root.path()};
    const auto fingerprint = cacheFingerprint();
    const auto prefix = entryPrefix(cache, "eacp", fingerprint);

    check(prefix.parentDirectory() == cache.bin() / "eacp" / "macos-arm64");
    check(prefix.fileName() == fingerprintId(fingerprint).substr(0, 12));

    const auto created = createEntry(cache, "eacp", fingerprint, "abc");
    check(created.has_value() && created->prefix == prefix);
    check(created.has_value() && created->configs.empty());
    check(eacp::Files::readFile(prefix / "epm-targets.cmake") == targetsFileText());

    auto release = ConfigMarker {};
    release.config = "Release";
    check(saveJsonFile(configDirectory(prefix, "Release") / "epm-config.json",
                       release));

    // A marker whose name and contents disagree does not count, nor a unit
    // without one.
    check(
        saveJsonFile(configDirectory(prefix, "Debug") / "epm-config.json", release));
    check(writeTextFile(configDirectory(prefix, "RelWithDebInfo") / "x.cmake", "x"));

    // A unit and marker spelling a config in another case (as a store from
    // another machine may hold) is passed over, not read as Release.
    // (Its own entry: on a case-insensitive filesystem configs/release is
    // configs/Release.)
    auto lower = ConfigMarker {};
    lower.config = "release";
    check(saveJsonFile(prefix.sibling("aaaaaaaaaaaa") / "manifest.json",
                       EntryManifest {entrySchema, fingerprint, "abc", "x"}));
    check(saveJsonFile(configDirectory(prefix.sibling("aaaaaaaaaaaa"), "release")
                           / "epm-config.json",
                       lower));
    const auto lowerOnly = readEntry(prefix.sibling("aaaaaaaaaaaa"));
    check(lowerOnly.has_value() && lowerOnly->configs.empty()
          && lowerOnly->markers.empty());
    check(eacp::Files::removeAll(prefix.sibling("aaaaaaaaaaaa")));

    // An entry of the old bin/<name>/<key12> layout is never seen, nor one of
    // an older schema, nor staging.
    check(writeTextFile(cache.bin() / "eacp" / "8ea428e72a65" / "manifest.json",
                        "{\"schema\": 1}"));
    check(writeTextFile(cache.bin() / "eacp" / "8ea428e72a65.json", "{}"));
    check(saveJsonFile(prefix.sibling("0123456789ab") / "manifest.json",
                       EntryManifest {2, fingerprint, "abc", "2026-11-02"}));
    check(saveJsonFile(stagingDirectory(prefix) / "x.1" / "manifest.json",
                       EntryManifest {entrySchema, fingerprint, "abc", "x"}));

    const auto entries = findEntries(cache, "eacp", "macos-arm64");

    check(entries.size() == 1);
    check(entries[0].prefix == prefix);
    check(entries[0].configs == Configs {"Release"});
    check(entries[0].markers.size() == 1);
    check(entries[0].fingerprint.compilerVersion == "17.0.0.17000013");
    check(entries[0].targetsFile() == prefix / "epm-targets.cmake");
    check(allEntries(cache).size() == 1);
    check(findEntries(cache, "eacp", "macos-x64").empty());

    // createEntry takes the entry there.
    const auto again = createEntry(cache, "eacp", fingerprint, "other");
    check(again.has_value() && again->configs == Configs {"Release"});
};

auto tCreateReplacesOlder =
    test("BinaryCache/createEntry/replacesAnOlderLayout") = []
{
    auto root = eacp::TemporaryFile {};
    const auto cache = CacheRoot {root.path()};
    const auto fingerprint = cacheFingerprint();
    const auto prefix = entryPrefix(cache, "eacp", fingerprint);

    // An entry of schema 2 at the same path, with its in-place files.
    check(saveJsonFile(prefix / "manifest.json",
                       EntryManifest {2, fingerprint, "abc", "2026-11-02"}));
    check(writeTextFile(prefix / "lib/Release/libeacp.a", "old"));
    check(!readEntry(prefix).has_value());

    const auto created = createEntry(cache, "eacp", fingerprint, "abc");

    check(created.has_value() && created->configs.empty());
    check(!eacp::Files::exists(prefix / "lib"));
    check(eacp::Files::listDirectory(stagingDirectory(prefix)).empty());
};

auto tStaleStaging = test("BinaryCache/staging/staleOnesGo") = []
{
    auto root = eacp::TemporaryFile {};
    const auto directory = root.path();

    check(writeTextFile(directory / "eacp-temp-a.1000" / "x", "x"));
    check(writeTextFile(directory / "not-staging" / "x", "x"));
    const auto fresh = newStaging(directory / "fp");
    check(writeTextFile(fresh.path() / "x", "x"));

    removeStaleStaging(directory);
    removeStaleStaging(stagingDirectory(directory / "fp"));

    check(!eacp::Files::exists(directory / "eacp-temp-a.1000"));
    check(eacp::Files::exists(directory / "not-staging"));
    check(eacp::Files::exists(fresh.path() / "x"));
};

auto tStagingOwner = test("BinaryCache/staging/liveOnesStayWhateverTheirAge") = []
{
    auto root = eacp::TemporaryFile {};
    const auto directory = root.path();
    const auto now =
        std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count());
    const auto here = hostId();
    const auto self = std::to_string(currentProcessId());
    const auto dead = std::string {"2147483000"};
    const auto stage = [&](const std::string& name)
    {
        check(writeTextFile(directory / name / "x", "x"));
        return directory / name;
    };

    // This host's: kept while its process runs, however old (a day-long
    // build), deleted at once when it is gone.
    const auto mineOld = stage("eacp-temp-a." + self + "-" + here + ".1000");
    const auto deadFresh = stage("eacp-temp-b." + dead + "-" + here + "." + now);

    // Another host's, whose processes this one cannot see: by the heartbeat.
    const auto otherFresh = stage("eacp-temp-c.1-0123456789ab." + now);
    const auto otherOld = stage("eacp-temp-d.1-0123456789ab.1000");
    const auto otherBeating = stage("eacp-temp-e.1-0123456789ab.1000");
    touchStagingHeartbeat(otherBeating);

    check(stagingOwner(mineOld.fileName())->processId == currentProcessId());
    check(stagingOwner(mineOld.fileName())->host == here);
    check(stagingOwner(otherOld.fileName())->createdAt == 1000);
    check(!stagingOwner("eacp-temp-x").has_value());
    check(newStaging(directory / "fp")
              .path()
              .fileName()
              .find("." + self + "-" + here + ".")
          != std::string::npos);

    removeStaleStaging(directory);

    check(eacp::Files::exists(mineOld));
    check(!eacp::Files::exists(deadFresh));
    check(eacp::Files::exists(otherFresh));
    check(!eacp::Files::exists(otherOld));
    check(eacp::Files::exists(otherBeating));
};

namespace
{
// A harvested Release in a fresh staging directory of `prefix`'s entry, with
// one archive and one gen/shared file its export names.
eacp::TemporaryFile stageRelease(const eacp::FilePath& prefix,
                                 const std::string& archive = "!<arch>\nobject")
{
    auto staging = newStaging(prefix);
    const auto unit = configDirectory(staging.path(), "Release");
    const auto shared = std::string {"gen/shared/0123456789ab/Register.cpp"};

    check(writeTextFile(unit / "lib/Release/libeacp.a", archive));
    check(writeTextFile(unit / "include/src/eacp.h", "#pragma once\n"));
    check(writeTextFile(unit / "export-Release.cmake",
                        sharedRootLine() + "  INTERFACE_SOURCES \""
                            + std::string {sharedRootVariable} + "/" + shared
                            + "\"\n"));
    check(writeTextFile(unit / "export-Release.cmake.names", "eacp\n"));
    check(writeTextFile(staging.path() / shared, "int registered;\n"));
    return staging;
}

Env cacheEnv(std::string version = "17.0.0.17000013")
{
    auto env = Env {};
    env.cxxCompiler = "/usr/bin/c++";
    env.cxxCompilerId = "AppleClang";
    env.cxxCompilerVersion = std::move(version);
    return env;
}

ConfigMarker describedRelease(const eacp::FilePath& staged, const Env& env)
{
    auto described = describeStagedConfig(staged, "Release", env);
    check(described.marker.has_value());
    return described.marker.value_or(ConfigMarker {});
}
} // namespace

auto tDescribeStaged = test("BinaryCache/publish/markerRecordsEveryFile") = []
{
    auto root = eacp::TemporaryFile {};
    const auto prefix =
        createEntry(CacheRoot {root.path()}, "eacp", cacheFingerprint(), "abc")
            ->prefix;
    const auto staging = stageRelease(prefix);
    const auto described =
        describeStagedConfig(staging.path(), "Release", cacheEnv());
    const auto marker = described.marker.value_or(ConfigMarker {});
    const auto& files = described.files;

    check(marker.config == "Release");
    check(marker.compilerVersion == "17.0.0.17000013");
    check(marker.lto.empty());
    check(files.files.size() == 4);
    check(files.files[0].path == "export-Release.cmake");
    check(files.files[2].path == "include/src/eacp.h");
    check(files.files[2].size == 13);
    check(files.files[2].sha256 == sha256Hex("#pragma once\n"));
    check(files.shared.size() == 1);
    check(files.shared[0].path == "gen/shared/0123456789ab/Register.cpp");

    // The files list sits in the unit, recorded by its sha256; the marker,
    // which every pick reads, holds no list.
    const auto unit = configDirectory(staging.path(), "Release");
    const auto filesText = eacp::Files::readFile(unit / "epm-files.json");
    check(marker.filesSha256 == sha256Hex(filesText));
    check(loadConfigFiles(unit, marker).files.has_value());
    check(filesText.find("include/src/eacp.h") != std::string::npos);

    auto tampered = marker;
    tampered.filesSha256 = sha256Hex("other");
    check(!loadConfigFiles(unit, tampered).files.has_value());

    // A config whose objects are LLVM bitcode says so.
    const auto bitcode =
        stageRelease(prefix, std::string {"BC\xC0\xDE\x35\x14\x00\x00", 8});
    check(describedRelease(bitcode.path(), cacheEnv()).lto == llvmBitcode);
};

auto tPublishRace = test("BinaryCache/publish/concurrentPublishersBothSucceed") = []
{
    for (auto round = 0; round < 10; ++round)
    {
        auto root = eacp::TemporaryFile {};
        const auto cache = CacheRoot {root.path()};

        // Both create the entry and stage the same config, without a lock.
        auto created = std::vector<std::optional<BinaryEntry>>(2);
        auto creators = std::vector<std::thread> {};

        for (auto i = 0; i < 2; ++i)
            creators.emplace_back(
                [&, i]
                {
                    created[i] =
                        createEntry(cache, "eacp", cacheFingerprint(), "abc");
                });

        for (auto& thread: creators)
            thread.join();

        check(created[0].has_value() && created[1].has_value());

        if (!created[0] || !created[1])
            return;

        const auto prefix = created[0]->prefix;
        check(created[1]->prefix == prefix);

        const auto first = stageRelease(prefix);
        const auto second = stageRelease(prefix);
        const auto markers =
            eacp::Vector<ConfigMarker> {describedRelease(first.path(), cacheEnv()),
                                        describedRelease(second.path(), cacheEnv())};
        const auto staged =
            eacp::Vector<eacp::FilePath> {first.path(), second.path()};
        auto results = std::vector<Published>(2);
        auto publishers = std::vector<std::thread> {};

        for (auto i = 0; i < 2; ++i)
            publishers.emplace_back(
                [&, i]
                { results[i] = publishConfig(prefix, staged[i], markers[i]); });

        for (auto& thread: publishers)
            thread.join();

        const auto published = [&](PublishResult result)
        {
            return (results[0].result == result ? 1 : 0)
                   + (results[1].result == result ? 1 : 0);
        };

        check(published(PublishResult::published) == 1);
        check(published(PublishResult::alreadyThere) == 1);

        const auto entry = readEntry(prefix);
        check(entry.has_value() && entry->configs == Configs {"Release"});
        check(verifyCache(cache).problems.empty());
        check(verifyCache(cache).configs == 1);
    }
};

auto tVerify = test("BinaryCache/verify/detectsCorruptAndIncomplete") = []
{
    auto root = eacp::TemporaryFile {};
    const auto cache = CacheRoot {root.path()};
    const auto prefix =
        createEntry(cache, "eacp", cacheFingerprint(), "abc")->prefix;
    const auto staging = stageRelease(prefix, "!<arch>\nobject one");

    check(publishConfig(
              prefix, staging.path(), describedRelease(staging.path(), cacheEnv()))
              .result
          == PublishResult::published);
    check(verifyCache(cache).problems.empty());

    const auto unit = configDirectory(prefix, "Release");
    const auto label = "eacp/macos-arm64/" + prefix.fileName() + " Release: ";
    const auto problems = [&] { return verifyCache(cache).problems; };

    // The same size, other bytes.
    check(writeTextFile(unit / "lib/Release/libeacp.a", "!<arch>\nobject two"));
    check(
        problems()
        == eacp::Vector<std::string> {
            label + "lib/Release/libeacp.a: sha256 differs from the recorded one"});
    check(verificationText(verifyCache(cache)).find("1 problem")
          != std::string::npos);

    check(writeTextFile(unit / "lib/Release/libeacp.a", "!<arch>\n"));
    check(problems()
          == eacp::Vector<std::string> {
              label + "lib/Release/libeacp.a: size 8, recorded 18"});

    check(eacp::Files::removeFile(unit / "lib/Release/libeacp.a"));
    check(writeTextFile(unit / "lib/Release/extra.a", "x"));
    check(eacp::Files::removeFile(prefix / "gen/shared/0123456789ab/Register.cpp"));
    check(problems().size() == 3);

    // A unit without its marker, and an entry directory without a manifest.
    check(eacp::Files::removeFile(unit / "epm-config.json"));
    check(writeTextFile(prefix.sibling("aaaaaaaaaaaa") / "configs/x", "x"));
    const auto incomplete = problems();
    check(incomplete.size() == 2);
    check(
        incomplete.contains(label.substr(0, label.size() - 2)
                            + ": no valid epm-config.json (an incomplete config)"));
};

auto tLtoUsable = test("BinaryCache/lto/pinnedToItsCompiler") = []
{
    auto marker = ConfigMarker {};
    marker.compilerId = "AppleClang";
    marker.compilerVersion = "17.0.0.17000013";

    check(configUsableBy(marker, xcodeFingerprint()));

    marker.lto = std::string {llvmBitcode};
    check(!configUsableBy(marker, xcodeFingerprint()));
    check(configUsableBy(marker, cacheFingerprint()));

    // A compatible entry whose Release is bitcode of another compiler cannot
    // serve a Release consumer, under any policy, but can a Debug one.
    const auto entries = Entries {
        cacheEntry("ninja", cacheFingerprint(), {"Release"}, {}, "llvm-bitcode")};

    for (const auto policy: {compatible, AbiMatch::any})
    {
        check(chosen(entries, xcodeFingerprint(), policy, {"Release"}) == "none");
        check(chosen(entries, xcodeFingerprint(), policy, {"Debug"}) == "ninja");
    }

    check(chosen(entries, cacheFingerprint(), compatible, {"Release"}) == "ninja");
    check(entries[0].usableConfigs(xcodeFingerprint()).empty());
};

namespace
{
// Mid's fingerprint built with `version` against the entry `leaf`.
AbiFingerprint midOver(const AbiFingerprint& leaf, const std::string& version)
{
    auto mid = cacheFingerprint(version);
    mid.sourceId = "mid";
    mid.deps = {{"Leaf", fingerprintId(leaf)}};
    mid.compatibleDeps = {{"Leaf", abiKey(leaf, AbiMatch::compatible)}};
    mid.anyDeps = {{"Leaf", abiKey(leaf, AbiMatch::any)}};
    return mid;
}

AbiFingerprint leafBuiltWith(const std::string& version)
{
    auto leaf = cacheFingerprint(version);
    leaf.sourceId = "leaf";
    return leaf;
}
} // namespace

auto tDepsAcrossMachines = test("BinaryCache/deps/ciMidOverCiLeafUsableLocally") = []
{
    // CI built Leaf and Mid with one compiler; this machine has its own Leaf
    // entry, strict for it, built with a compatible compiler.
    const auto ciLeaf = leafBuiltWith("17.0.0.17000013");
    const auto localLeaf = leafBuiltWith("17.0.0.17000319");
    const auto ciMid = midOver(ciLeaf, "17.0.0.17000013");
    const auto wanted = midOver(localLeaf, "17.0.0.17000319");
    const auto entries = Entries {cacheEntry("ci", ciMid, {"Release"})};

    check(ciMid.deps != wanted.deps);
    check(abiKey(ciLeaf, compatible) == abiKey(localLeaf, compatible));
    check(abiMatches(ciMid, wanted, compatible));
    check(abiMatches(ciMid, wanted, AbiMatch::any));
    check(chosen(entries, wanted, compatible, {"Release"}) == "ci");

    // strict still wants Mid built against this machine's own Leaf.
    check(!abiMatches(ciMid, wanted, AbiMatch::strict));
    check(chosen(entries, wanted, AbiMatch::strict, {"Release"}) == "none");

    // A Leaf compatible would not accept in place of CI's rejects CI's Mid.
    const auto otherMajor = midOver(leafBuiltWith("18.1.0"), "17.0.0.17000319");
    check(chosen(entries, otherMajor, compatible, {"Release"}) == "none");
    check(chosen(entries, otherMajor, AbiMatch::any, {"Release"}) == "ci");

    // Without the policy's keys (a step that recorded none), the deps hashes
    // decide, as strictly as before.
    auto unkeyed = wanted;
    unkeyed.compatibleDeps.clear();
    unkeyed.anyDeps.clear();
    check(!abiMatches(ciMid, unkeyed, compatible));
};

namespace
{
std::string arMember(const std::string& name, const std::string& data)
{
    auto header = std::string(60, ' ');
    header.replace(0, name.size(), name);
    const auto size = std::to_string(data.size());
    header.replace(48, size.size(), size);
    header.replace(58, 2, "`\n");
    return header + data + (data.size() % 2 == 1 ? "\n" : "");
}

const auto machO = std::string {"\xCF\xFA\xED\xFE\x0C\x00\x00\x01", 8};
const auto bitcode = std::string {"BC\xC0\xDE\x35\x14\x00\x00", 8};

std::string archiveOf(const eacp::Vector<std::string>& members)
{
    auto archive = std::string {"!<arch>\n"} + arMember("/", "symbols");

    for (auto i = 0; i < members.size(); ++i)
        archive += arMember("m" + std::to_string(i) + ".o/", members[i]);

    return archive;
}

std::string bigEndian32(std::uint32_t value)
{
    return {static_cast<char>(value >> 24),
            static_cast<char>(value >> 16),
            static_cast<char>(value >> 8),
            static_cast<char>(value)};
}

std::string universalOf(const eacp::Vector<std::string>& slices)
{
    auto header = bigEndian32(0xCAFEBABE) + bigEndian32(slices.size());
    auto offset = 8 + 20 * static_cast<std::uint32_t>(slices.size());
    auto body = std::string {};

    for (const auto& slice: slices)
    {
        header += bigEndian32(0x0100000C) + bigEndian32(0) + bigEndian32(offset)
                  + bigEndian32(slice.size()) + bigEndian32(0);
        body += slice;
        offset += slice.size();
    }

    return header + body;
}
} // namespace

auto tLtoDetect = test("BinaryCache/lto/detectsBitcodeInArchives") = []
{
    // A plain archive of machine code, with an odd-sized member before.
    check(ltoKindOf(archiveOf({"odd", machO, machO})).empty());

    // One bitcode member is enough, raw or in its wrapper.
    check(ltoKindOf(archiveOf({machO, bitcode})) == llvmBitcode);
    check(ltoKindOf(archiveOf({std::string {"\xDE\xC0\x17\x0B", 4} + "rest"}))
          == llvmBitcode);
    check(ltoKindOf(bitcode) == llvmBitcode);

    // A BSD long name sits before the member's data.
    const auto named = std::string {"Long_Object_Name.o"} + std::string(2, '\0');
    check(ltoKindOf("!<arch>\n" + arMember("#1/20", named + bitcode))
          == llvmBitcode);
    check(ltoKindOf("!<arch>\n" + arMember("#1/20", named + machO)).empty());

    // GCC's LTO objects are ELF with .gnu.lto_ sections; text that merely
    // mentions one is not an object.
    const auto gcc = std::string {"\x7F"
                                  "ELF\x02\x01\x01",
                                  7}
                     + "..gnu.lto_.symtab.0";
    check(ltoKindOf(archiveOf({gcc})) == gccLto);
    check(ltoKindOf("// sections named .gnu.lto_ are LTO IR\n").empty());

    // Every slice of a universal archive.
    check(ltoKindOf(universalOf({archiveOf({machO}), archiveOf({bitcode})}))
          == llvmBitcode);
    check(ltoKindOf(universalOf({archiveOf({machO}), archiveOf({machO})})).empty());

    // A Java class shares the magic; truncated input is no crash.
    check(ltoKindOf(bigEndian32(0xCAFEBABE) + bigEndian32(0x34)).empty());
};

// What this machine's cc and ar make of -flto: LLVM bitcode for Clang, GCC
// LTO objects for GCC, in an archive either way. Skipped without cc, ar or
// LTO support.
auto tLtoReal = test("BinaryCache/lto/detectsTheRealCompilersOutput") = []
{
    const auto cc = findExecutable("cc");
    const auto ar = findExecutable("ar");

    if (!cc || !ar)
        return;

    auto root = eacp::TemporaryFile {};
    const auto source = root.path() / "a.c";
    check(writeTextFile(source, "int f(void) { return 1; }\n"));

    const auto archive =
        [&](const std::string& name, eacp::Vector<std::string> flags)
    {
        const auto object = root.path() / (name + ".o");
        const auto library = root.path() / ("lib" + name + ".a");
        flags.addFrom(
            eacp::Vector<std::string> {"-c", source.str(), "-o", object.str()});

        if (!runCommand(cc->str(), flags).ok
            || !runCommand(ar->str(), {"rcs", library.str(), object.str()}).ok)
            return std::optional<std::string> {};

        return std::optional {eacp::Files::readFile(library)};
    };

    const auto plain = archive("plain", {});
    const auto lto = archive("lto", {"-flto"});

    if (!plain || !lto)
        return;

    check(ltoKindOf(*plain).empty());
    check(ltoKindOf(*lto) == llvmBitcode || ltoKindOf(*lto) == gccLto);
};

auto tLtoTruncated = test("BinaryCache/lto/truncatedInputIsNoCrash") = []
{
    check(ltoKindOf("!<arch>\n" + arMember("x.o/", bitcode).substr(0, 62)).empty());
    check(ltoKindOf("").empty());
};

auto tLtoNesting = test("BinaryCache/lto/slicesAndMembersNeverNestDeeper") = []
{
    // A slice covering the whole file, header included, is the file again:
    // passed over, not followed for ever.
    auto selfSlice = bigEndian32(0xCAFEBABE) + bigEndian32(1) + bigEndian32(7)
                     + bigEndian32(0) + bigEndian32(0) + bigEndian32(28)
                     + bigEndian32(0);
    check(ltoKindOf(selfSlice).empty());

    // A slice overlapping the slice table is passed over too.
    auto overlapping = universalOf({bitcode});
    overlapping.replace(16, 4, bigEndian32(20));
    check(ltoKindOf(overlapping).empty());

    // A universal file inside a slice, or an archive inside a member, is not
    // read: neither is what a toolchain writes.
    check(ltoKindOf(universalOf({universalOf({bitcode})})).empty());
    check(ltoKindOf(archiveOf({archiveOf({bitcode})})).empty());
    check(ltoKindOf(universalOf({bitcode})) == llvmBitcode);
};

namespace
{
std::string littleEndian32(std::uint32_t value)
{
    return {static_cast<char>(value),
            static_cast<char>(value >> 8),
            static_cast<char>(value >> 16),
            static_cast<char>(value >> 24)};
}

std::string name16(const std::string& name)
{
    return name + std::string(16 - name.size(), '\0');
}

// A 64-bit little-endian Mach-O object with one LC_SEGMENT_64 named
// `segment` holding one section in `sectionSegment`.
std::string machOObject(const std::string& segment,
                        const std::string& sectionSegment)
{
    const auto commandSize = 72u + 80u;
    auto object = littleEndian32(0xFEEDFACF) + littleEndian32(0x0100000C)
                  + littleEndian32(0) + littleEndian32(1) + littleEndian32(1)
                  + littleEndian32(commandSize) + littleEndian32(0)
                  + littleEndian32(0);

    object += littleEndian32(0x19) + littleEndian32(commandSize) + name16(segment)
              + std::string(32, '\0') + littleEndian32(7) + littleEndian32(7)
              + littleEndian32(1) + littleEndian32(0);
    object +=
        name16("__wrapper_sects") + name16(sectionSegment) + std::string(48, '\0');
    return object;
}

// An anonymous COFF object header (cl.exe's /GL objects and bigobj files)
// with `classId` at offset 12.
std::string anonymousObject(const std::string& classId)
{
    return std::string {"\0\0\xFF\xFF\x01\x00\x64\x86", 8} + std::string(4, '\0')
           + classId + std::string(16, '\0');
}
} // namespace

auto tLtoMachOGcc = test("BinaryCache/lto/detectsGccLtoInMachO") = []
{
    check(ltoKindOf(machOObject("", "__GNU_LTO")) == gccLto);
    check(ltoKindOf(machOObject("__GNU_LTO", "__TEXT")) == gccLto);
    check(ltoKindOf(archiveOf({machO, machOObject("", "__GNU_LTO")})) == gccLto);
    check(ltoKindOf(machOObject("", "__TEXT")).empty());

    // A command size past the end, or below a command's own header, stops
    // the walk without reading out of bounds.
    auto lying = machOObject("", "__GNU_LTO");
    lying.replace(36, 4, littleEndian32(0x7FFFFFFF));
    check(ltoKindOf(lying).empty());
    lying.replace(36, 4, littleEndian32(4));
    check(ltoKindOf(lying).empty());

    for (auto size = std::size_t {0}; size < 150; size += 7)
        check(ltoKindOf(machOObject("", "__GNU_LTO").substr(0, size)).empty());
};

auto tLtoMsvc = test("BinaryCache/lto/detectsMsvcGlObjects") = []
{
    const auto ltcg = std::string {"\x38\xFE\xB3\x0C\xA5\xD9\xAB\x4D"
                                   "\xAC\x9B\xD6\xB6\x22\x26\x53\xC2",
                                   16};
    const auto bigObj = std::string {"\xC7\xA1\xBA\xD1\xEE\xBA\xA9\x4B"
                                     "\xAF\x20\xFA\xF6\x6A\xA4\xDC\xB8",
                                     16};

    check(ltoKindOf(anonymousObject(ltcg)) == msvcLtcg);
    check(ltoKindOf(archiveOf({anonymousObject(ltcg)})) == msvcLtcg);
    check(ltoKindOf(anonymousObject(bigObj)).empty());
    check(ltoKindOf(anonymousObject(ltcg).substr(0, 20)).empty());
};
