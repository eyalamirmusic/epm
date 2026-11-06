#include "SourceCache.h"
#include "PackageLock.h"

#include <epm/Spec/JsonFile.h>
#include <epm/Spec/Manifest.h>
#include <epm/Util/Log.h>
#include <epm/Util/Paths.h>
#include <epm/Util/Sha256.h>
#include <epm/Util/Timestamp.h>

#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/StdPath.h>
#include <eacp/Core/Utils/TemporaryFile.h>

#include <filesystem>

namespace epm
{
using namespace eacp;

namespace
{
// A package's recipe, as Graph/ManifestLoader.h names it.
constexpr auto manifestFileName = "epm.cpp";

struct ResolvedCommit
{
    std::optional<std::string> commit;
    std::string error;
};

ResolvedCommit resolveCommit(const std::string& url, const std::string& ref)
{
    if (isFullCommitHash(ref))
        return {ref, {}};

    const auto remote = Git::lsRemote(url);

    if (!remote.ok)
        return {std::nullopt, remote.error};

    if (auto commit = resolveRef(remote.refs, ref))
        return {commit, {}};

    return {std::nullopt, "no tag or branch by that name"};
}

std::string firstLine(const std::string& text)
{
    return text.substr(0, text.find('\n'));
}

std::optional<std::string> runGit(const FilePath& directory,
                                  const Vector<std::string>& arguments)
{
    const auto result = Git::run(directory, arguments);

    if (result.ok)
        return std::nullopt;

    auto message = firstLine(result.message());
    return "git " + arguments[0] + " failed"
           + (message.empty() ? "" : ": " + message);
}

// Shallow by commit first; servers that refuse to serve an unadvertised commit
// get a full fetch of the ref the commit was resolved from (or of every
// branch and tag when that ref was a bare hash).
std::optional<std::string> checkoutCommit(const FilePath& directory,
                                          const std::string& url,
                                          const std::string& ref,
                                          const std::string& commit)
{
    if (auto error = runGit(directory, {"init", "-q"}))
        return error;

    if (auto error = runGit(directory, {"remote", "add", "origin", url}))
        return error;

    if (!runGit(directory, {"fetch", "-q", "--depth", "1", "origin", commit}))
        return runGit(directory, {"checkout", "-q", "--detach", "FETCH_HEAD"});

    Log::step("epm: shallow fetch by commit refused, fetching " + ref);

    const auto fallback =
        ref.empty() || isFullCommitHash(ref)
            ? Vector<std::string> {"fetch",
                                   "-q",
                                   "--tags",
                                   "origin",
                                   "+refs/heads/*:refs/remotes/origin/*"}
            : Vector<std::string> {"fetch", "-q", "origin", ref};

    if (auto error = runGit(directory, fallback))
        return error;

    return runGit(directory, {"checkout", "-q", "--detach", commit});
}

std::optional<std::string> updateSubmodules(const FilePath& directory)
{
    if (!Files::isFile(directory / ".gitmodules"))
        return std::nullopt;

    return runGit(directory,
                  {"submodule", "update", "--init", "--recursive", "--depth", "1"});
}

// The top-level .git directory and the .git files submodules leave behind.
bool removeGitMetadata(const FilePath& directory)
{
    auto options = Files::DirectoryOptions {};
    options.recursive = true;
    options.includeHidden = true;

    auto gitEntries = Vector<FilePath> {};

    Files::forEachEntry(directory,
                        options,
                        [&](const Files::DirectoryEntry& entry)
                        {
                            if (entry.path.fileName() != ".git")
                                return Files::Visit::next;

                            gitEntries.add(entry.path);
                            return Files::Visit::skipChildren;
                        });

    for (const auto& entry: gitEntries)
        if (!Files::removeAll(entry))
            return false;

    return true;
}

SourceResult failure(std::string error)
{
    auto result = SourceResult {};
    result.error = std::move(error);
    return result;
}

// src/<name>/<sourceId12>.json, written last, so its presence alone means the
// snapshot is complete.
FilePath markerOf(const FilePath& snapshot)
{
    return snapshot.sibling(snapshot.fileName() + ".json");
}

// The epm.cpp of `commit` alone, into `directory`: a shallow fetch of the
// commit's trees without file contents, then a checkout of that one file,
// whose contents git fetches on demand. A server that ignores the filter
// sends the commit's files, which costs only time. Leaves `directory`
// without an epm.cpp when the commit has none.
std::optional<std::string> checkoutManifest(const FilePath& directory,
                                            const std::string& url,
                                            const std::string& commit)
{
    if (auto error = runGit(directory, {"init", "-q"}))
        return error;

    if (auto error = runGit(directory, {"remote", "add", "origin", url}))
        return error;

    if (auto error = runGit(
            directory,
            {"fetch", "-q", "--depth", "1", "--filter=blob:none", "origin", commit}))
        return error;

    const auto listed = Git::run(
        directory, {"ls-tree", "--name-only", "FETCH_HEAD", "--", manifestFileName});

    if (!listed.ok)
        return "git ls-tree failed: " + firstLine(listed.message());

    if (listed.output.find(manifestFileName) == std::string::npos)
        return std::nullopt;

    return runGit(directory,
                  {"checkout", "-q", "FETCH_HEAD", "--", manifestFileName});
}
} // namespace

std::string sourceId(std::string_view url, std::string_view commit)
{
    return sha256Hex("git\n" + repositoryKey(url) + "\n" + std::string {commit});
}

bool isFullCommitHash(std::string_view ref)
{
    if (ref.size() != 40)
        return false;

    for (const auto c: ref)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')
              || (c >= 'A' && c <= 'F')))
            return false;

    return true;
}

std::optional<std::string> resolveRef(const Vector<Git::RemoteRef>& refs,
                                      std::string_view ref)
{
    if (isFullCommitHash(ref))
        return std::string {ref};

    const auto name = std::string {ref};
    const auto candidates = Vector<std::string> {"refs/tags/" + name + "^{}",
                                                 "refs/tags/" + name,
                                                 "refs/heads/" + name,
                                                 name};

    for (const auto& candidate: candidates)
        for (const auto& remote: refs)
            if (remote.ref == candidate)
                return remote.sha;

    return std::nullopt;
}

FilePath snapshotDirectory(const CacheRoot& cache,
                           const std::string& name,
                           const std::string& sourceId)
{
    return cache.src() / name / sourceId.substr(0, 12);
}

SourceResult resolveSource(const CacheRoot& cache,
                           const std::string& name,
                           const std::string& url,
                           const std::string& ref)
{
    const auto resolved = resolveCommit(url, ref);

    if (!resolved.commit)
        return failure("cannot resolve " + ref + " of " + url + ": "
                       + resolved.error);

    auto result = SourceResult {};
    result.snapshot.commit = *resolved.commit;
    result.snapshot.sourceId = sourceId(url, result.snapshot.commit);
    result.ok = true;

    Log::step("epm: " + name + " " + ref + " -> " + result.snapshot.commit);

    if (const auto directory =
            snapshotDirectory(cache, name, result.snapshot.sourceId);
        Files::isFile(markerOf(directory)))
        result.snapshot.directory = directory;

    return result;
}

SourceResult fetchSource(const CacheRoot& cache,
                         const std::string& name,
                         const std::string& url,
                         const std::string& ref,
                         bool lockHeld)
{
    const auto resolved = resolveSource(cache, name, url, ref);

    if (!resolved.ok)
        return resolved;

    return fetchCommit(cache, name, url, ref, resolved.snapshot.commit, lockHeld);
}

SourceResult fetchCommit(const CacheRoot& cache,
                         const std::string& name,
                         const std::string& url,
                         const std::string& ref,
                         const std::string& commit,
                         bool lockHeld)
{
    // A full hash resolves without the network.
    auto result = resolveSource(cache, name, url, commit);

    if (!result.ok || !result.snapshot.directory.empty())
        return result;

    result.snapshot.directory =
        snapshotDirectory(cache, name, result.snapshot.sourceId);
    const auto marker = markerOf(result.snapshot.directory);

    auto lock = std::optional<PackageLock> {};

    if (!lockHeld)
    {
        lock.emplace(cache, name, packageLockTimeout);

        if (!lock->isLocked())
            return failure("timed out waiting for another epm process using "
                           + name);
    }

    if (Files::isFile(marker))
        return result;

    Log::step("epm: fetching " + url + " at " + result.snapshot.commit);

    auto staging = TemporaryFile {cache.tmp(), "-src-" + name};

    if (!Files::createDirectories(staging.path()))
        return failure("cannot create " + staging.path().str());

    if (auto error =
            checkoutCommit(staging.path(), url, ref, result.snapshot.commit))
        return failure("fetching " + url + " at " + result.snapshot.commit + ": "
                       + *error);

    if (auto error = updateSubmodules(staging.path()))
        return failure("fetching submodules of " + url + ": " + *error);

    if (!removeGitMetadata(staging.path()))
        return failure("cannot remove .git from " + staging.path().str());

    if (!Files::removeAll(result.snapshot.directory)
        || !Files::moveFile(staging.path(), result.snapshot.directory))
        return failure("cannot move the snapshot into "
                       + result.snapshot.directory.str());

    const auto markerValue =
        SourceMarker {url, result.snapshot.commit, utcTimestamp()};

    if (!saveJsonFile(marker, markerValue))
        return failure("cannot write " + marker.str());

    return result;
}

SourceResult fetchManifest(const CacheRoot& cache,
                           const std::string& name,
                           const std::string& url,
                           const std::string& ref)
{
    auto result = resolveSource(cache, name, url, ref);

    if (!result.ok)
        return result;

    const auto commit = result.snapshot.commit;
    const auto recipes = cache.src() / name / ".recipes";
    result.manifestDirectory = recipes / result.snapshot.sourceId.substr(0, 12);

    // It appears whole, by one rename, so its presence means complete.
    if (Files::isDirectory(result.manifestDirectory))
        return result;

    // Staged beside it, so the rename never crosses filesystems.
    if (!Files::createDirectories(recipes))
        return failure("cannot create " + recipes.str());

    auto staging = TemporaryFile {recipes, ".staging"};
    const auto manifest = staging.path() / "recipe";
    const auto checkout = staging.path() / "checkout";

    if (!Files::createDirectories(manifest) || !Files::createDirectories(checkout))
        return failure("cannot create " + staging.path().str());

    // The epm.cpp alone even from a complete snapshot: a recipe is one file,
    // and compiled from beside the rest of its repository it could include
    // what a recipe fetched alone cannot.
    auto from = checkout / manifestFileName;

    if (!result.snapshot.directory.empty())
        from = result.snapshot.directory / manifestFileName;
    else
    {
        Log::step("epm: fetching the " + std::string {manifestFileName} + " of "
                  + url + " at " + commit);

        if (auto error = checkoutManifest(checkout, url, commit))
        {
            Log::step("epm: fetching the " + std::string {manifestFileName} + " of "
                      + url + " alone failed (" + *error
                      + "), fetching the whole source");

            const auto fetched = fetchCommit(cache, name, url, ref, commit);

            if (!fetched.ok)
                return fetched;

            result.snapshot = fetched.snapshot;
            from = result.snapshot.directory / manifestFileName;
        }
    }

    if (Files::isFile(from) && !Files::copyFile(from, manifest / manifestFileName))
        return failure("cannot stage the " + std::string {manifestFileName} + " of "
                       + name + " in " + manifest.str());

    // A rename never replaces a directory that is not empty: another process
    // may have put the same checkout there meanwhile, and readers may be
    // using it.
    if (!renameNoReplace(manifest, result.manifestDirectory)
        && !Files::isDirectory(result.manifestDirectory))
        return failure("cannot move the " + std::string {manifestFileName} + " of "
                       + name + " into " + result.manifestDirectory.str());

    return result;
}
} // namespace epm
