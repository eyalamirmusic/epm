#include <epm/Source/PackageCache.h>
#include <epm/Source/Download.h>
#include <epm/Source/Mirror.h>
#include <epm/Util/LongPath.h>
#include <epm/Lock/MerkleHash.h>
#include <epm/Util/Paths.h>
#include <epm/Lock/Messages.h>
#include <epm/Util/Sha256.h>
#include <epm/Util/Status.h>

#include <eacp/Core/Process/Process.h>
#include <eacp/Core/Utils/Console.h>
#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/TemporaryFile.h>
#include <epm/Cache/PackageLock.h>
#include <eacp/Core/Utils/Strings.h>
#include <eacp/Core/Utils/Zip.h>
#include <eacp/Core/Platform/Platform.h>

#include <ea_data_structures/Utilities/VectorUtilities.h>

#include <stdexcept>

namespace epm
{
namespace
{
constexpr auto urlKeyLength = 16;

std::string computeUrlKey(const std::string& url)
{
    return computeSha256Hex(url).substr(0, urlKeyLength);
}

eacp::FilePath getStampPath(const eacp::FilePath& sourceDirectory)
{
    return getSiblingWithSuffix(sourceDirectory, ".stamp");
}

std::string readStamp(const eacp::FilePath& path)
{
    if (!eacp::Files::isFile(path))
        return {};

    return eacp::Strings::trim(eacp::Files::readFile(path));
}

eacp::Processes::ProcessResult runPatch(const std::string& executable,
                                        const eacp::FilePath& tree,
                                        const eacp::FilePath& patch)
{
    auto options = eacp::Processes::ProcessOptions {};
    options.executable = executable;
    options.arguments = {"-p1", "-i", patch.str()};
    options.workingDirectory = tree.str();
    options.mergeErrorOutput = true;
    return eacp::Processes::run(options);
}

// CPM's PATCHES run `patch -p1 < file`, which allows fuzzy context and CRLF
// differences that `git apply` refuses, so epm runs the same program.
void applyPatch(const eacp::FilePath& tree, const eacp::FilePath& patch)
{
    auto result = runPatch("patch", tree, patch);

    if (!result.launched && eacp::Platform::isWindows())
        result = runPatch(findGitBundledPatch().str(), tree, patch);

    if (!result.launched)
        throw std::runtime_error("applying " + patch.str()
                                 + " needs the patch program, as CPM's PATCHES "
                                   "does; install patch or put it on PATH");

    if (result.exitCode != 0)
        throw std::runtime_error("patch -p1 -i " + patch.str() + " failed in "
                                 + tree.str() + ":\n" + result.output);
}

bool checkOutCommitWithLfs(const GitTreeListing& listing,
                           const eacp::FilePath& destination)
{
    const auto& mirror = listing.mirror;
    const auto& commit = listing.commit;

    if (!usesGitLfs(listing))
    {
        checkOutCommit(mirror, commit, destination, GitLfsFiles::pointers);
        return false;
    }

    fetchGitLfsObjects(mirror, commit);
    checkOutCommit(mirror, commit, destination, GitLfsFiles::content);
    return true;
}

eacp::FilePath findArchiveContentRoot(const eacp::FilePath& extracted)
{
    auto options = eacp::Files::DirectoryOptions {};
    options.includeHidden = true;
    auto entries = eacp::Files::listDirectory(extracted, options);

    if (entries.size() == 1 && entries[0].kind == eacp::Files::EntryKind::directory)
        return entries[0].path;

    return extracted;
}
constexpr auto mirrorLockTimeout = eacp::Time::MS {10 * 60 * 1000};

// Holds one mirror for this process: configures sharing the cache would
// otherwise fetch into the same bare repository at once, and git's shallow
// file cannot take that.
class MirrorLock
{
public:
    MirrorLock(const CacheRoot& cache, const std::string& url)
        : lock(cache, "mirror-" + computeUrlKey(url), mirrorLockTimeout)
    {
        if (!lock.isLocked())
        {
            throw std::runtime_error("another epm has been fetching " + url
                                     + " for ten minutes; try again once it ends");
        }
    }

private:
    PackageLock lock;
};
} // namespace

PackageCache::PackageCache(CacheRoot rootToUse, eacp::FilePath cmakeToUse)
    : root(std::move(rootToUse))
    , cmake(std::move(cmakeToUse))
{
}

eacp::FilePath PackageCache::getSourceDirectory(const std::string& hash) const
{
    return root.src() / hash;
}

eacp::FilePath PackageCache::getMirrorDirectory(const std::string& url) const
{
    return root.git() / (computeUrlKey(url) + ".git");
}

eacp::FilePath PackageCache::getOrCreateMirror(const std::string& url) const
{
    auto mirror = getMirrorDirectory(url);
    eacp::Files::createDirectories(mirror.parentDirectory());
    createBareMirror(mirror, url);
    return mirror;
}

eacp::FilePath PackageCache::getDownloadPath(const std::string& url) const
{
    auto name = eacp::Files::legalFileName(eacp::FilePath {url}.fileName());
    return root.downloads() / computeUrlKey(url) / name;
}

eacp::TemporaryFile PackageCache::createStagingDirectory() const
{
    // A random name, so configures sharing the cache never stage into one tree.
    auto staging = eacp::TemporaryFile {root.tmp(), ""};
    eacp::Files::createDirectories(staging.path());
    return staging;
}

bool PackageCache::hasIntactEntry(const std::string& hash,
                                  Verification verification) const
{
    auto directory = getSourceDirectory(hash);

    if (!eacp::Files::isDirectory(directory))
        return false;

    auto stampPath = getStampPath(directory);
    auto stamp = computeTreeStamp(directory);

    if (verification == Verification::trustStamp && readStamp(stampPath) == stamp)
        return true;

    if (computeMerkleHash(directory) != hash)
        return false;

    eacp::Files::writeFile(stampPath, std::string_view {stamp});
    return true;
}

void PackageCache::removeEntry(const std::string& hash) const
{
    auto directory = getSourceDirectory(hash);
    removeTreeAtAnyLength(directory);
    eacp::Files::removeFile(getStampPath(directory));
}

MaterializedPackage PackageCache::materialize(const PackageRequest& request,
                                              const std::string& commit,
                                              Network network) const
{
    auto stagingReservation = createStagingDirectory();
    auto staging = stagingReservation.path();

    try
    {
        auto tree = staging / "tree";
        eacp::Files::createDirectories(tree);
        auto resolvedCommit = std::string {};
        auto submodules = std::vector<LockedSubmodule> {};
        auto usesLfs = false;

        if (request.kind == PackageKind::archive)
            fetchArchiveTree(request, network, tree);
        else
            resolvedCommit =
                fetchGitTree(request, commit, network, tree, submodules, usesLfs);

        auto contentRoot = tree;

        if (request.kind == PackageKind::archive)
            contentRoot = findArchiveContentRoot(tree);

        for (const auto& patch: request.patches)
            applyPatch(contentRoot, patch);

        auto stored = storeTree(contentRoot, resolvedCommit, std::move(submodules));
        stored.usesGitLfs = usesLfs;
        removeTreeAtAnyLength(staging);
        return stored;
    }
    catch (...)
    {
        removeTreeAtAnyLength(staging);
        throw;
    }
}

eacp::FilePath PackageCache::fetchCommitIntoMirror(const std::string& name,
                                                   const std::string& url,
                                                   const std::string& commit,
                                                   Network network) const
{
    auto mirrorLock = MirrorLock {root, url};
    auto mirror = getOrCreateMirror(url);

    if (hasCommit(mirror, commit))
        return mirror;

    if (network == Network::forbidden)
    {
        throw std::runtime_error(name + " commit " + commit + " from " + url
                                 + " is not in the cache and offline mode forbids "
                                   "fetching it");
    }

    try
    {
        fetchRefIntoMirror(mirror, commit);
    }
    catch (const std::runtime_error&)
    {
        throw std::runtime_error(describeFailedDownload({name, url, commit}));
    }

    return mirror;
}

std::string PackageCache::fetchGitTree(const PackageRequest& request,
                                       const std::string& commit,
                                       Network network,
                                       const eacp::FilePath& staging,
                                       std::vector<LockedSubmodule>& submodules,
                                       bool& usesLfs) const
{
    auto resolvedCommit = commit;
    auto mirror = eacp::FilePath {};

    if (resolvedCommit.empty())
    {
        if (network == Network::forbidden)
        {
            throw std::runtime_error(
                "resolving " + request.name + "'s ref '" + request.ref
                + "' needs the network, which offline mode forbids");
        }

        auto mirrorLock = MirrorLock {root, request.url};
        mirror = getOrCreateMirror(request.url);
        resolvedCommit = fetchRefIntoMirror(mirror, request.ref);
    }
    else
    {
        mirror = fetchCommitIntoMirror(
            request.name, request.url, resolvedCommit, network);
    }

    auto listing = listTree(mirror, resolvedCommit);
    usesLfs = checkOutCommitWithLfs(listing, staging);
    checkOutSubmodules(request.name,
                       request.url,
                       listing,
                       staging,
                       {},
                       request.submoduleSelection,
                       network,
                       submodules,
                       usesLfs);
    return resolvedCommit;
}

void PackageCache::checkOutSubmodules(const std::string& name,
                                      const std::string& parentUrl,
                                      const GitTreeListing& parentListing,
                                      const eacp::FilePath& tree,
                                      const std::string& pathPrefix,
                                      const SubmoduleSelection& selection,
                                      Network network,
                                      std::vector<LockedSubmodule>& submodules,
                                      bool& usesLfs) const
{
    for (const auto& submodule: listSubmodules(parentListing, tree))
    {
        auto fullPath = pathPrefix + submodule.path;
        auto isSelected = !selection.isRestricted
                          || EA::Vectors::contains(selection.paths, fullPath);

        if (!isSelected)
            continue;

        auto url = resolveSubmoduleUrl(parentUrl, submodule.url);
        auto mirror = fetchCommitIntoMirror(name, url, submodule.commit, network);
        auto destination = tree / submodule.path;
        eacp::Files::createDirectories(destination);
        auto submoduleListing = listTree(mirror, submodule.commit);
        usesLfs = checkOutCommitWithLfs(submoduleListing, destination) || usesLfs;
        submodules.push_back({fullPath, url, submodule.commit});

        if (selection.isRecursive)
        {
            checkOutSubmodules(name,
                               url,
                               submoduleListing,
                               destination,
                               fullPath + "/",
                               {},
                               network,
                               submodules,
                               usesLfs);
        }
    }
}

void PackageCache::fetchArchiveTree(const PackageRequest& request,
                                    Network network,
                                    const eacp::FilePath& staging) const
{
    auto archive = getDownloadPath(request.url);

    if (!eacp::Files::isFile(archive))
    {
        if (network == Network::forbidden)
        {
            throw std::runtime_error(request.name + "'s archive " + request.url
                                     + " is not in the cache and offline mode "
                                       "forbids downloading it");
        }

        auto expected = ExpectedHash {
            request.archiveHash.algorithm,
            request.archiveHash.hex,
            [this, &request](const eacp::FilePath& file)
            { return computeArchiveHash(file, request.archiveHash.algorithm); }};

        try
        {
            downloadFile(request.url, archive, expected);
        }
        catch (const HashMismatchError&)
        {
            throw;
        }
        catch (const std::runtime_error& error)
        {
            printStatus(error.what());
            throw std::runtime_error(
                describeFailedDownload({request.name, request.url, {}}));
        }
    }

    extractArchive(archive, staging);
}

// file(<algorithm>) knows every algorithm URL_HASH may name, SHA-3 included.
std::string PackageCache::computeArchiveHash(const eacp::FilePath& archive,
                                             const std::string& algorithm) const
{
    auto script = root.tmp() / "hash-file.cmake";
    eacp::Files::writeFile(
        script,
        std::string_view {
            "file(${ALGORITHM} \"${FILE}\" digest)\nmessage(\"${digest}\")\n"});

    auto options = eacp::Processes::ProcessOptions {};
    options.executable = cmake.str();
    options.arguments = {
        "-DALGORITHM=" + algorithm, "-DFILE=" + archive.str(), "-P", script.str()};
    auto result = eacp::Processes::run(options);

    if (result.exitCode != 0)
        throw std::runtime_error("cmake could not hash " + archive.str() + ":\n"
                                 + result.errorOutput);

    return eacp::Strings::toLower(eacp::Strings::trim(result.errorOutput));
}

void PackageCache::extractArchive(const eacp::FilePath& archive,
                                  const eacp::FilePath& destination) const
{
    if (archive.hasExtension("zip"))
    {
        auto reader = eacp::Zip::Reader {archive};

        if (!reader.isValid() || !reader.extractAll(destination))
            throw std::runtime_error("cannot extract " + archive.str() + ": "
                                     + reader.errorMessage());
        return;
    }

    auto options = eacp::Processes::ProcessOptions {};
    options.executable = cmake.str();
    options.arguments = {"-E", "tar", "xf", archive.str()};
    options.workingDirectory = destination.str();
    auto result = eacp::Processes::run(options);

    if (result.exitCode != 0)
        throw std::runtime_error("cmake -E tar could not extract " + archive.str()
                                 + ":\n" + result.errorOutput);
}

MaterializedPackage
    PackageCache::storeTree(const eacp::FilePath& tree,
                            const std::string& commit,
                            std::vector<LockedSubmodule> submodules) const
{
    auto hash = computeMerkleHash(tree);
    auto destination = getSourceDirectory(hash);

    if (!eacp::Files::isDirectory(destination))
    {
        eacp::Files::createDirectories(destination.parentDirectory());

        if (!renameAtAnyLength(tree, destination)
            && !eacp::Files::isDirectory(destination))
            throw std::runtime_error("cannot move the unpacked tree into "
                                     + destination.str());
    }

    eacp::Files::writeFile(getStampPath(destination),
                           std::string_view {computeTreeStamp(destination)});
    return {commit, hash, destination, std::move(submodules)};
}
} // namespace epm
