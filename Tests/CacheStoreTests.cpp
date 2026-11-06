#include <epm/Build/Configs.h>
#include <epm/Build/Harvest.h>
#include <epm/Cache/BinaryCache.h>
#include <epm/Cache/CacheStore.h>
#include <epm/Cache/CacheVerify.h>
#include <epm/Spec/JsonFile.h>
#include <epm/Util/Sha256.h>
#include <epm/Util/Text.h>

#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/TemporaryFile.h>

#include <NanoTest/NanoTest.h>

#include <functional>

using namespace nano;
using namespace epm;
using eacp::FilePath;

namespace
{
using Configs = eacp::Vector<std::string>;

AbiFingerprint storeFingerprint(std::string version = "17.0.0.17000013")
{
    auto fingerprint = AbiFingerprint {};
    fingerprint.platform = "macos-arm64";
    fingerprint.compilerId = "AppleClang";
    fingerprint.compilerVersion = version;
    fingerprint.compilerMajor = majorVersion(version);
    fingerprint.systemName = "Darwin";
    fingerprint.systemProcessor = "arm64";
    fingerprint.sourceId = "abc";
    return fingerprint;
}

Env storeEnv()
{
    auto env = Env {};
    env.cxxCompiler = "/usr/bin/c++";
    env.cxxCompilerId = "AppleClang";
    env.cxxCompilerVersion = "17.0.0.17000013";
    return env;
}

// `config` of eacp published into `cache`'s entry for `fingerprint`: one
// archive and one gen/shared file its export names.
FilePath publishInto(const CacheRoot& cache,
                     const std::string& config,
                     const AbiFingerprint& fingerprint = storeFingerprint(),
                     const std::string& archive = {})
{
    const auto prefix = createEntry(cache, "eacp", fingerprint, "abc")->prefix;
    const auto staging = newStaging(prefix);
    const auto unit = configDirectory(staging.path(), config);
    const auto shared = std::string {"gen/shared/0123456789ab/Register.cpp"};

    check(writeTextFile(unit / ("lib/" + config + "/libeacp.a"),
                        archive.empty() ? "!<arch>\n" + config : archive));
    check(writeTextFile(unit / ("export-" + config + ".cmake"),
                        sharedRootLine() + "  INTERFACE_SOURCES \""
                            + std::string {sharedRootVariable} + "/" + shared
                            + "\"\n"));
    check(writeTextFile(unit / ("export-" + config + ".cmake.names"), "eacp\n"));
    check(writeTextFile(staging.path() / shared, "int registered;\n"));

    const auto described = describeStagedConfig(staging.path(), config, storeEnv());
    check(described.marker.has_value());
    check(publishConfig(prefix, staging.path(), *described.marker).result
          == PublishResult::published);
    removeStagingIfEmpty(prefix);
    return prefix;
}

// The published config of `marker` in the entry at `prefix` with its
// epm-files.json changed by `change`, as a store from another machine may
// hold it, and the marker that records the changed list.
ConfigMarker rewrittenFiles(const FilePath& prefix,
                            const ConfigMarker& marker,
                            const std::function<void(ConfigFiles&)>& change)
{
    const auto unit = configDirectory(prefix, marker.config);
    auto files =
        loadJsonFile<ConfigFiles>(unit / "epm-files.json").value_or(ConfigFiles {});
    change(files);

    const auto text = jsonText(files);
    check(writeTextFile(unit / "epm-files.json", text));

    auto changed = marker;
    changed.filesSha256 = sha256Hex(text);
    return changed;
}

// Every file under `directory`, relative, sorted.
Configs filesUnder(const FilePath& directory)
{
    auto options = eacp::Files::DirectoryOptions {};
    options.recursive = true;
    options.includeHidden = true;
    auto files = Configs {};

    for (const auto& entry: eacp::Files::listDirectory(directory, options))
        files.add(entry.path.relativeTo(directory).str());

    return files;
}

Configs localConfigs(const CacheRoot& cache)
{
    const auto entries = findEntries(cache, "eacp", "macos-arm64");
    return entries.empty() ? Configs {} : entries[0].configs;
}
} // namespace

auto tStoreCopy = test("CacheStore/copy/checkedAndPublished") = []
{
    auto root = eacp::TemporaryFile {};
    const auto local = CacheRoot {root.path() / "local"};
    const auto store = CacheRoot {root.path() / "store"};
    const auto from = publishInto(local, "Release");
    const auto marker = *readEntry(from)->markerOf("Release");
    const auto to = createEntry(store, "eacp", storeFingerprint(), "abc")->prefix;

    const auto copied = copyConfig(from, to, marker);
    check(copied.result == PublishResult::published);
    check(copied.bytes > 0);
    check(readEntry(to)->configs == Configs {"Release"});
    check(verifyCache(store).problems.empty());
    check(verifyCache(store).configs == 1);
    check(!eacp::Files::exists(stagingDirectory(to)));

    // A second copy finds it there and copies nothing.
    check(copyConfig(from, to, marker).result == PublishResult::alreadyThere);
};

auto tStoreCopyCorrupt = test("CacheStore/copy/aCorruptUnitPublishesNothing") = []
{
    auto root = eacp::TemporaryFile {};
    const auto local = CacheRoot {root.path() / "local"};
    const auto store = CacheRoot {root.path() / "store"};
    const auto from = publishInto(local, "Release");
    const auto marker = *readEntry(from)->markerOf("Release");
    check(writeTextFile(configDirectory(from, "Release") / "lib/Release/libeacp.a",
                        "!<arch>\nRelease, changed"));

    const auto to = createEntry(store, "eacp", storeFingerprint(), "abc")->prefix;
    const auto copied = copyConfig(from, to, marker);

    check(copied.result == PublishResult::failed);
    check(copied.error.starts_with("lib/Release/libeacp.a: size "));
    check(readEntry(to)->configs.empty());
    check(!eacp::Files::exists(stagingDirectory(to)));

    // A files list naming a file outside its unit is not followed.
    const auto escaping = rewrittenFiles(from,
                                         marker,
                                         [](ConfigFiles& files)
                                         { files.files[0].path = "../../outside"; });
    check(copyConfig(from, to, escaping).error
          == "../../outside: named outside its directory");

    // A files list that is not the one the marker recorded is not read.
    auto stale = marker;
    stale.filesSha256 = marker.filesSha256 == std::string(64, '0')
                            ? std::string(64, '1')
                            : std::string(64, '0');
    check(copyConfig(from, to, stale).error
          == "epm-files.json: sha256 differs from the one the marker recorded");
};

auto tStoreUntrusted = test("CacheStore/copy/storeMarkersAreChecked") = []
{
    auto root = eacp::TemporaryFile {};
    const auto local = CacheRoot {root.path() / "local"};
    const auto store = CacheRoot {root.path() / "store"};
    const auto from = publishInto(local, "Release");
    const auto marker = *readEntry(from)->markerOf("Release");
    const auto to = createEntry(store, "eacp", storeFingerprint(), "abc")->prefix;
    const auto filesFile = configDirectory(from, "Release") / "epm-files.json";
    const auto original = eacp::Files::readFile(filesFile);

    // A shared path into a unit would pre-create configs/Release in the
    // destination and block its publish for good.
    const auto intoUnit = rewrittenFiles(
        from,
        marker,
        [](ConfigFiles& files) { files.shared[0].path = "configs/Release/x"; });
    check(copyConfig(from, to, intoUnit).error
          == "configs/Release/x: a shared file outside gen/shared/");
    check(!eacp::Files::exists(configDirectory(to, "Release")));

    // A unit without the .names the facade reads.
    const auto withoutNames = [](ConfigFiles& files)
    {
        auto kept = eacp::Vector<UnitFile> {};

        for (const auto& file: files.files)
            if (!file.path.ends_with(".names"))
                kept.add(file);

        files.files = kept;
    };
    const auto noNames = rewrittenFiles(from, marker, withoutNames);
    check(copyConfig(from, to, noNames).error
          == "the unit lists no export-Release.cmake.names");

    // An lto the bytes do not bear out, either way.
    check(writeTextFile(filesFile, original));
    auto claimsLto = marker;
    claimsLto.lto = "llvm-bitcode";
    check(copyConfig(from, to, claimsLto).error
          == "its files hold no LTO objects, but the marker records llvm-bitcode");

    publishInto(
        local, "Debug", storeFingerprint(), std::string {"BC\xC0\xDE\x35\x14", 6});
    const auto bitcodeMarker = *readEntry(from)->markerOf("Debug");
    check(bitcodeMarker.lto == "llvm-bitcode");

    auto hidesLto = bitcodeMarker;
    hidesLto.lto.clear();
    check(copyConfig(from, to, hidesLto).error
          == "its files hold llvm-bitcode objects, but the marker records none");

    // LTO objects recorded for another compiler than the entry's.
    auto otherCompiler = bitcodeMarker;
    otherCompiler.compilerVersion = "17.0.0.17000319";
    check(copyConfig(from, to, otherCompiler).error.starts_with("it records "));

    check(readEntry(to)->configs.empty());
    check(verifyCache(store).problems.empty());
};

auto tStorePullComplete =
    test("CacheStore/pull/aCompleteStoreEntryBeatsAnIncompleteExactOne") = []
{
    auto root = eacp::TemporaryFile {};

    // The store's entry is another compiler's, compatible, with both configs.
    const auto builder = CacheRoot {root.path() / "builder"};
    const auto other = storeFingerprint("17.0.0.17000319");
    publishInto(builder, "Debug", other);
    publishInto(builder, "Release", other);
    const auto store = root.path() / "store";
    check(pushToStore(builder, store, {}).configs == 2);

    // Local holds the exact entry with Debug only.
    const auto local = CacheRoot {root.path() / "local", {store}};
    const auto exact = publishInto(local, "Debug");
    const auto both = Configs {"Debug", "Release"};
    const auto pulled = pullFromStores(
        local, "eacp", storeFingerprint(), AbiMatch::compatible, both);

    check(pulled.copied.size() == 2);

    const auto pick = pickEntry(findEntries(local, "eacp", "macos-arm64"),
                                storeFingerprint(),
                                AbiMatch::compatible,
                                both);
    check(pick.has_value() && pick->prefix != exact);
    check(pick.has_value() && pick->configs == both);
    check(readEntry(exact)->configs == Configs {"Debug"});
};

auto tStorePush = test("CacheStore/push/secondPushIsANoOp") = []
{
    auto root = eacp::TemporaryFile {};
    const auto local = CacheRoot {root.path() / "local"};
    const auto store = root.path() / "store";
    publishInto(local, "Release");
    publishInto(local, "Debug");

    const auto first = pushToStore(local, store, {});
    check(first.configs == 2);
    check(first.entries == 1);
    check(first.alreadyThere == 0);
    check(first.problems.empty());
    check(verifyCache(CacheRoot {store}).problems.empty());

    const auto second = pushToStore(local, store, {});
    check(second.configs == 0);
    check(second.alreadyThere == 2);
    check(transferText(second, "pushed", "to", store)
          == "pushed 0 configs (0 B) into 0 entries to " + store.str()
                 + "; 2 already there");

    // A name the cache has no entry of is a problem.
    const auto named = pushToStore(local, store, {"eacp", "fmt"});
    check(named.problems == Configs {"fmt: no entries"});

    // Pulled back into an empty cache, the entries are whole.
    const auto fresh = CacheRoot {root.path() / "fresh"};
    const auto pulled = pullFromStore(fresh, store, {});
    check(pulled.configs == 2);
    check(localConfigs(fresh) == Configs {"Debug", "Release"});
};

auto tStorePull = test("CacheStore/pull/takesWhatTheLocalPickLacks") = []
{
    auto root = eacp::TemporaryFile {};
    const auto builder = CacheRoot {root.path() / "builder"};
    publishInto(builder, "Debug");
    publishInto(builder, "Release");
    const auto store = root.path() / "store";
    check(pushToStore(builder, store, {}).configs == 2);
    const auto before = filesUnder(store);

    // Local holds Release of the same fingerprint; the store adds Debug.
    const auto local = CacheRoot {root.path() / "local", {store}};
    publishInto(local, "Release");
    const auto pulled = pullFromStores(local,
                                       "eacp",
                                       storeFingerprint(),
                                       AbiMatch::compatible,
                                       {"Debug", "Release"});

    check(pulled.copied == Configs {"Debug from " + store.str()});
    check(pulled.notes.empty());
    check(localConfigs(local) == Configs {"Debug", "Release"});
    check(verifyCache(local).problems.empty());

    // Nothing was written into the store.
    check(filesUnder(store) == before);

    // A local hit looks at no store.
    check(pullFromStores(local,
                         "eacp",
                         storeFingerprint(),
                         AbiMatch::compatible,
                         {"Debug", "Release"})
              .copied.empty());
};

auto tStoreFallback = test("CacheStore/pull/aCorruptStoreFallsBackToTheNext") = []
{
    auto root = eacp::TemporaryFile {};
    const auto builder = CacheRoot {root.path() / "builder"};
    publishInto(builder, "Release");
    const auto bad = root.path() / "bad";
    const auto good = root.path() / "good";
    const auto missing = root.path() / "missing";
    check(pushToStore(builder, bad, {}).configs == 1);
    check(pushToStore(builder, good, {}).configs == 1);

    const auto badEntry = findEntriesIn(bad / "bin", "eacp", "macos-arm64")[0];
    check(writeTextFile(configDirectory(badEntry.prefix, "Release")
                            / "lib/Release/libeacp.a",
                        "!<arch>\nRelease, corrupt"));

    const auto local = CacheRoot {root.path() / "local", {missing, bad, good}};
    check(storeNotes(local)
          == Configs {"the cache store " + missing.str()
                      + " does not exist: not used"});

    const auto pulled = pullFromStores(
        local, "eacp", storeFingerprint(), AbiMatch::compatible, {"Release"});

    check(pulled.copied == Configs {"Release from " + good.str()});
    check(pulled.notes.size() == 1);
    check(pulled.notes[0].starts_with("the cache store " + bad.str()
                                      + " holds Release of eacp"));
    check(localConfigs(local) == Configs {"Release"});
    check(verifyCache(local).problems.empty());
};

auto tStoreNoGain = test("CacheStore/pull/copiesOnlyWhatThePickGains") = []
{
    auto root = eacp::TemporaryFile {};

    // The store's entry is another compiler's, compatible, with Release only.
    const auto builder = CacheRoot {root.path() / "builder"};
    publishInto(builder, "Release", storeFingerprint("17.0.0.17000319"));
    const auto store = root.path() / "store";
    check(pushToStore(builder, store, {}).configs == 1);

    // Local holds Release too and needs Debug as well: the store adds nothing.
    const auto local = CacheRoot {root.path() / "local", {store}};
    publishInto(local, "Release");
    const auto pulled = pullFromStores(local,
                                       "eacp",
                                       storeFingerprint(),
                                       AbiMatch::compatible,
                                       {"Debug", "Release"});

    check(pulled.copied.empty());
    check(findEntries(local, "eacp", "macos-arm64").size() == 1);

    // Under strict, the store's entry is not even a candidate; with an empty
    // local cache, compatible takes it.
    const auto empty = CacheRoot {root.path() / "empty", {store}};
    check(pullFromStores(
              empty, "eacp", storeFingerprint(), AbiMatch::strict, {"Release"})
              .copied.empty());
    check(pullFromStores(
              empty, "eacp", storeFingerprint(), AbiMatch::compatible, {"Release"})
              .copied
          == Configs {"Release from " + store.str()});
};
