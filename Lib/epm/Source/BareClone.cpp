#include <epm/Source/BareClone.h>
#include <epm/Lock/ErrorMessages.h>
#include <epm/Source/SourceSnapshot.h>
#include <epm/Util/Git.h>
#include <epm/Util/Text.h>
#include <epm/Util/Status.h>

#include <eacp/Core/Platform/Platform.h>
#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/TemporaryFile.h>
#include <eacp/Core/Utils/Strings.h>

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace epm
{
namespace
{
constexpr auto gitAttributesFileName = std::string_view {".gitattributes"};
constexpr auto gitSymlinkMode = "120000";
constexpr auto lfsFilterAttribute = std::string_view {"filter=lfs"};

EA::Vector<std::string> withGitDirectory(const eacp::FilePath& bareClone,
                                         const EA::Vector<std::string>& rest)
{
    auto arguments = EA::Vector<std::string> {"--git-dir=" + bareClone.str()};
    arguments.addFrom(rest);
    return arguments;
}

bool isGitAttributesPath(const std::string& path)
{
    return path == gitAttributesFileName
           || path.ends_with("/" + std::string {gitAttributesFileName});
}

// Windows checks symlinks out as plain files holding the link text, as Git for
// Windows does by default, so a checkout never depends on whether the account
// may create symlinks; the Merkle hash counts them as symlinks either way.
std::string getSymlinksSetting()
{
    if constexpr (eacp::Platform::isWindows())
        return "core.symlinks=false";

    return "core.symlinks=true";
}

std::string getGitLfsInstallCommand()
{
    if constexpr (eacp::Platform::isWindows())
        return "winget install GitHub.GitLFS";

    if constexpr (eacp::Platform::isMac())
        return "brew install git-lfs";

    return "sudo apt install git-lfs";
}

std::string_view removeSubmoduleKeySuffix(std::string_view key)
{
    for (auto suffix: {std::string_view {".path"}, std::string_view {".url"}})
    {
        if (key.ends_with(suffix))
            return key.substr(0, key.size() - suffix.size());
    }

    return {};
}

bool isRemoteRefName(const eacp::FilePath& bareClone, const std::string& ref)
{
    auto listed =
        Git::runOrThrow(withGitDirectory(bareClone, {"ls-remote", "origin", ref}));
    return !listed.empty();
}

void fetchEveryBranchAndTag(const eacp::FilePath& bareClone)
{
    auto arguments = withGitDirectory(
        bareClone,
        {"fetch", "--quiet", "--tags", "origin", "+refs/heads/*:refs/heads/*"});
    auto isShallow = Git::runOrThrow(
        withGitDirectory(bareClone, {"rev-parse", "--is-shallow-repository"}));

    if (isShallow == "true")
        arguments.add("--unshallow");

    Git::runOrThrow(arguments);
}
} // namespace

void createBareClone(const eacp::FilePath& bareClone, const std::string& url)
{
    if (eacp::Files::isDirectory(bareClone))
        return;

    Git::runOrThrow({"init", "--quiet", "--bare", bareClone.str()});
    Git::runOrThrow(withGitDirectory(bareClone, {"remote", "add", "origin", url}));
}

bool hasCommit(const eacp::FilePath& bareClone, const std::string& commit)
{
    auto arguments = withGitDirectory(
        bareClone, {"rev-parse", "--quiet", "--verify", commit + "^{commit}"});
    return Git::run(arguments).ok;
}

std::string fetchRefIntoBareClone(const eacp::FilePath& bareClone,
                                  const std::string& ref)
{
    if (isFullCommitHash(ref) || isRemoteRefName(bareClone, ref))
    {
        // A ref of this fetch's own rather than FETCH_HEAD, which configures
        // sharing the bareClone would overwrite under each other.
        auto fetchedRef = "refs/epm/" + eacp::TemporaryFile {}.path().fileName();
        Git::runOrThrow(withGitDirectory(bareClone,
                                         {"fetch",
                                          "--quiet",
                                          "--no-tags",
                                          "--depth",
                                          "1",
                                          "origin",
                                          "+" + ref + ":" + fetchedRef}));
        auto commit = Git::runOrThrow(
            withGitDirectory(bareClone, {"rev-parse", fetchedRef + "^{commit}"}));
        Git::runOrThrow(
            withGitDirectory(bareClone, {"update-ref", "-d", fetchedRef}));
        return commit;
    }

    fetchEveryBranchAndTag(bareClone);
    return Git::runOrThrow(
        withGitDirectory(bareClone, {"rev-parse", ref + "^{commit}"}));
}

GitTreeListing listTree(const eacp::FilePath& bareClone, const std::string& commit)
{
    auto listing = GitTreeListing {bareClone, commit};
    auto entries = Git::runOrThrow(withGitDirectory(
        bareClone, {"-c", "core.quotepath=off", "ls-tree", "-r", commit}));

    for (const auto line: splitLines(entries))
    {
        auto tab = line.find('\t');
        auto fields = eacp::Strings::split(line.substr(0, tab), ' ');
        auto path = std::string {line.substr(tab + 1)};
        constexpr auto modeField = 0;
        constexpr auto typeField = 1;
        constexpr auto shaField = 2;

        if (fields[modeField] == gitSymlinkMode)
            listing.symlinks.push_back(path);

        if (fields[typeField] == "commit")
            listing.gitlinks.push_back({path, fields[shaField]});
        else if (isGitAttributesPath(path))
            listing.attributeFiles.push_back(path);
    }

    return listing;
}

bool usesGitLfs(const GitTreeListing& listing)
{
    for (const auto& path: listing.attributeFiles)
    {
        auto attributes = Git::runOrThrow(withGitDirectory(
            listing.bareClone, {"cat-file", "-p", listing.commit + ":" + path}));

        if (attributes.find(lfsFilterAttribute) != std::string::npos)
            return true;
    }

    return false;
}

void fetchGitLfsObjects(const eacp::FilePath& bareClone, const std::string& commit)
{
    if (!Git::isLfsInstalled())
    {
        throw std::runtime_error(
            formatUserError("this repository stores files in Git LFS, but the "
                            "git-lfs package is not installed",
                            getGitLfsInstallCommand()));
    }

    Git::runOrThrow(withGitDirectory(bareClone, {"lfs", "fetch", "origin", commit}));
}

void checkOutCommit(const eacp::FilePath& bareClone,
                    const std::string& commit,
                    const eacp::FilePath& destination,
                    GitLfsFiles lfsFiles)
{
    // The lfs filter is set either way, so the machine's own git config never
    // decides whether a checkout holds pointer files or their content.
    auto isSmudging = lfsFiles == GitLfsFiles::content;
    auto smudge = std::string {isSmudging ? "git-lfs smudge -- %f" : ""};
    auto process = std::string {isSmudging ? "git-lfs filter-process" : ""};
    auto required = std::string {isSmudging ? "true" : "false"};
    auto indexFile = destination.sibling(destination.fileName() + ".index");
    auto arguments = withGitDirectory(bareClone,
                                      {"--work-tree=" + destination.str(),
                                       "-c",
                                       "core.autocrlf=false",
                                       "-c",
                                       "core.eol=lf",
                                       "-c",
                                       getSymlinksSetting(),
                                       "-c",
                                       "filter.lfs.smudge=" + smudge,
                                       "-c",
                                       "filter.lfs.process=" + process,
                                       "-c",
                                       "filter.lfs.required=" + required,
                                       "checkout",
                                       "--quiet",
                                       "--force",
                                       commit,
                                       "--",
                                       "."});

    Git::runOrThrow(arguments, {{"GIT_INDEX_FILE", indexFile.str()}});
    eacp::Files::removeFile(indexFile);
}

EA::Vector<GitSubmodule> listSubmodules(const GitTreeListing& listing,
                                        const eacp::FilePath& tree)
{
    auto modulesFile = tree / ".gitmodules";

    if (!eacp::Files::isFile(modulesFile))
        return {};

    // --list rather than --get-regexp, which exits 1 for a file with no entries.
    auto settings =
        Git::runOrThrow({"config", "--file", modulesFile.str(), "--list"});
    auto submodules = EA::Vector<GitSubmodule> {};

    for (const auto& [gitlinkPath, commit]: listing.gitlinks)
    {
        auto submodule = GitSubmodule {gitlinkPath, {}, commit};
        auto lines = splitLines(settings);

        for (const auto& line: lines)
        {
            auto equals = line.find('=');
            auto key = line.substr(0, equals);

            if (!key.ends_with(".path") || line.substr(equals + 1) != gitlinkPath)
                continue;

            auto urlKey = std::string {removeSubmoduleKeySuffix(key)} + ".url=";

            for (const auto& candidate: lines)
            {
                if (candidate.starts_with(urlKey))
                    submodule.url = std::string {candidate.substr(urlKey.size())};
            }
        }

        // A gitlink .gitmodules does not name is not a submodule git checks out.
        if (!submodule.url.empty())
            submodules.push_back(submodule);
    }

    return submodules;
}

} // namespace epm
