#include <PackageArguments.h>

#include <Submodules.h>

#include <NanoTest/NanoTest.h>

#include <stdexcept>

using namespace nano;
using lockstep::parsePackageArguments;

auto tGithubKeyword = test("Arguments/githubKeywordWithTag") = []
{
    auto spec = parsePackageArguments(
        {"NAME", "Miro", "GITHUB", "eyalamirmusic/Miro", "GIT_TAG", "main"});
    check(spec.name == "Miro");
    check(spec.kind == lockstep::PackageKind::git);
    check(spec.url == "https://github.com/eyalamirmusic/Miro.git");
    check(spec.ref == "main");
};

auto tGithubRepository = test("Arguments/githubRepositoryNamesFromRepo") = []
{
    auto spec = parsePackageArguments(
        {"GITHUB_REPOSITORY", "fmtlib/fmt", "GIT_TAG", "11.0.2"});
    check(spec.name == "fmt");
    check(spec.ref == "11.0.2");
};

auto tVersionBecomesTag = test("Arguments/versionBecomesVTag") = []
{
    auto spec = parsePackageArguments(
        {"NAME", "a", "GITLAB_REPOSITORY", "x/a", "VERSION", "1.2"});
    check(spec.url == "https://gitlab.com/x/a.git");
    check(spec.ref == "v1.2");
};

auto tShorthandVersion = test("Arguments/shorthandGithubAtVersion") = []
{
    auto spec = parsePackageArguments({"gh:cpm-cmake/testpack-adder@1.0.1"});
    check(spec.name == "testpack-adder");
    check(spec.url == "https://github.com/cpm-cmake/testpack-adder.git");
    check(spec.ref == "v1.0.1");
};

auto tShorthandTag = test("Arguments/shorthandTagWinsOverVersion") = []
{
    auto spec = parsePackageArguments({"gh:nlohmann/json@3.11.3#develop"});
    check(spec.ref == "develop");
};

auto tShorthandBitbucket = test("Arguments/shorthandBitbucketHashTag") = []
{
    auto spec = parsePackageArguments({"bb:team/repo#abc123"});
    check(spec.url == "https://bitbucket.org/team/repo.git");
    check(spec.ref == "abc123");
};

auto tShorthandGitUrl = test("Arguments/shorthandGitUrlAtVersion") = []
{
    auto spec = parsePackageArguments({"https://example.com/lib/thing.git@2.0"});
    check(spec.kind == lockstep::PackageKind::git);
    check(spec.url == "https://example.com/lib/thing.git");
    check(spec.name == "thing");
    check(spec.ref == "v2.0");
};

auto tShorthandSshUrl = test("Arguments/shorthandSshUrlKeepsItsAt") = []
{
    auto spec = parsePackageArguments({"git@github.com:owner/repo.git#v1"});
    check(spec.url == "git@github.com:owner/repo.git");
    check(spec.name == "repo");
    check(spec.ref == "v1");
};

auto tShorthandArchive = test("Arguments/shorthandArchiveIsUrl") = []
{
    auto spec = parsePackageArguments({"https://example.com/zlib-1.3.1.tar.gz"});
    check(spec.kind == lockstep::PackageKind::archive);
    check(spec.name == "zlib-1.3.1");
};

auto tUriKeyword = test("Arguments/uriKeywordWithOptions") = []
{
    auto spec = parsePackageArguments(
        {"URI", "gh:owner/lib@1.0", "OPTIONS", "LIB_TESTS OFF", "LIB_NAME a b"});
    check(spec.name == "lib");
    check(spec.ref == "v1.0");
    check(spec.options.size() == 2);
    check(spec.options[0].first == "LIB_TESTS");
    check(spec.options[0].second == "OFF");
    check(spec.options[1].second == "a b");
};

auto tFlagsTakeBooleans = test("Arguments/flagsTakeOptionalBooleans") = []
{
    auto spec = parsePackageArguments({"NAME",
                                       "a",
                                       "URL",
                                       "https://x/a.zip",
                                       "DOWNLOAD_ONLY",
                                       "YES",
                                       "EXCLUDE_FROM_ALL",
                                       "NO",
                                       "SYSTEM"});
    check(spec.isDownloadOnly);
    check(!spec.isExcludedFromAll);
    check(spec.isSystem);
};

auto tUnknownKeywordsIgnored = test("Arguments/unknownCpmKeywordsAreSkipped") = []
{
    auto spec = parsePackageArguments({"NAME",
                                       "a",
                                       "GITHUB_REPOSITORY",
                                       "o/a",
                                       "GIT_TAG",
                                       "v1",
                                       "GIT_SHALLOW",
                                       "TRUE",
                                       "SOURCE_SUBDIR",
                                       "cmake",
                                       "PATCHES",
                                       "a.patch",
                                       "b.patch"});
    check(spec.sourceSubdirectory == "cmake");
    check(spec.patches.size() == 2);
    check(spec.ref == "v1");
};

auto tFetchContentDetails = test("Arguments/fetchContentDeclareDetails") = []
{
    auto spec =
        parsePackageArguments({"NAME",
                               "adder",
                               "GIT_REPOSITORY",
                               "https://github.com/cpm-cmake/testpack-adder.git",
                               "GIT_TAG",
                               "v1.0.1",
                               "SOURCE_DIR",
                               "/tmp/somewhere"});
    check(spec.kind == lockstep::PackageKind::git);
    check(spec.name == "adder");
    check(spec.checkoutDirectory == "/tmp/somewhere");
};

auto tSourceDirOnlyIsLocal = test("Arguments/sourceDirAloneIsLocal") = []
{
    auto spec = parsePackageArguments({"NAME", "mine", "SOURCE_DIR", "../mine"});
    check(spec.kind == lockstep::PackageKind::local);
    check(spec.localSourceDirectory == "../mine");
};

auto tMissingSourceThrows = test("Arguments/noSourceThrows") = []
{
    auto threw = false;

    try
    {
        parsePackageArguments({"NAME", "a", "GIT_TAG", "v1"});
    }
    catch (const std::invalid_argument&)
    {
        threw = true;
    }

    check(threw);
};

auto tMissingTagThrows = test("Arguments/gitWithoutTagOrVersionThrows") = []
{
    auto threw = false;

    try
    {
        parsePackageArguments({"GITHUB_REPOSITORY", "o/a"});
    }
    catch (const std::invalid_argument&)
    {
        threw = true;
    }

    check(threw);
};

namespace PackageArgumentsTests
{
bool isRejected(const std::vector<std::string>& arguments,
                const std::string& expected)
{
    try
    {
        parsePackageArguments(arguments);
    }
    catch (const std::invalid_argument& error)
    {
        return std::string {error.what()}.find(expected) != std::string::npos;
    }

    return false;
}

std::vector<std::string> withGitSource(std::vector<std::string> extra)
{
    auto arguments = std::vector<std::string> {
        "NAME", "a", "GITHUB_REPOSITORY", "o/a", "GIT_TAG", "v1"};
    arguments.insert(arguments.end(), extra.begin(), extra.end());
    return arguments;
}
} // namespace PackageArgumentsTests

using PackageArgumentsTests::isRejected;
using PackageArgumentsTests::withGitSource;

auto tSubmodulesDefault = test("Arguments/submodulesDefaultToAllRecursive") = []
{
    auto spec = parsePackageArguments(withGitSource({}));
    check(!spec.submoduleSelection.isRestricted);
    check(spec.submoduleSelection.isRecursive);
};

auto tSubmodulesNamed = test("Arguments/gitSubmodulesNamesPaths") = []
{
    auto spec = parsePackageArguments(withGitSource({"GIT_SUBMODULES",
                                                     "external/a",
                                                     "external/b",
                                                     "GIT_SUBMODULES_RECURSE",
                                                     "OFF"}));
    check(spec.submoduleSelection.isRestricted);
    check(spec.submoduleSelection.paths.size() == 2);
    check(!spec.submoduleSelection.isRecursive);
};

auto tSubmodulesEmpty = test("Arguments/emptyGitSubmodulesMeansNone") = []
{
    auto spec =
        parsePackageArguments(withGitSource({"GIT_SUBMODULES", "EXCLUDE_FROM_ALL"}));
    check(spec.submoduleSelection.isRestricted);
    check(spec.submoduleSelection.paths.empty());
    check(spec.isExcludedFromAll);
};

auto tUrlHash = test("Arguments/urlHashSha256IsKept") = []
{
    auto spec = parsePackageArguments(
        {"NAME", "z", "URL", "https://x/z.tar.gz", "URL_HASH", "SHA256=ABCDEF"});
    check(spec.archiveHash.algorithm == "SHA256");
    check(spec.archiveHash.hex == "abcdef");
};

auto tUrlHashSha1 = test("Arguments/urlHashTakesEveryCMakeAlgorithm") = []
{
    auto spec = parsePackageArguments(
        {"NAME", "z", "URL", "https://x/z.tar.gz", "URL_HASH", "sha1=AB"});
    check(spec.archiveHash.algorithm == "SHA1");
    check(spec.archiveHash.hex == "ab");
    auto sha3 = parsePackageArguments(
        {"NAME", "z", "URL", "https://x/z.tar.gz", "URL_HASH", "SHA3_512=cd"});
    check(sha3.archiveHash.algorithm == "SHA3_512");
    auto md5 = parsePackageArguments(
        {"NAME", "z", "URL", "https://x/z.tar.gz", "URL_MD5", "ef"});
    check(md5.archiveHash.algorithm == "MD5");
};

auto tUrlHashUnknown = test("Arguments/urlHashUnknownAlgorithmRefused") = []
{
    check(isRejected(
        {"NAME", "z", "URL", "https://x/z.tar.gz", "URL_HASH", "CRC32=ab"},
        "URL_HASH"));
};

auto tOptionWithoutValue = test("Arguments/optionWithoutValueIsOn") = []
{
    auto spec = parsePackageArguments(
        withGitSource({"OPTIONS", "LIBIGL_WITH_CGAL", "FOO  bar "}));
    check(spec.options.size() == 2);
    check(spec.options[0].first == "LIBIGL_WITH_CGAL");
    check(spec.options[0].second == "ON");
    check(spec.options[1].second == "bar");
};

auto tIgnored = test("Arguments/shallowNoCacheAndCacheKeyAreAccepted") = []
{
    auto spec = parsePackageArguments(withGitSource({"GIT_SHALLOW",
                                                     "TRUE",
                                                     "NO_CACHE",
                                                     "YES",
                                                     "CUSTOM_CACHE_KEY",
                                                     "k",
                                                     "SYSTEM"}));
    check(spec.isSystem);
};

auto tRefused = test("Arguments/commandsAndForceAreRefusedByName") = []
{
    check(isRejected(withGitSource({"DOWNLOAD_COMMAND", "curl", "x"}),
                     "DOWNLOAD_COMMAND"));
    check(isRejected(withGitSource({"UPDATE_COMMAND", "git", "pull"}),
                     "UPDATE_COMMAND"));
    check(isRejected(withGitSource({"PATCH_COMMAND", "git", "apply"}),
                     "PATCH_COMMAND"));
    check(isRejected(withGitSource({"FORCE", "YES"}), "FORCE"));
};

auto tUnknown = test("Arguments/unknownOptionIsNamed") = []
{ check(isRejected(withGitSource({"GIT_FROBNICATE", "x"}), "GIT_FROBNICATE")); };

auto tCpmSourceDir = test("Arguments/sourceDirWithRepositoryIsWhereItIsCloned") = []
{
    auto spec = parsePackageArguments(withGitSource({"SOURCE_DIR", "../a"}));
    check(spec.kind == lockstep::PackageKind::git);
    check(spec.checkoutDirectory == "../a");
    check(spec.localSourceDirectory.empty());
};

auto tRelativeUrls = test("Submodules/relativeUrlsResolveAgainstParent") = []
{
    using lockstep::resolveSubmoduleUrl;
    check(resolveSubmoduleUrl("https://github.com/a/b.git", "../c.git")
          == "https://github.com/a/c.git");
    check(resolveSubmoduleUrl("https://github.com/a/b", "../../x/c")
          == "https://github.com/x/c");
    check(resolveSubmoduleUrl("https://github.com/a/b.git", "./sub")
          == "https://github.com/a/b.git/sub");
    check(resolveSubmoduleUrl("https://github.com/a/b.git", "https://other/x.git")
          == "https://other/x.git");
    check(resolveSubmoduleUrl("file:///tmp/fixtures/top", "../leaf")
          == "file:///tmp/fixtures/leaf");
};

auto tMiniaudio = test("Arguments/makeASoundMiniaudioCallIsAccepted") = []
{
    auto spec = parsePackageArguments(
        {"NAME",
         "miniaudio",
         "URL",
         "https://github.com/mackron/miniaudio/archive/refs/tags/0.11.25.zip",
         "DOWNLOAD_ONLY",
         "YES",
         "DOWNLOAD_NO_PROGRESS",
         "YES",
         "QUIET"});
    check(spec.kind == lockstep::PackageKind::archive);
    check(spec.isDownloadOnly);
    check(spec.url
          == "https://github.com/mackron/miniaudio/archive/refs/tags/0.11.25.zip");
};

auto tIgnoredSweep = test("Arguments/everyIgnoredKeywordIsAcceptedWithAValue") = []
{
    for (auto keyword: lockstep::listIgnoredKeywords())
    {
        auto bare = parsePackageArguments(withGitSource({std::string {keyword}}));
        auto valued = parsePackageArguments(
            withGitSource({std::string {keyword}, "first", "second", "SYSTEM"}));
        check(bare.kind == lockstep::PackageKind::git);
        check(valued.isSystem);
    }
};

auto tOtherSources = test("Arguments/sourcesTheLockCannotHoldAreRefusedByName") = []
{
    check(isRejected({"NAME", "a", "SVN_REPOSITORY", "svn://x"}, "SVN_REPOSITORY"));
    check(isRejected(
        {"NAME", "a", "URL", "https://x/a.zip", "DOWNLOAD_NO_EXTRACT", "YES"},
        "DOWNLOAD_NO_EXTRACT"));
};
