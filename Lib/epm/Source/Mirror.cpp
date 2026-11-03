#include <epm/Source/Mirror.h>
#include <epm/Lock/Messages.h>
#include <epm/Util/Git.h>
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
constexpr auto gitExecutable = "git";
constexpr auto fullShaLength = 40;
constexpr auto gitAttributesFileName = std::string_view {".gitattributes"};
constexpr auto lfsFilterAttribute = std::string_view {"filter=lfs"};

std::string joinArguments(const eacp::Vector<std::string>& arguments)
{
    return std::string {gitExecutable} + ' ' + eacp::Strings::join(arguments, " ");
}

eacp::Vector<std::string>
    createMirrorArguments(const eacp::FilePath& mirror,
                          const eacp::Vector<std::string>& rest)
{
    auto arguments = eacp::Vector<std::string> {"--git-dir=" + mirror.str()};
    arguments.addFrom(rest);
    return arguments;
}

std::string runGitOrThrow(
    const eacp::Vector<std::string>& arguments,
    const eacp::Vector<eacp::Processes::EnvironmentVariable>& environment = {})
{
    auto output = runGit(arguments, environment);

    if (!output)
        throw std::runtime_error(joinArguments(arguments) + " failed");

    return *output;
}

eacp::Vector<std::string> splitLines(const std::string& text)
{
    auto lines = eacp::Vector<std::string> {};

    for (const auto& line: eacp::Strings::split(text, '\n'))
    {
        if (auto trimmed = eacp::Strings::trim(line); !trimmed.empty())
            lines.add(trimmed);
    }

    return lines;
}

bool isGitAttributesPath(const std::string& path)
{
    return path == gitAttributesFileName
           || path.ends_with("/" + std::string {gitAttributesFileName});
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

bool isFullSha(const std::string& ref)
{
    return ref.size() == fullShaLength
           && std::all_of(
               ref.begin(), ref.end(), [](char c) { return std::isxdigit(c); });
}

bool isRemoteRefName(const eacp::FilePath& mirror, const std::string& ref)
{
    auto listed =
        runGitOrThrow(createMirrorArguments(mirror, {"ls-remote", "origin", ref}));
    return !listed.empty();
}

void fetchEveryBranchAndTag(const eacp::FilePath& mirror)
{
    auto arguments = createMirrorArguments(
        mirror,
        {"fetch", "--quiet", "--tags", "origin", "+refs/heads/*:refs/heads/*"});
    auto isShallow = runGitOrThrow(
        createMirrorArguments(mirror, {"rev-parse", "--is-shallow-repository"}));

    if (isShallow == "true")
        arguments.add("--unshallow");

    runGitOrThrow(arguments);
}
} // namespace

std::optional<std::string>
    runGit(const eacp::Vector<std::string>& arguments,
           const eacp::Vector<eacp::Processes::EnvironmentVariable>& environment)
{
    auto result = Git::run(arguments, environment);

    if (result.ok)
        return eacp::Strings::trim(result.output);

    auto errorOutput = eacp::Strings::trim(result.error);

    if (!errorOutput.empty())
        printStatus(joinArguments(arguments) + ":\n" + errorOutput);

    return std::nullopt;
}

void createBareMirror(const eacp::FilePath& mirror, const std::string& url)
{
    if (eacp::Files::isDirectory(mirror))
        return;

    runGitOrThrow({"init", "--quiet", "--bare", mirror.str()});
    runGitOrThrow(createMirrorArguments(mirror, {"remote", "add", "origin", url}));
}

bool hasCommit(const eacp::FilePath& mirror, const std::string& commit)
{
    auto arguments = createMirrorArguments(
        mirror, {"rev-parse", "--quiet", "--verify", commit + "^{commit}"});
    return runGit(arguments).has_value();
}

std::string fetchRefIntoMirror(const eacp::FilePath& mirror, const std::string& ref)
{
    if (isFullSha(ref) || isRemoteRefName(mirror, ref))
    {
        // A ref of this fetch's own rather than FETCH_HEAD, which configures
        // sharing the mirror would overwrite under each other.
        auto fetchedRef = "refs/epm/" + eacp::TemporaryFile {}.path().fileName();
        runGitOrThrow(createMirrorArguments(mirror,
                                            {"fetch",
                                             "--quiet",
                                             "--no-tags",
                                             "--depth",
                                             "1",
                                             "origin",
                                             "+" + ref + ":" + fetchedRef}));
        auto commit = runGitOrThrow(
            createMirrorArguments(mirror, {"rev-parse", fetchedRef + "^{commit}"}));
        runGitOrThrow(
            createMirrorArguments(mirror, {"update-ref", "-d", fetchedRef}));
        return commit;
    }

    fetchEveryBranchAndTag(mirror);
    return runGitOrThrow(
        createMirrorArguments(mirror, {"rev-parse", ref + "^{commit}"}));
}

GitTreeListing listTree(const eacp::FilePath& mirror, const std::string& commit)
{
    auto listing = GitTreeListing {mirror, commit};
    auto entries = runGitOrThrow(createMirrorArguments(
        mirror, {"-c", "core.quotepath=off", "ls-tree", "-r", commit}));

    for (const auto& line: splitLines(entries))
    {
        auto tab = line.find('\t');
        auto fields = eacp::Strings::split(line.substr(0, tab), ' ');
        auto path = line.substr(tab + 1);
        constexpr auto typeField = 1;
        constexpr auto shaField = 2;

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
        auto attributes = runGitOrThrow(createMirrorArguments(
            listing.mirror, {"cat-file", "-p", listing.commit + ":" + path}));

        if (attributes.find(lfsFilterAttribute) != std::string::npos)
            return true;
    }

    return false;
}

void fetchGitLfsObjects(const eacp::FilePath& mirror, const std::string& commit)
{
    if (!runGit({"lfs", "version"}))
    {
        throw std::runtime_error(
            formatUserError("this repository stores files in Git LFS, but the "
                            "git-lfs package is not installed",
                            getGitLfsInstallCommand()));
    }

    runGitOrThrow(createMirrorArguments(mirror, {"lfs", "fetch", "origin", commit}));
}

void checkOutCommit(const eacp::FilePath& mirror,
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
    auto arguments = createMirrorArguments(mirror,
                                           {"--work-tree=" + destination.str(),
                                            "-c",
                                            "core.autocrlf=false",
                                            "-c",
                                            "core.eol=lf",
                                            "-c",
                                            "core.symlinks=true",
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

    runGitOrThrow(arguments, {{"GIT_INDEX_FILE", indexFile.str()}});
    eacp::Files::removeFile(indexFile);
}

std::vector<GitSubmodule> listSubmodules(const GitTreeListing& listing,
                                         const eacp::FilePath& tree)
{
    auto modulesFile = tree / ".gitmodules";

    if (!eacp::Files::isFile(modulesFile))
        return {};

    // --list rather than --get-regexp, which exits 1 for a file with no entries.
    auto settings = runGitOrThrow({"config", "--file", modulesFile.str(), "--list"});
    auto submodules = std::vector<GitSubmodule> {};

    for (const auto& [gitlinkPath, commit]: listing.gitlinks)
    {
        auto submodule = GitSubmodule {gitlinkPath, {}, commit};
        auto lines = splitLines(settings);

        for (const auto& line: lines)
        {
            auto equals = line.find('=');
            auto key = std::string_view {line}.substr(0, equals);

            if (!key.ends_with(".path") || line.substr(equals + 1) != gitlinkPath)
                continue;

            auto urlKey = std::string {removeSubmoduleKeySuffix(key)} + ".url=";

            for (const auto& candidate: lines)
            {
                if (candidate.starts_with(urlKey))
                    submodule.url = candidate.substr(urlKey.size());
            }
        }

        // A gitlink .gitmodules does not name is not a submodule git checks out.
        if (!submodule.url.empty())
            submodules.push_back(submodule);
    }

    return submodules;
}

eacp::FilePath findGitBundledPatch()
{
    // Git for Windows ships patch.exe in usr/bin beside libexec/git-core, which
    // is where CPM looks too.
    constexpr auto levelsAboveGitInstall = 3;
    auto execPath = runGit({"--exec-path"});

    if (!execPath)
        return {};

    auto install = eacp::FilePath {*execPath};

    for (auto level = 0; level < levelsAboveGitInstall; ++level)
        install = install.parentDirectory();

    return install / "usr" / "bin" / "patch.exe";
}
} // namespace epm
