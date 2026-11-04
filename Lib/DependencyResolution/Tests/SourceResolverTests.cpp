#include "TestDirectory.h"

#include <DependencyResolution/DependencyResolution.h>
#include <DependencyResolution/Util/Command.h>

#include <eacp/Core/Utils/StdPath.h>

#include <NanoTest/NanoTest.h>

#include <filesystem>
#include <stdexcept>

using namespace nano;
using namespace DependencyResolution;

namespace SourceResolverTests
{
constexpr auto firstTag = "v1.0.0";
constexpr auto secondTag = "v1.1.0";
constexpr auto pinnedRecipe = "epm.cpp";

std::string runGit(const eacp::FilePath& repository,
                   const EA::Vector<std::string>& arguments)
{
    auto all = EA::Vector<std::string> {"-C",
                                        repository.str(),
                                        "-c",
                                        "user.name=epm",
                                        "-c",
                                        "user.email=epm@example.invalid",
                                        "-c",
                                        "commit.gpgsign=false"};

    for (const auto& argument: arguments)
        all.add(argument);

    auto git = findExecutable("git");

    if (!git)
        throw std::runtime_error("no git on PATH");

    auto result = runCommand(git->str(), all);

    if (!result.ok)
        throw std::runtime_error("git failed: " + result.output);

    return std::string {eacp::Strings::trim(result.output)};
}

void commitEverything(const eacp::FilePath& repository)
{
    runGit(repository, {"add", "-A"});
    runGit(repository, {"commit", "--quiet", "-m", "fixture"});
}

// A directory with a local repository `shared` tagged v1.0.0 and v1.1.0 (each
// with its own epm.cpp), a cache and the lock's path.
struct Fixture
{
    explicit Fixture(const std::string& name)
        : root(createEmptyTestDirectory("resolver-" + name))
    {
        writeTestFile(shared() / "shared.txt", "one\n");
        writeTestFile(shared() / pinnedRecipe, "// recipe one\n");
        runGit(shared(), {"init", "--quiet", "-b", "main"});
        commitEverything(shared());
        runGit(shared(), {"tag", firstTag});
        firstCommit = runGit(shared(), {"rev-parse", "HEAD"});
        writeTestFile(shared() / "shared.txt", "two\n");
        commitEverything(shared());
        runGit(shared(), {"tag", secondTag});
        secondCommit = runGit(shared(), {"rev-parse", "HEAD"});
    }

    eacp::FilePath shared() const { return root / "shared.git"; }
    eacp::FilePath lock() const { return root / "epm-lock.json"; }

    LockOptions options(LockMode mode = LockMode::locked) const
    {
        auto result = LockOptions {};
        result.lockFile = lock();
        result.cacheRoot = root / "cache";
        result.cmake = eacp::FilePath {DEPENDENCY_RESOLUTION_TEST_CMAKE};
        result.mode = mode;
        result.pinnedFiles = {pinnedRecipe};
        return result;
    }

    PlannedSource request(const std::string& requestedBy,
                          const std::string& ref) const
    {
        return {"shared", requestedBy, "file://" + shared().str(), ref, false};
    }

    eacp::FilePath root;
    std::string firstCommit;
    std::string secondCommit;
};

std::string resolveError(const EA::Vector<PlannedSource>& requests,
                         const LockOptions& options)
{
    try
    {
        resolveSources(requests, options);
    }
    catch (const std::exception& error)
    {
        return error.what();
    }

    return {};
}

bool contains(const std::string& text, const std::string& part)
{
    return text.find(part) != std::string::npos;
}

auto tWritesAndReadsTheLock =
    test("SourceResolver/lockedModeWritesAMissingLockThenReadsIt") = []
{
    auto fixture = Fixture {"writes"};
    auto requests = EA::Vector<PlannedSource> {fixture.request("root", firstTag)};

    auto first = resolveSources(requests, fixture.options());
    check(first.packages.size() == 1);
    check(first.packages[0].commit == fixture.firstCommit);
    check(eacp::Files::isFile(first.packages[0].sourceDirectory / "shared.txt"));
    check(eacp::Files::isFile(fixture.lock()));

    auto written = eacp::Files::readFile(fixture.lock());
    auto second = resolveSources(requests, fixture.options());
    check(second.packages[0].hash == first.packages[0].hash);
    check(second.rootHash == first.rootHash);
    check(eacp::Files::readFile(fixture.lock()) == written);
};

auto tMinimalVersionSelection =
    test("SourceResolver/selectsTheHighestTagAcrossRequesters") = []
{
    auto fixture = Fixture {"selection"};
    auto result = resolveSources(
        {fixture.request("a", firstTag), fixture.request("b", secondTag)},
        fixture.options());

    check(result.packages[0].commit == fixture.secondCommit);
};

auto tConflictAndOverride =
    test("SourceResolver/twoCommitsConflictUntilTheRootOverrides") = []
{
    auto fixture = Fixture {"conflict"};
    auto requests =
        EA::Vector<PlannedSource> {fixture.request("a", fixture.firstCommit),
                                   fixture.request("b", fixture.secondCommit)};

    auto error = resolveError(requests, fixture.options());
    check(contains(error, "disagree on shared"));
    check(contains(error, "a asks for"));
    check(contains(error, "b asks for"));

    auto options = fixture.options();
    options.overrides["shared"].commit = fixture.secondCommit;
    auto result = resolveSources(requests, options);
    check(result.packages[0].commit == fixture.secondCommit);
    check(contains(eacp::Files::readFile(fixture.lock()), "\"isOverridden\": true"));
};

auto tContinuousIntegration =
    test("SourceResolver/continuousIntegrationRefusesToWriteTheLock") = []
{
    auto fixture = Fixture {"ci"};
    auto options = fixture.options();
    options.isContinuousIntegration = true;

    check(contains(resolveError({fixture.request("root", firstTag)}, options),
                   "does not exist"));

    resolveSources({fixture.request("root", firstTag)}, fixture.options());
    auto late = PlannedSource {
        "late", "root", "file://" + fixture.shared().str(), firstTag, false};

    check(contains(resolveError({fixture.request("root", firstTag), late}, options),
                   "late"));
};

auto tLockedAddsMissing =
    test("SourceResolver/lockedModeAddsAPackageTheLockLacks") = []
{
    auto fixture = Fixture {"adds"};
    resolveSources({fixture.request("root", firstTag)}, fixture.options());

    auto late = PlannedSource {
        "late", "root", "file://" + fixture.shared().str(), secondTag, false};
    auto result =
        resolveSources({fixture.request("root", firstTag), late}, fixture.options());

    check(result.packages.size() == 2);
    check(contains(eacp::Files::readFile(fixture.lock()), "\"name\": \"late\""));
};

auto tChangedRequest = test("SourceResolver/aRequestTheLockDoesNotRecordStops") = []
{
    auto fixture = Fixture {"changed"};
    resolveSources({fixture.request("root", firstTag)}, fixture.options());

    check(contains(
        resolveError({fixture.request("root", secondTag)}, fixture.options()),
        "shared"));

    auto updated = resolveSources({fixture.request("root", secondTag)},
                                  fixture.options(LockMode::update));
    check(updated.packages[0].commit == fixture.secondCommit);
};

auto tOffline = test("SourceResolver/offlineNeedsALock") = []
{
    auto fixture = Fixture {"offline"};
    auto requests = EA::Vector<PlannedSource> {fixture.request("root", firstTag)};

    check(!resolveError(requests, fixture.options(LockMode::offline)).empty());

    resolveSources(requests, fixture.options());
    auto offline = resolveSources(requests, fixture.options(LockMode::offline));
    check(offline.packages[0].commit == fixture.firstCommit);
};

auto tSystemPackages = test("SourceResolver/systemPackagesAreNotFetched") = []
{
    auto fixture = Fixture {"system"};
    auto threads = PlannedSource {"Threads", "root", {}, {}, true};
    auto result = resolveSources({fixture.request("root", firstTag), threads},
                                 fixture.options());

    check(result.packages.size() == 1);
    check(result.systemPackages == EA::Vector<std::string> {"Threads"});
    check(!contains(eacp::Files::readFile(fixture.lock()), "Threads"));
};

// The cache trusts an entry whose files keep their sizes and modification
// times; a same-size edit with the time put back is only caught by the hash
// the lock pins.
auto tPinnedFile = test("SourceResolver/aPinnedFileChangedInTheCacheIsRefused") = []
{
    auto fixture = Fixture {"pinned"};
    auto requests = EA::Vector<PlannedSource> {fixture.request("root", firstTag)};
    auto result = resolveSources(requests, fixture.options());
    auto directory = result.packages[0].sourceDirectory;
    auto resolver = SourceResolver {fixture.options()};

    resolver.verifyPinnedFile("shared", directory, pinnedRecipe);

    auto recipe = eacp::toStdPath(directory / pinnedRecipe);
    auto written = std::filesystem::last_write_time(recipe);
    eacp::Files::writeFile(directory / pinnedRecipe, "// recipe two\n");
    std::filesystem::last_write_time(recipe, written);

    auto again = resolveSources(requests, fixture.options());
    check(again.packages[0].sourceDirectory == directory);

    auto error = std::string {};

    try
    {
        resolver.verifyPinnedFile("shared", directory, pinnedRecipe);
    }
    catch (const std::exception& thrown)
    {
        error = thrown.what();
    }

    check(contains(error, "shared's epm.cpp"));
};
} // namespace SourceResolverTests
