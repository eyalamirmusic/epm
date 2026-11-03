#include <epm/Cache/SourceCache.h>

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

EA::Vector<Git::RemoteRef> fixtureRefs()
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

    // printf 'git\nhttps://example.com/a.git\nabc' | shasum -a 256
    check(id == "0c5da1b8a17d809fbb79e5faaaf4bcb2815ea6c3e530f151d47e995703767985");
    check(id != sourceId("https://example.com/b.git", "abc"));
    check(id != sourceId("https://example.com/a.git", "abd"));
};
