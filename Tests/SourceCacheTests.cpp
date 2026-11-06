#include <epm/Cache/SourceCache.h>
#include <epm/Util/Command.h>
#include <epm/Util/Text.h>

#include <eacp/Core/Utils/Environment.h>
#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/Strings.h>
#include <eacp/Core/Utils/TemporaryFile.h>

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace epm;

namespace
{
constexpr auto refsFixture =
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\tHEAD\n"
    "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\trefs/heads/main\n"
    "cccccccccccccccccccccccccccccccccccccccc\trefs/heads/v1.0\n"
    "dddddddddddddddddddddddddddddddddddddddd\trefs/tags/v1.0\n"
    "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee\trefs/tags/v1.0^{}\n"
    "ffffffffffffffffffffffffffffffffffffffff\trefs/tags/v2.0\n"
    "1212121212121212121212121212121212121212\trefs/heads/v2.0\n"
    "3434343434343434343434343434343434343434\trefs/heads/release/3\n";

eacp::Vector<Git::RemoteRef> fixtureRefs()
{
    return Git::parseLsRemote(refsFixture);
}
} // namespace

auto tPeeledTagWins = test("SourceCache/resolveRef/peeledTagFirst") = []
{
    check(resolveRef(fixtureRefs(), "v1.0")
          == std::string {"eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"});
};

auto tTagBeforeBranch = test("SourceCache/resolveRef/tagBeforeBranch") = []
{
    check(resolveRef(fixtureRefs(), "v2.0")
          == std::string {"ffffffffffffffffffffffffffffffffffffffff"});
};

auto tBranch = test("SourceCache/resolveRef/branch") = []
{
    check(resolveRef(fixtureRefs(), "main")
          == std::string {"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"});
    check(resolveRef(fixtureRefs(), "release/3")
          == std::string {"3434343434343434343434343434343434343434"});
};

auto tExactRef = test("SourceCache/resolveRef/exactRef") = []
{
    check(resolveRef(fixtureRefs(), "HEAD")
          == std::string {"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"});
};

auto tFullHashPassthrough = test("SourceCache/resolveRef/fullHashPassesThrough") = []
{
    const auto hash = std::string {"0123456789abcdef0123456789abcdef01234567"};

    check(isFullCommitHash(hash));
    check(resolveRef({}, hash) == hash);
};

auto tUnknownRef = test("SourceCache/resolveRef/unknown") = []
{
    check(!resolveRef(fixtureRefs(), "nope").has_value());
    check(!resolveRef(fixtureRefs(), "0123456").has_value());
    check(!isFullCommitHash("0123456789abcdef0123456789abcdef0123456g"));
};

auto tSourceId = test("SourceCache/sourceId") = []
{
    const auto id = sourceId("https://example.com/a.git", "abc");

    // printf 'git\nexample.com/a\nabc' | shasum -a 256
    check(id == "0cf66cbf7cfff180a690dcc6ead020aaebf8edbc31b70d0f62040a8386e18cc1");
    check(id != sourceId("https://example.com/b.git", "abc"));
    check(id != sourceId("https://example.com/a.git", "abd"));
    check(id != sourceId("https://example.org/a.git", "abc"));
};

// A laptop's ssh remote and CI's https one share snapshots and entries.
auto tSourceIdSpellings = test("SourceCache/sourceId/urlSpellings") = []
{
    const auto id = sourceId("https://github.com/owner/repo", "abc");

    check(sourceId("git@github.com:owner/repo.git", "abc") == id);
    check(sourceId("ssh://git@github.com/owner/repo.git", "abc") == id);
    check(sourceId("https://github.com/Owner/Repo.git/", "abc") == id);
    check(sourceId("git@github.com:other/repo.git", "abc") != id);
    check(sourceId("git@github.com:owner/repo.git", "abd") != id);
};

namespace
{
// A repository at `directory` with main (a commit holding a, then one adding
// b) and other (one more, adding c), and epm.cpp beside an include it names;
// the first commit of main, or empty without git.
std::string makeRepository(const eacp::FilePath& directory)
{
    const auto git = findExecutable("git");

    if (!git)
        return {};

    const auto run = [&](eacp::Vector<std::string> arguments)
    {
        auto all = eacp::Vector<std::string> {"-c",
                                              "user.name=epm",
                                              "-c",
                                              "user.email=epm@example.invalid",
                                              "-c",
                                              "commit.gpgsign=false"};
        all.addFrom(arguments);
        return runCommand(git->str(), all, directory);
    };

    check(writeTextFile(directory / "a", "a\n"));
    check(writeTextFile(directory / "epm.cpp", "#include \"helper.h\"\n"));
    check(writeTextFile(directory / "helper.h", "#pragma once\n"));

    if (!run({"init", "-q", "-b", "main"}).ok || !run({"add", "-A"}).ok
        || !run({"commit", "-q", "-m", "a"}).ok)
        return {};

    const auto first = run({"rev-parse", "HEAD"});
    check(writeTextFile(directory / "b", "b\n"));
    check(run({"add", "-A"}).ok && run({"commit", "-q", "-m", "b"}).ok);
    check(run({"checkout", "-q", "-b", "other"}).ok);
    check(writeTextFile(directory / "c", "c\n"));
    check(run({"add", "-A"}).ok && run({"commit", "-q", "-m", "c"}).ok);
    check(run({"checkout", "-q", "main"}).ok);

    return std::string {eacp::Strings::trim(first.output)};
}

// Git configuration for every git this process runs, through the
// environment; undone on destruction.
struct GitEnvironment
{
    explicit GitEnvironment(const eacp::FilePath& trace)
    {
        eacp::setEnv("GIT_CONFIG_COUNT", "1");
        eacp::setEnv("GIT_CONFIG_KEY_0", "protocol.version");
        eacp::setEnv("GIT_CONFIG_VALUE_0", "0");
        eacp::setEnv("GIT_TRACE", trace.str());
    }

    ~GitEnvironment()
    {
        for (const auto* name: {"GIT_CONFIG_COUNT",
                                "GIT_CONFIG_KEY_0",
                                "GIT_CONFIG_VALUE_0",
                                "GIT_TRACE"})
            eacp::unsetEnv(name);
    }
};
} // namespace

// Protocol v0 refuses a commit that is no branch tip: the fallback fetches
// the branch it was resolved from, not every branch and tag.
auto tFetchCommitFallback =
    test("SourceCache/fetchCommit/fallbackFetchesTheRef") = []
{
    auto root = eacp::TemporaryFile {};
    const auto repository = root.path() / "repo";
    const auto first = makeRepository(repository);

    if (first.empty())
        return;

    const auto cache = CacheRoot {root.path() / "cache"};
    const auto trace = root.path() / "trace.txt";
    auto fetched = SourceResult {};

    {
        const auto environment = GitEnvironment {trace};
        fetched = fetchCommit(cache, "Repo", repository.str(), "main", first);
    }

    check(fetched.ok);
    check(fetched.snapshot.commit == first);
    check(eacp::Files::isFile(fetched.snapshot.directory / "a"));
    check(!eacp::Files::exists(fetched.snapshot.directory / "b"));

    const auto log = eacp::Files::readFile(trace);
    check(log.find("fetch -q origin main") != std::string::npos);
    check(log.find("refs/heads/*") == std::string::npos);
};

// A dependency's recipe is its epm.cpp alone, whether the snapshot is in the
// cache or not: never compiled from beside the rest of its repository.
auto tManifestAlone = test("SourceCache/fetchManifest/aRecipeIsOneFile") = []
{
    auto root = eacp::TemporaryFile {};
    const auto repository = root.path() / "repo";
    const auto first = makeRepository(repository);

    if (first.empty())
        return;

    const auto alone = CacheRoot {root.path() / "alone"};
    const auto fromRepository =
        fetchManifest(alone, "Repo", repository.str(), first);
    check(fromRepository.ok);
    check(eacp::Files::isFile(fromRepository.manifestDirectory / "epm.cpp"));
    check(!eacp::Files::exists(fromRepository.manifestDirectory / "helper.h"));

    // With the whole snapshot in the cache, the same: epm.cpp copied alone.
    const auto cached = CacheRoot {root.path() / "cached"};
    check(fetchSource(cached, "Repo", repository.str(), first).ok);
    const auto fromSnapshot = fetchManifest(cached, "Repo", repository.str(), first);
    check(fromSnapshot.ok);
    check(fromSnapshot.manifestDirectory
          == cached.src() / "Repo" / ".recipes"
                 / sourceId(repository.str(), first).substr(0, 12));
    check(eacp::Files::isFile(fromSnapshot.manifestDirectory / "epm.cpp"));
    check(!eacp::Files::exists(fromSnapshot.manifestDirectory / "helper.h"));

    // Staged inside .recipes, which holds nothing else once it is done.
    check(eacp::Files::listDirectory(cached.src() / "Repo" / ".recipes").size()
          == 1);
};
