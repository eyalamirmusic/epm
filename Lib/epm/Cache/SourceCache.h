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

// A cached tree: src/<hash>, where hash is its Merkle hash.
struct SourceSnapshot
{
    std::string commit;
    std::string hash;
    eacp::FilePath directory;
};

// src/<sourceId12>.json: which tree a commit (with its patches and
// submodule selection) unpacked to, so an unlocked configure finds it again
// without checking it out.
struct SourceMarker
{
    std::string url;
    std::string commit;
    std::string hash;
    std::string fetchedAt;

    MIRO_REFLECT(url, commit, hash, fetchedAt)
};

} // namespace epm
