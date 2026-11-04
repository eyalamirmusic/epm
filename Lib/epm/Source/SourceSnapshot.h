#pragma once

#include <epm/Cache/CacheRoot.h>

#include <Miro/Reflect.h>

#include <optional>
#include <string>
#include <string_view>

namespace epm
{
bool isFullCommitHash(std::string_view ref);

// A cached tree: src/<hash>, where hash is its Merkle hash.
struct SourceSnapshot
{
    std::string commit;
    std::string hash;
    eacp::FilePath directory;
};

} // namespace epm
