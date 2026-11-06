#pragma once

#include "CacheRoot.h"

#include <epm/Util/Git.h>

#include <Miro/Reflect.h>

#include <optional>
#include <string>
#include <string_view>

namespace epm
{
// sha256("git\n" + repositoryKey(url) + "\n" + commit), full hex; directories
// use the first 12. The key, not the URL as written, so every spelling of one
// repository (https, ssh, with or without .git) shares the snapshot and the
// binary entries built from it.
std::string sourceId(std::string_view url, std::string_view commit);

bool isFullCommitHash(std::string_view ref);

// A 40-hex ref as is; otherwise refs/tags/<ref>^{}, refs/tags/<ref>,
// refs/heads/<ref>, then an exact match (HEAD, refs/...), in that order.
std::optional<std::string> resolveRef(const eacp::Vector<Git::RemoteRef>& refs,
                                      std::string_view ref);

// src/<name>/<sourceId12>.json, written once the snapshot is complete.
struct SourceMarker
{
    std::string url;
    std::string commit;
    std::string fetchedAt;

    MIRO_REFLECT(url, commit, fetchedAt)
};

struct SourceSnapshot
{
    std::string commit;
    std::string sourceId;

    // The checkout; empty for a source resolved but not fetched
    // (resolveSource).
    eacp::FilePath directory;
};

struct SourceResult
{
    bool ok = false;
    SourceSnapshot snapshot;

    // fetchManifest only: a directory holding the package's epm.cpp when it
    // has one, the snapshot's own or a checkout of that file alone; empty
    // means the snapshot's directory.
    eacp::FilePath manifestDirectory;
    std::string error;
};

// src/<name>/<sourceId12>: where the snapshot of `sourceId` goes.
eacp::FilePath snapshotDirectory(const CacheRoot& cache,
                                 const std::string& name,
                                 const std::string& sourceId);

// The commit `ref` names and its sourceId, without fetching the source:
// `git ls-remote`, or nothing at all for a full commit hash. The snapshot's
// directory is set only when that snapshot is complete in the cache already.
SourceResult resolveSource(const CacheRoot& cache,
                           const std::string& name,
                           const std::string& url,
                           const std::string& ref);

// Resolves `ref` and fetches a clean snapshot (no .git) into
// src/<name>/<sourceId12>, unless one is already marked complete. The fetch
// takes the package lock, unless `lockHeld` says the caller holds it already
// (eacp's IPC::Lock is not re-entrant).
SourceResult fetchSource(const CacheRoot& cache,
                         const std::string& name,
                         const std::string& url,
                         const std::string& ref,
                         bool lockHeld = false);

// The same for `commit`, resolved from `ref` already (a tag or branch, or
// the commit itself): no `git ls-remote`, and a server that refuses to serve
// the commit by its hash is asked for `ref` instead, then the commit checked
// out of it (every branch and tag only when `ref` is a hash or empty).
SourceResult fetchCommit(const CacheRoot& cache,
                         const std::string& name,
                         const std::string& url,
                         const std::string& ref,
                         const std::string& commit,
                         bool lockHeld = false);

// What the graph walk needs of a package before its build step runs
// (Docs/Graph.md §5): resolveSource, and in manifestDirectory
// src/<name>/.recipes/<sourceId12>/, a directory holding the package's
// epm.cpp alone when it has one (empty without), without fetching the rest of
// the source: copied from the snapshot when that is complete in the cache,
// else a checkout of that one file (a shallow fetch without file contents,
// `--filter=blob:none`, then the file). A server that refuses that fetch gets
// fetchCommit, and the epm.cpp is copied from the snapshot. Either way the
// recipe is compiled from beside nothing else of its repository: a recipe is
// one file, the same whether the snapshot is cached or not. The directory is
// staged inside .recipes/ and appears by one rename.
SourceResult fetchManifest(const CacheRoot& cache,
                           const std::string& name,
                           const std::string& url,
                           const std::string& ref);
} // namespace epm
