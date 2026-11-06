#include <epm/Build/Harvest.h>
#include <epm/Cache/BinaryCache.h>
#include <epm/Cache/CacheStore.h>
#include <epm/Recipe/Recipe.h>
#include <epm/Resolve/Resolve.h>
#include <epm/Spec/JsonFile.h>
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

// A package whose build() is the test's.
struct Steps final : Package
{
    std::function<void(Package&)> body;

    void build() override { body(*this); }
};

BuildOutput runSteps(const BuildInput& input, std::function<void(Package&)> body)
{
    auto steps = Steps {};
    steps.body = std::move(body);
    return runPackage(steps, input);
}

BuildInput packageInput(const FilePath& root,
                        std::string mode = "auto",
                        Configs configs = {"Release"})
{
    auto input = BuildInput {};
    input.cacheRoot = (root / "cache").str();
    input.env.cmake = "/usr/bin/cmake";
    input.env.cxxCompiler = "/usr/bin/c++";
    input.env.cxxCompilerId = "AppleClang";
    input.env.cxxCompilerVersion = "17.0.0.17000013";
    input.env.systemName = "Darwin";
    input.env.systemProcessor = "arm64";
    input.request.name = "Lib";
    input.request.gitUrl = "https://example.invalid/lib.git";
    input.request.gitRef = "v1";
    input.request.mode = std::move(mode);
    input.request.configs = std::move(configs);
    input.request.options = {{"LIB_TESTS", "OFF"}};
    input.source =
        BuildSource {std::string(40, 'a'), "abcdef", (root / "src").str()};
    input.epmExecutable = "/opt/epm/bin/epm";
    return input;
}

// What a harvested build of `config` leaves in its staged unit.
void writeFakeExport(const Prefix& prefix, const std::string& config)
{
    const auto exportFile = exportFileFor(prefix.configDirectory(config), config);
    check(writeTextFile(exportFile, "# export of Lib\n"));
    check(writeTextFile(exportFile.sibling(exportFile.fileName() + ".names"),
                        "Lib\n"));
}

// The default flow with a callback that fakes the build of each config,
// recording which configs it was asked for.
BuildOutput fakeBuild(const BuildInput& input, Configs& asked, int& calls)
{
    return runSteps(input,
                    [&](Package& package)
                    {
                        const auto source = package.fetch();
                        const auto key = package.fingerprint(source);
                        const auto build = [&](const Prefix& prefix)
                        {
                            ++calls;
                            asked = prefix.configs();

                            for (const auto& config: prefix.configs())
                                writeFakeExport(prefix, config);
                        };

                        if (auto prefix = package.cached(key, build))
                            package.exportPrefix(*prefix);
                        else
                            package.useSource();
                    });
}

// A prefix outside cached(), or with `staging` named, one inside its
// callback.
Prefix fakePrefix(Configs configs = {"Release"}, std::string staging = {})
{
    auto fingerprint = AbiFingerprint {};
    fingerprint.platform = "macos-arm64";
    const auto path = FilePath {"/cache/bin/Lib/macos-arm64/0123456789ab"};
    auto entry = BinaryEntry {path, fingerprint, {}, {}, {}};
    const auto staged =
        staging.empty() ? FilePath {} : stagingDirectory(path) / staging;
    return {entry, fingerprint, std::move(configs), staged};
}

struct Custom final : CMakePackage
{
};

// The API as a recipe uses it, spelled as in the design.
struct Fmt : epm::CMakePackage
{
    void configure(epm::CMakeBuild& b) override { b.option("FMT_DOC", "OFF"); }
};

struct Zlib : epm::Package
{
    void build() override
    {
        auto src = fetch();
        auto key = fingerprint(src);
        if (auto prefix =
                cached(key, [&](const epm::Prefix& p) { cmake(src, p).run(); }))
            exportPrefix(*prefix);
        else
            useSource();
    }
};

struct MyProject : epm::Recipe
{
    void describe() override
    {
        auto fmt = add<Fmt>("fmt", "fmtlib/fmt", "11.0.2");
        fmt.option("FMT_TEST", "OFF");
        add<Zlib>("zlib", "madler/zlib", "v1.3.1");
        add("nlohmann_json", "nlohmann/json", "v3.11.3");

        if (env().profile == "debug-deps")
            fmt.config("Debug");
    }
};

struct Declares final : Recipe
{
    void describe() override
    {
        auto first = add<Custom>("A", "org/A", "v1");
        add("B", "org/B", "v2").option("B_FLAG", "ON");
        first.option("A_FLAG", "OFF")
            .config("Debug")
            .mode("prebuilt")
            .abiMatch("any");

        if (env().profile == "more")
            add("C");
    }
};
} // namespace

auto tPackageSource = test("Package/cached/sourceMode") = []
{
    auto root = eacp::TemporaryFile {};
    auto asked = Configs {};
    auto calls = 0;
    const auto output = fakeBuild(packageInput(root.path(), "source"), asked, calls);

    check(output.ok());
    check(calls == 0);
    check(output.name == "Lib");
    check(output.mode == "source");
    check(output.sourceDirectory == (root.path() / "src").str());
    check(output.prefix.empty() && output.fingerprintHash.empty());
    check(!eacp::Files::exists(FilePath {packageInput(root.path()).cacheRoot} / "bin"
                               / "Lib"));
};

auto tPackageBuildsMissing = test("Package/cached/buildsMissingConfigs") = []
{
    auto root = eacp::TemporaryFile {};
    const auto input = packageInput(root.path(), "auto", {"Debug", "Release"});
    auto asked = Configs {};
    auto calls = 0;
    const auto output = fakeBuild(input, asked, calls);

    check(output.ok());
    check(calls == 1);
    check(asked == Configs {"Debug", "Release"});
    check(output.mode == "prebuilt");
    check(output.sourceDirectory == (root.path() / "src").str());
    check(output.platform == "macos-arm64");
    check(output.configs == Configs {"Debug", "Release"});
    check(output.note.empty());

    const auto entry = readEntry(FilePath {output.prefix});
    check(entry.has_value());

    if (!entry)
        return;

    check(entry->configs == Configs {"Debug", "Release"});
    check(output.fingerprintHash == fingerprintId(entry->fingerprint));
    check(entry->fingerprint.sourceId == "abcdef");
    check(entry->fingerprint.options.at("LIB_TESTS") == "OFF");
    check(eacp::Files::isFile(entry->prefix / "configs/Debug/epm-config.json"));
    check(eacp::Files::isFile(entry->prefix / "configs/Release/epm-config.json"));
    check(eacp::Files::readFile(entry->targetsFile()) == targetsFileText());

    // Each unit records its files, hashed; staging is gone.
    const auto* release = entry->markerOf("Release");
    check(release != nullptr
          && loadConfigFiles(configDirectory(entry->prefix, "Release"), *release)
                     .files.value_or(ConfigFiles {})
                     .files.size()
                 == 2);
    check(release != nullptr && release->lto.empty());
    check(release != nullptr
          && release->compilerVersion == input.env.cxxCompilerVersion);
    check(eacp::Files::listDirectory(stagingDirectory(entry->prefix)).empty());
};

auto tPackageUsesEntry = test("Package/cached/usesACompleteEntry") = []
{
    auto root = eacp::TemporaryFile {};
    const auto input = packageInput(root.path());
    auto asked = Configs {};
    auto calls = 0;
    const auto first = fakeBuild(input, asked, calls);
    const auto second = fakeBuild(input, asked, calls);

    check(first.ok() && second.ok());
    check(calls == 1);
    check(second.mode == "prebuilt");
    check(second.prefix == first.prefix);
    check(second.fingerprintHash == first.fingerprintHash);

    // binary-only takes it too.
    const auto binaryOnly =
        fakeBuild(packageInput(root.path(), "binary-only"), asked, calls);
    check(binaryOnly.ok() && binaryOnly.prefix == first.prefix);
    check(calls == 1);
};

auto tPackageAdditive = test("Package/cached/buildsOnlyTheMissingConfig") = []
{
    auto root = eacp::TemporaryFile {};
    auto asked = Configs {};
    auto calls = 0;
    const auto release = fakeBuild(packageInput(root.path()), asked, calls);
    const auto both = fakeBuild(
        packageInput(root.path(), "auto", {"Debug", "Release"}), asked, calls);

    check(release.ok() && both.ok());
    check(calls == 2);
    check(asked == Configs {"Debug"});
    check(both.prefix == release.prefix);
    check(both.configs == Configs {"Debug", "Release"});
};

namespace
{
// Every file under `directory`, by relative path, with its contents.
std::map<std::string, std::string> fileContentsUnder(const FilePath& directory)
{
    auto options = eacp::Files::DirectoryOptions {};
    options.recursive = true;
    options.includeHidden = true;
    auto files = std::map<std::string, std::string> {};

    for (const auto& entry: eacp::Files::listDirectory(directory, options))
        if (eacp::Files::isFile(entry.path))
            files[entry.path.relativeTo(directory).str()] =
                eacp::Files::readFile(entry.path);

    return files;
}
} // namespace

auto tPackageAddLeavesOthers =
    test("Package/cached/addingAConfigLeavesOthersAlone") = []
{
    auto root = eacp::TemporaryFile {};
    auto asked = Configs {};
    auto calls = 0;
    const auto release = fakeBuild(packageInput(root.path()), asked, calls);
    const auto prefix = FilePath {release.prefix};
    const auto releaseBefore = fileContentsUnder(configDirectory(prefix, "Release"));
    const auto facadeBefore = eacp::Files::readFile(prefix / "epm-targets.cmake");

    const auto both = fakeBuild(
        packageInput(root.path(), "auto", {"Debug", "Release"}), asked, calls);

    check(both.ok() && both.prefix == release.prefix);
    check(asked == Configs {"Debug"});
    check(!releaseBefore.empty());
    check(fileContentsUnder(configDirectory(prefix, "Release")) == releaseBefore);
    check(eacp::Files::readFile(prefix / "epm-targets.cmake") == facadeBefore);
    check(eacp::Files::isFile(configDirectory(prefix, "Debug") / "epm-config.json"));
};

// A compatible entry of another compiler takes a missing config only when it
// is machine code: LTO objects go to the exact entry, which then gets the
// other configs too, so the entry's own compiler can still use its entry.
auto tPackageLtoExact = test("Package/cached/ltoObjectsGoToTheExactEntry") = []
{
    auto root = eacp::TemporaryFile {};
    auto asked = Configs {};
    auto calls = 0;
    const auto ninja = fakeBuild(packageInput(root.path()), asked, calls);

    auto input = packageInput(root.path(), "auto", {"Debug", "Release"});
    input.env.cxxCompilerVersion = "17.0.0.17000319";
    auto rounds = eacp::Vector<Configs> {};

    const auto xcode =
        runSteps(input,
                 [&](Package& package)
                 {
                     const auto key = package.fingerprint(package.fetch());
                     const auto build = [&](const Prefix& prefix)
                     {
                         rounds.add(prefix.configs());

                         for (const auto& config: prefix.configs())
                         {
                             writeFakeExport(prefix, config);
                             check(writeTextFile(
                                 prefix.configDirectory(config) / "lib/libLib.a",
                                 std::string {"BC\xC0\xDE\x35\x14\x00\x00", 8}));
                         }
                     };

                     if (auto prefix = package.cached(key, build))
                         package.exportPrefix(*prefix);
                 });

    check(ninja.ok() && xcode.ok());
    check(rounds == eacp::Vector<Configs> {{"Debug"}, {"Release"}});
    check(xcode.prefix != ninja.prefix);
    check(xcode.configs == Configs {"Debug", "Release"});
    check(xcode.note.empty());

    // The compatible entry is as it was; the exact one holds both, pinned.
    check(readEntry(FilePath {ninja.prefix})->configs == Configs {"Release"});

    const auto exact = readEntry(FilePath {xcode.prefix});
    check(exact && exact->markerOf("Debug")->lto == "llvm-bitcode");
    check(exact && exact->markerOf("Debug")->compilerVersion == "17.0.0.17000319");
};

// An entry whose Debug is LTO objects of its own compiler and whose Release is
// plain, picked for Release by a compatible compiler: the configs it reports,
// which the facade loads, leave the Debug it cannot link out.
auto tPackageUsableConfigs =
    test("Package/export/onlyConfigsTheConsumerCanLink") = []
{
    auto root = eacp::TemporaryFile {};
    const auto both = Configs {"Debug", "Release"};
    const auto bitcode = std::string {"BC\xC0\xDE\x35\x14\x00\x00", 8};
    const auto build = [&](const Prefix& prefix)
    {
        for (const auto& config: prefix.configs())
        {
            writeFakeExport(prefix, config);
            check(writeTextFile(prefix.configDirectory(config) / "lib/libLib.a",
                                config == "Debug" ? bitcode : "!<arch>\n"));
        }
    };
    const auto steps = [&](Package& package)
    {
        if (auto prefix =
                package.cached(package.fingerprint(package.fetch()), build))
            package.exportPrefix(*prefix);
    };
    const auto built = runSteps(packageInput(root.path(), "auto", both), steps);

    check(built.ok() && built.configs == both);

    auto input = packageInput(root.path(), "binary-only", {"Release"});
    input.env.cxxCompilerVersion = "17.0.0.17000319";
    auto asked = Configs {};
    auto calls = 0;
    const auto used = fakeBuild(input, asked, calls);

    check(used.ok() && calls == 0);
    check(used.prefix == built.prefix);
    check(used.configs == Configs {"Release"});
};

auto tPackageBinaryOnlyMiss = test("Package/cached/binaryOnlyMissIsAnError") = []
{
    auto root = eacp::TemporaryFile {};
    auto asked = Configs {};
    auto calls = 0;
    const auto output =
        fakeBuild(packageInput(root.path(), "binary-only"), asked, calls);

    check(!output.ok());
    check(calls == 0);
    check(output.mode.empty());
    check(output.error.find("Lib is binary-only, but the cache has no macos-arm64 "
                            "entry holding Release")
          != std::string::npos);
};

// A config a cache store holds counts as cached, binary-only included: it is
// copied into the local cache and used from there, and nothing is built.
auto tPackageStoreHit = test("Package/cached/aStoreHitCountsAsCached") = []
{
    auto root = eacp::TemporaryFile {};
    auto asked = Configs {};
    auto calls = 0;
    const auto builder = packageInput(root.path() / "ci");
    check(fakeBuild(builder, asked, calls).ok());
    check(calls == 1);

    const auto store = root.path() / "store";
    check(pushToStore(CacheRoot {FilePath {builder.cacheRoot}}, store, {}).configs
          == 1);

    auto input = packageInput(root.path() / "dev", "binary-only");
    input.cacheStores = {store.str()};
    calls = 0;
    const auto output = fakeBuild(input, asked, calls);

    check(output.ok());
    check(calls == 0);
    check(output.mode == "prebuilt");
    check(FilePath {output.prefix}.str().starts_with(input.cacheRoot));
    check(output.configs == Configs {"Release"});

    // Without the store the same request has nothing to use.
    auto alone = packageInput(root.path() / "other", "binary-only");
    alone.cacheStores = {(root.path() / "nowhere").str()};
    const auto missed = fakeBuild(alone, asked, calls);
    check(!missed.ok());
    check(missed.error.find("neither the cache nor its 1 cache store has")
          != std::string::npos);
};

auto tPackageNoBuildMissing = test("Package/cached/autoFallsBackToSource") = []
{
    auto root = eacp::TemporaryFile {};
    auto input = packageInput(root.path());
    input.request.buildMissingBinaries = false;
    auto asked = Configs {};
    auto calls = 0;
    const auto output = fakeBuild(input, asked, calls);

    check(output.ok());
    check(calls == 0);
    check(output.mode == "source");

    // prebuilt builds it anyway.
    input.request.mode = "prebuilt";
    const auto prebuilt = fakeBuild(input, asked, calls);
    check(prebuilt.ok() && prebuilt.mode == "prebuilt");
    check(calls == 1);
};

auto tPackageNoExport = test("Package/cached/aBuildThatExportsNothingFails") = []
{
    auto root = eacp::TemporaryFile {};
    const auto output = runSteps(packageInput(root.path()),
                                 [](Package& package)
                                 {
                                     const auto key =
                                         package.fingerprint(package.fetch());
                                     package.cached(key, [](const Prefix&) {});
                                     package.useSource();
                                 });

    check(!output.ok());
    check(output.error.find("left no export-Release.cmake") != std::string::npos);
};

// A callback that throws after harvesting one config leaves the entry with
// its manifest only; the next run builds every config into it.
auto tPackageCallbackThrows =
    test("Package/cached/callbackThatThrowsMarksNothing") = []
{
    auto root = eacp::TemporaryFile {};
    const auto input = packageInput(root.path(), "auto", {"Debug", "Release"});
    const auto failed =
        runSteps(input,
                 [](Package& package)
                 {
                     const auto key = package.fingerprint(package.fetch());
                     package.cached(key,
                                    [](const Prefix& prefix)
                                    {
                                        writeFakeExport(prefix, "Debug");
                                        throw BuildError {"Release broke"};
                                    });
                     package.useSource();
                 });

    check(!failed.ok());
    check(failed.error == "Release broke");

    const auto store = CacheRoot {FilePath {input.cacheRoot}};
    const auto entries = findEntries(store, "Lib", "macos-arm64");
    check(entries.size() == 1);

    if (entries.size() != 1)
        return;

    check(entries[0].configs.empty());
    check(!eacp::Files::exists(entries[0].prefix / "configs/Debug"));
    check(eacp::Files::listDirectory(stagingDirectory(entries[0].prefix)).empty());

    auto asked = Configs {};
    auto calls = 0;
    const auto rebuilt = fakeBuild(input, asked, calls);
    check(rebuilt.ok());
    check(calls == 1);
    check(asked == Configs {"Debug", "Release"});
    check(FilePath {rebuilt.prefix} == entries[0].prefix);
    check(rebuilt.configs == Configs {"Debug", "Release"});
};

// A source resolved but not fetched: fetch() inside cached()'s callback
// fetches the commit epm resolved, under the lock cached() holds, and the
// snapshot already in the cache makes that a lookup.
auto tPackageFetchUnderLock = test("Package/fetch/insideCachedFetchesTheCommit") = []
{
    auto root = eacp::TemporaryFile {};
    auto input = packageInput(root.path());
    const auto commit = std::string(40, 'b');
    const auto id = sourceId(input.request.gitUrl, commit);
    const auto snapshot =
        FilePath {input.cacheRoot} / "src" / "Lib" / id.substr(0, 12);
    check(eacp::Files::createDirectories(snapshot));
    check(saveJsonFile(snapshot.sibling(snapshot.fileName() + ".json"),
                       SourceMarker {input.request.gitUrl, commit, "now"}));
    input.source = BuildSource {commit, id, {}};

    auto resolvedOutside = SourceSnapshot {};
    auto fetchedInside = SourceSnapshot {};
    const auto output = runSteps(input,
                                 [&](Package& package)
                                 {
                                     resolvedOutside = package.resolveSource();
                                     const auto key =
                                         package.fingerprint(resolvedOutside);
                                     const auto build = [&](const Prefix& prefix)
                                     {
                                         fetchedInside = package.fetch();
                                         writeFakeExport(prefix, "Release");
                                     };

                                     if (auto prefix = package.cached(key, build))
                                         package.exportPrefix(*prefix);
                                 });

    check(output.ok());
    check(resolvedOutside.directory.empty());
    check(resolvedOutside.sourceId == id);
    check(fetchedInside.directory == snapshot);
    check(fetchedInside.commit == commit);

    // Fetched only to build: the prebuilt result names no source directory.
    check(output.sourceDirectory.empty());

    // With the source handed in, fetch() returns it, lock or not.
    fetchedInside = SourceSnapshot {};
    const auto handed = runSteps(packageInput(root.path(), "auto", {"Debug"}),
                                 [&](Package& package)
                                 {
                                     const auto key =
                                         package.fingerprint(package.fetch());
                                     const auto build = [&](const Prefix& prefix)
                                     {
                                         fetchedInside = package.fetch();
                                         writeFakeExport(prefix, "Debug");
                                     };

                                     if (auto prefix = package.cached(key, build))
                                         package.exportPrefix(*prefix);
                                 });

    check(handed.ok());
    check(fetchedInside.directory == root.path() / "src");
};

auto tPackageFetchGiven = test("Package/fetch/takesTheGivenSource") = []
{
    auto root = eacp::TemporaryFile {};
    auto snapshot = SourceSnapshot {};
    const auto output = runSteps(packageInput(root.path()),
                                 [&](Package& package)
                                 {
                                     snapshot = package.fetch();
                                     package.useSource();
                                 });

    check(output.ok());
    check(snapshot.commit == std::string(40, 'a'));
    check(snapshot.sourceId == "abcdef");
    check(snapshot.directory == root.path() / "src");

    // Nothing was fetched: the cache has no src/.
    check(!eacp::Files::exists(FilePath {packageInput(root.path()).cacheRoot}
                               / "src"));
};

auto tPackageContext = test("Package/context") = []
{
    auto root = eacp::TemporaryFile {};
    auto input = packageInput(root.path());
    input.dependencies.add({"Dep", "prebuilt", "f00", "/p", "/p/t.cmake", {}});
    input.dependencyGraph = "set(epm_PACKAGES \"Dep\")\n";
    input.verbose = true;

    const auto output =
        runSteps(input,
                 [&](Package& package)
                 {
                     check(package.spec().name == "Lib");
                     check(package.env().systemName == "Darwin");
                     check(package.cache().path().str() == input.cacheRoot);
                     check(package.dependencies().size() == 1);
                     check(package.dependencies()[0].fingerprint == "f00");
                     check(package.dependencyGraph() == input.dependencyGraph);
                     check(package.verbose());
                     package.useSource();
                 });

    check(output.ok());
};

auto tPackageNoResult = test("Package/run/aBuildThatRecordsNothingFails") = []
{
    auto root = eacp::TemporaryFile {};
    const auto output = runSteps(packageInput(root.path()), [](Package&) {});

    check(!output.ok());
    check(output.error.find("recorded no result") != std::string::npos);
};

auto tPackageThrows = test("Package/run/reportsWhatTheStepThrows") = []
{
    auto root = eacp::TemporaryFile {};
    const auto failed =
        runSteps(packageInput(root.path()),
                 [](Package&) { throw BuildError {"it broke", "the tail"}; });

    check(failed.error == "it broke");
    check(failed.details == "the tail");

    const auto threw = runSteps(packageInput(root.path()),
                                [](Package&) { throw std::runtime_error {"oops"}; });

    check(threw.error == "the build step of Lib threw: oops");

    // The last record wins.
    const auto exported = runSteps(packageInput(root.path()),
                                   [](Package& package)
                                   {
                                       package.exportPrefix(fakePrefix());
                                       package.useSource();
                                   });

    check(exported.mode == "source" && exported.prefix.empty());
};

auto tPackageExport = test("Package/exportPrefix") = []
{
    auto root = eacp::TemporaryFile {};
    auto prefix = fakePrefix();
    const auto output =
        runSteps(packageInput(root.path()),
                 [&](Package& package) { package.exportPrefix(prefix); });

    check(output.ok());
    check(output.mode == "prebuilt");
    check(output.prefix == "/cache/bin/Lib/macos-arm64/0123456789ab");
    check(output.platform == "macos-arm64");
    check(output.fingerprintHash == fingerprintId(prefix.entry().fingerprint));

    // A prebuilt package names its source only when the step asked for it.
    check(output.sourceDirectory.empty());

    const auto fetched = runSteps(packageInput(root.path()),
                                  [&](Package& package)
                                  {
                                      package.fetch();
                                      package.exportPrefix(prefix);
                                  });

    check(fetched.sourceDirectory == (root.path() / "src").str());

    const auto resolved = resolveResultFor(output);
    check(resolved.ok);
    check(resolved.response.name == "Lib");
    check(resolved.response.prefix == FilePath {output.prefix});
    check(resolved.fingerprintHash == output.fingerprintHash);
};

auto tCMakeBuildArguments = test("CMakeBuild/configureArguments") = []
{
    auto root = eacp::TemporaryFile {};
    auto input = packageInput(root.path());
    const auto cacheRoot = FilePath {input.cacheRoot};

    runSteps(
        input,
        [&](Package& package)
        {
            const auto source = package.fetch();
            auto build = package.cmake(source, fakePrefix());
            build.generator("Ninja", "/opt/tools/ninja");

            check(build.configs() == Configs {"Release"});
            check(build.buildDirectory("Release")
                  == cacheRoot / "build" / "Lib" / "macos-arm64" / "0123456789ab"
                         / "Release");

            // Inside cached()'s callback, one tree per staging directory.
            auto staged = package.cmake(source, fakePrefix({"Release"}, "s.1"));
            check(staged.buildDirectory("Release")
                  == cacheRoot / "build" / "Lib" / "macos-arm64" / "0123456789ab"
                         / "s.1" / "Release");
            check(staged.childBuild("Release").prefix
                  == FilePath {"/cache/bin/Lib/macos-arm64/.staging/s.1/configs/"
                               "Release"});

            const auto arguments = build.configureArguments("Release");
            const auto has = [&](const std::string& argument)
            { return arguments.contains(argument); };

            check(arguments[0] == "-S"
                  && arguments[1] == (root.path() / "src").str());
            check(arguments[3] == build.buildDirectory("Release").str());
            check(arguments[4] == "-G" && arguments[5] == "Ninja");
            check(has("-DCMAKE_MAKE_PROGRAM=/opt/tools/ninja"));
            check(has("-DCMAKE_BUILD_TYPE=Release"));
            check(has("-DCMAKE_CXX_COMPILER=/usr/bin/c++"));
            check(has("-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=/cache/bin/Lib/macos-arm64/"
                      "0123456789ab/configs/Release/lib/Release"));
            check(has("-DCPM_SOURCE_CACHE=" + (cacheRoot / "cpm").str()));
            check(has("-DEPM_EXPORT_FILE=/cache/bin/Lib/macos-arm64/0123456789ab/"
                      "configs/Release/export-Release.cmake"));
            check(has("-DEPM_EXPORT_NAMESPACE=epm_release::"));
            check(arguments.back() == "-DLIB_TESTS=OFF");
            check(!has("-DEPM_EXECUTABLE=/opt/epm/bin/epm"));

            // The same fingerprint as the package's own until a value changes.
            check(fingerprintId(package.fingerprint(source))
                  == fingerprintId(package.fingerprint(source, build)));

            build.option("LIB_TESTS", "ON").define("CMAKE_CXX_STANDARD", "17");
            const auto changed = build.configureArguments("Release");
            check(changed[changed.size() - 2] == "-DLIB_TESTS=ON");
            check(changed.back() == "-DCMAKE_CXX_STANDARD=17");
            check(build.definedValues().at("CMAKE_CXX_STANDARD") == "17");
            check(fingerprintId(package.fingerprint(source))
                  != fingerprintId(package.fingerprint(source, build)));

            build.configs({"Debug"});
            check(build.configs() == Configs {"Debug"});
            package.useSource();
        });

    // With dependencies the child calls back the epm of the input.
    input.dependencyGraph = "set(epm_PACKAGES \"Dep\")\n";
    runSteps(input,
             [&](Package& package)
             {
                 const auto arguments = package.cmake(package.fetch(), fakePrefix())
                                            .configureArguments("Release");
                 check(arguments.contains("-DEPM_EXECUTABLE=/opt/epm/bin/epm"));
                 check(arguments.contains("-DEPM_NESTED=ON"));
                 package.useSource();
             });
};

auto tCMakeBuildDefinedValues = test("CMakeBuild/definedValuesWithoutTypes") = []
{
    auto root = eacp::TemporaryFile {};
    auto values = std::map<std::string, std::string> {};
    const auto output =
        runSteps(packageInput(root.path(), "source"),
                 [&](Package& package)
                 {
                     auto build = package.cmake(package.fetch(), fakePrefix());
                     build.option("LIB_TESTS", "ON")
                         .define("CMAKE_POSITION_INDEPENDENT_CODE:BOOL", "ON")
                         .define("LIB_TESTS:BOOL", "OFF");
                     values = build.definedValues();

                     // The typed name reaches the -D flag as written.
                     check(build.configureArguments("Release").contains(
                         "-DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=ON"));
                     package.useSource(build);
                 });

    check(values
          == std::map<std::string, std::string> {
              {"CMAKE_POSITION_INDEPENDENT_CODE", "ON"}, {"LIB_TESTS", "OFF"}});

    // useSource(build) records them for graph.cmake.
    check(output.ok() && output.mode == "source");
    check(output.options == values);
};

auto tPackageOutputOptions = test("Package/output/optionsOfTheBuild") = []
{
    auto root = eacp::TemporaryFile {};

    // The default flow with a hook: source mode records the hook's option.
    struct Hooked final : CMakePackage
    {
        void configure(CMakeBuild& build) override { build.option("HOOK", "ON"); }
    };

    auto hooked = Hooked {};
    const auto source = runPackage(hooked, packageInput(root.path(), "source"));
    check(source.ok() && source.mode == "source");
    check(source.options
          == std::map<std::string, std::string> {{"HOOK", "ON"},
                                                 {"LIB_TESTS", "OFF"}});
    check(resolveResultFor(source).options == source.options);

    // exportPrefix records the options of the fingerprint asked for.
    auto asked = Configs {};
    auto calls = 0;
    const auto prebuilt = fakeBuild(packageInput(root.path()), asked, calls);
    check(prebuilt.ok() && prebuilt.mode == "prebuilt");
    check(prebuilt.options
          == std::map<std::string, std::string> {{"LIB_TESTS", "OFF"}});

    // A plain useSource() records none: the request's options stand.
    const auto plain = runSteps(packageInput(root.path(), "source"),
                                [](Package& package) { package.useSource(); });
    check(plain.ok() && plain.options.empty());
};

auto tCMakeBuildNoPrefix = test("CMakeBuild/withoutAPrefixFails") = []
{
    auto root = eacp::TemporaryFile {};
    const auto output =
        runSteps(packageInput(root.path()),
                 [](Package& package) { package.cmake(package.fetch()).run(); });

    check(!output.ok());
    check(output.error.find("has no prefix") != std::string::npos);
};

auto tRecipeAddCustom = test("Package/recipe/addRegistersCustomSteps") = []
{
    auto recipe = Declares {};
    auto input = BuildInput {};
    input.env.profile = "more";
    PackageRunner::setInput(recipe, input);
    recipe.describe();

    const auto& packages = recipe.project().packages;
    check(packages.size() == 3);

    if (packages.size() != 3)
        return;

    check(packages[0].customBuild);
    check(!packages[1].customBuild);
    check(!packages[2].customBuild);

    // The handle stays valid across later adds.
    check(packages[0].options.at("A_FLAG") == "OFF");
    check(packages[0].configs == Configs {"Debug"});
    check(packages[0].mode == "prebuilt");
    check(packages[0].abiMatch == "any");
    check(packages[1].options.at("B_FLAG") == "ON");

    const auto custom = recipe.createPackage("A");
    check(dynamic_cast<Custom*>(custom.get()) != nullptr);
    check(recipe.createPackage("B") == nullptr);
    check(recipe.createPackage("Z") == nullptr);

    check(Miro::toJSONString(packages[0]).find("\"customBuild\":true")
          != std::string::npos);
};

auto tPackageUserApi = test("Package/recipe/userFacingApi") = []
{
    auto project = MyProject {};
    PackageRunner::setInput(project, BuildInput {});
    project.describe();

    const auto& packages = project.project().packages;
    check(packages.size() == 3);
    check(dynamic_cast<Fmt*>(project.createPackage("fmt").get()) != nullptr);
    check(dynamic_cast<Zlib*>(project.createPackage("zlib").get()) != nullptr);
    check(project.createPackage("nlohmann_json") == nullptr);

    // Zlib's own steps, in source mode: no build, the given checkout.
    auto root = eacp::TemporaryFile {};
    auto zlib = project.createPackage("zlib");
    const auto output = runPackage(*zlib, packageInput(root.path(), "source"));
    check(output.ok() && output.mode == "source");

    // The profile adds a config to the same declaration, not a second one.
    auto debugDeps = MyProject {};
    auto input = BuildInput {};
    input.env.profile = "debug-deps";
    PackageRunner::setInput(debugDeps, input);
    debugDeps.describe();

    const auto& described = debugDeps.project();
    check(!projectError(described).has_value());
    check(described.packages.size() == 3);
    check(described.packages[0].configs == Configs {"Debug"});
};

auto tBuildProtocolJson = test("BuildProtocol/jsonRoundTrip") = []
{
    auto root = eacp::TemporaryFile {};
    auto input = packageInput(root.path());
    input.recipeName = "Mid";
    input.dependencies.add(
        {"Leaf", "prebuilt", "f00", "/p", "/p/epm-targets.cmake", {"Release"}});
    input.dependencyGraph = "set(epm_PACKAGES \"Leaf\")\n";
    input.verbose = true;

    check(saveJsonFile(root.path() / "in.json", input));
    const auto loaded = loadJsonFile<BuildInput>(root.path() / "in.json");
    check(loaded.has_value());

    if (loaded)
    {
        check(Miro::toJSONString(*loaded) == Miro::toJSONString(input));
        check(loaded->source.has_value());
        check(loaded->dependencies[0].targetsFile == "/p/epm-targets.cmake");
    }

    input.source.reset();
    check(saveJsonFile(root.path() / "in.json", input));
    const auto sourceless = loadJsonFile<BuildInput>(root.path() / "in.json");
    check(sourceless.has_value() && !sourceless->source.has_value());

    auto output = BuildOutput {};
    output.name = "Lib";
    output.mode = "prebuilt";
    output.prefix = "/p";
    output.configs = {"Debug", "Release"};
    output.fingerprintHash = "f00";
    check(saveJsonFile(root.path() / "out.json", output));
    const auto read = loadJsonFile<BuildOutput>(root.path() / "out.json");
    check(read.has_value()
          && Miro::toJSONString(*read) == Miro::toJSONString(output));
    check(read && read->ok());
};
