#include "SourceCache.h"
#include "PackageLock.h"

#include <epm/Spec/JsonFile.h>
#include <epm/Util/Log.h>
#include <epm/Util/Sha256.h>
#include <epm/Util/Timestamp.h>

#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/TemporaryFile.h>

namespace epm
{
using namespace eacp;

namespace
{
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
// get a full fetch of the ref (or of every branch and tag for a bare hash).
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
        isFullCommitHash(ref)
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

SourceResult sourceFailure(std::string error)
{
    auto result = SourceResult {};
    result.error = std::move(error);
    return result;
}
} // namespace

std::string sourceId(std::string_view url, std::string_view commit)
{
    return sha256Hex("git\n" + std::string {url} + "\n" + std::string {commit});
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

SourceResult fetchSource(const CacheRoot& cache,
                         const std::string& name,
                         const std::string& url,
                         const std::string& ref)
{
    const auto resolved = resolveCommit(url, ref);

    if (!resolved.commit)
        return sourceFailure("cannot resolve " + ref + " of " + url + ": "
                             + resolved.error);

    auto result = SourceResult {};
    result.snapshot.commit = *resolved.commit;
    result.snapshot.sourceId = sourceId(url, result.snapshot.commit);

    const auto shortId = result.snapshot.sourceId.substr(0, 12);
    const auto packageDirectory = cache.src() / name;
    const auto marker = packageDirectory / (shortId + ".json");
    result.snapshot.directory = packageDirectory / shortId;
    result.ok = true;

    Log::step("epm: " + name + " " + ref + " -> " + result.snapshot.commit);

    // The marker is written last, so its presence alone means complete.
    if (Files::isFile(marker))
        return result;

    auto lock = PackageLock {cache, name, packageLockTimeout};

    if (!lock.isLocked())
        return sourceFailure("timed out waiting for another epm process using "
                             + name);

    if (Files::isFile(marker))
        return result;

    Log::step("epm: fetching " + url + " at " + result.snapshot.commit);

    auto staging = TemporaryFile {cache.tmp(), "-src-" + name};

    if (!Files::createDirectories(staging.path()))
        return sourceFailure("cannot create " + staging.path().str());

    if (auto error =
            checkoutCommit(staging.path(), url, ref, result.snapshot.commit))
        return sourceFailure("fetching " + url + " at " + result.snapshot.commit
                             + ": " + *error);

    if (auto error = updateSubmodules(staging.path()))
        return sourceFailure("fetching submodules of " + url + ": " + *error);

    if (!removeGitMetadata(staging.path()))
        return sourceFailure("cannot remove .git from " + staging.path().str());

    if (!Files::removeAll(result.snapshot.directory)
        || !Files::moveFile(staging.path(), result.snapshot.directory))
        return sourceFailure("cannot move the snapshot into "
                             + result.snapshot.directory.str());

    const auto markerValue =
        SourceMarker {url, result.snapshot.commit, utcTimestamp()};

    if (!saveJsonFile(marker, markerValue))
        return sourceFailure("cannot write " + marker.str());

    return result;
}
} // namespace epm
