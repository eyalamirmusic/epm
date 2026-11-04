#pragma once

#include <DependencyResolution/Lock/LockFile.h>

#include <eacp/Core/Utils/Containers.h>
#include <eacp/Core/Utils/FilePath.h>

#include <map>
#include <string>

namespace DependencyResolution
{
enum class LockMode
{
    // Read the lock; write it when it is missing, add a package it lacks.
    locked,

    // Re-resolve every ref and rewrite the lock.
    update,

    // The lock and the cache only, never the network.
    offline
};

// One entry of the plan epm's graph walk makes: one project asks for one
// package from a URL at a ref. A package several projects ask for appears
// once per asker.
struct PlannedSource
{
    std::string name;

    // Who asked: the lock records each requester's requirement under it.
    std::string requestedBy;

    std::string url;

    // A tag, a branch or a commit.
    std::string ref;

    // Provided by the machine, never fetched or locked.
    bool isSystem = false;
};

// The root's replacement for every request of one package, as Go's
// `replace`: empty fields keep what the requests say.
struct RootOverride
{
    std::string url;
    std::string commit;
    std::string tag;
};

struct LockOptions
{
    eacp::FilePath lockFile;
    eacp::FilePath cacheRoot;

    // The cmake that extracts archives.
    eacp::FilePath cmake;

    LockMode mode = LockMode::locked;

    // Refuses to write a missing lock, or add a package to one, in locked mode.
    bool isContinuousIntegration = false;

    std::map<std::string, RootOverride> overrides;

    // Paths inside a package, relative to it, whose SHA-256 the lock records
    // so verifyPinnedFile can check them before they run.
    EA::Vector<std::string> pinnedFiles;

    // What a user runs to re-lock, named in error messages.
    std::string updateCommand = "resolve again in update mode";
};

struct ResolvedSource
{
    std::string name;
    std::string commit;
    std::string hash;
    eacp::FilePath sourceDirectory;
};

// Resolves packages through the lock, one package at a time as a graph walk
// meets them, and writes the lock as it goes. Failures throw
// std::runtime_error with the message a user reads.
class SourceResolver
{
public:
    explicit SourceResolver(LockOptions optionsToUse);

    // One package from every request for it: the root override when there is
    // one, else minimal version selection over the requests (the highest
    // semantic version tag; one commit; anything else is a conflict only a
    // root override settles). In locked and offline mode the lock must record
    // the selection, and the source is its cached tree, fetched at the locked
    // commit and checked against the locked Merkle hash when missing.
    ResolvedSource resolve(const std::string& name,
                           const EA::Vector<PlannedSource>& requests);

    // Throws unless `relativePath` inside the package resolved as `name`
    // hashes to what the lock records for it (an absent file to nothing).
    void verifyPinnedFile(const std::string& name,
                          const eacp::FilePath& sourceDirectory,
                          const std::string& relativePath) const;

    // The root hash over every package in the lock.
    std::string getRootHash() const;

private:
    LockOptions options;
    bool isWritingNewLock = false;
};

struct ResolvedSources
{
    EA::Vector<ResolvedSource> packages;
    EA::Vector<std::string> systemPackages;
    std::string rootHash;
};

// The plan made concrete: every package it names, in first-mention order,
// at an exact commit and Merkle hash recorded in the lock, with its source
// tree on disk. System packages are only listed.
ResolvedSources resolveSources(const EA::Vector<PlannedSource>& plan,
                               const LockOptions& options);
} // namespace DependencyResolution
