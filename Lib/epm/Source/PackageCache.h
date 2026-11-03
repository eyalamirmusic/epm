#pragma once

#include <epm/Cache/CacheRoot.h>

#include <eacp/Core/Utils/Containers.h>
#include <eacp/Core/Utils/FilePath.h>
#include <eacp/Core/Utils/TemporaryFile.h>

#include <epm/Source/Mirror.h>
#include <epm/Spec/PackageArguments.h>
#include <epm/Source/Submodules.h>

#include <string>
#include <eacp/Core/Utils/Containers.h>

namespace epm
{
enum class Network
{
    allowed,
    forbidden
};

enum class Verification
{
    trustStamp,
    rehash
};

struct PackageRequest
{
    std::string name;
    PackageKind kind = PackageKind::git;
    std::string url;
    std::string ref;
    EA::Vector<eacp::FilePath> patches;
    SubmoduleSelection submoduleSelection;
    ArchiveHash archiveHash;
};

struct MaterializedPackage
{
    std::string commit;
    std::string hash;
    eacp::FilePath sourceDirectory;
    EA::Vector<LockedSubmodule> submodules;
    bool usesGitLfs = false;
};

// The source half of the cache: bare git mirrors under git/, downloaded
// archives under downloads/, and unpacked trees under src/<merkle hash>.
class PackageCache
{
public:
    PackageCache(CacheRoot rootToUse, eacp::FilePath cmakeToUse);

    eacp::FilePath getSourceDirectory(const std::string& hash) const;

    // True when src/<hash> exists and still hashes to `hash`. With
    // trustStamp, a matching size/mtime stamp stands in for the hash.
    bool hasIntactEntry(const std::string& hash, Verification verification) const;

    void removeEntry(const std::string& hash) const;

    // Fetches `commit` (or, when it is empty, whatever `request.ref` names now),
    // unpacks it, applies the patches and stores the tree under its hash.
    // The commit `request.ref` names now, fetched into the mirror; the
    // archive's hash for an archive.
    std::string resolveCommit(const PackageRequest& request, Network network) const;

    MaterializedPackage materialize(const PackageRequest& request,
                                    const std::string& commit,
                                    Network network) const;

private:
    eacp::FilePath getMirrorDirectory(const std::string& url) const;
    eacp::FilePath getOrCreateMirror(const std::string& url) const;
    eacp::FilePath getDownloadPath(const std::string& url) const;
    eacp::TemporaryFile createStagingDirectory() const;

    std::string fetchGitTree(const PackageRequest& request,
                             const std::string& commit,
                             Network network,
                             const eacp::FilePath& staging,
                             EA::Vector<LockedSubmodule>& submodules,
                             bool& usesLfs) const;
    eacp::FilePath fetchCommitIntoMirror(const std::string& name,
                                         const std::string& url,
                                         const std::string& commit,
                                         Network network) const;
    void checkOutSubmodules(const std::string& name,
                            const std::string& parentUrl,
                            const GitTreeListing& parentListing,
                            const eacp::FilePath& tree,
                            const std::string& pathPrefix,
                            const SubmoduleSelection& selection,
                            Network network,
                            EA::Vector<LockedSubmodule>& submodules,
                            bool& usesLfs) const;
    void fetchArchiveTree(const PackageRequest& request,
                          Network network,
                          const eacp::FilePath& staging) const;
    std::string computeArchiveHash(const eacp::FilePath& archive,
                                   const std::string& algorithm) const;
    void extractArchive(const eacp::FilePath& archive,
                        const eacp::FilePath& destination) const;
    MaterializedPackage storeTree(const eacp::FilePath& tree,
                                  const std::string& commit,
                                  EA::Vector<LockedSubmodule> submodules) const;

    CacheRoot root;
    eacp::FilePath cmake;
};
} // namespace epm
