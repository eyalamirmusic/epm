#pragma once

#include "CacheRoot.h"

#include <epm/Util/Git.h>

#include <Miro/Reflect.h>

#include <optional>
#include <string>
#include <string_view>

namespace epm
{
// sha256("git\n" + url + "\n" + commit), full hex; directories use the first 12.
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
    eacp::FilePath directory;
};

struct SourceResult
{
    bool ok = false;
    SourceSnapshot snapshot;
    std::string error;
};

// Resolves `ref` and fetches a clean snapshot (no .git) into
// src/<name>/<sourceId12>, unless one is already marked complete.
SourceResult fetchSource(const CacheRoot& cache,
                         const std::string& name,
                         const std::string& url,
                         const std::string& ref);
} // namespace epm
