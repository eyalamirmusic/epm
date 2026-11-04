#pragma once

#include <DependencyResolution/Cache/CacheRoot.h>

#include <Miro/Reflect.h>

#include <optional>
#include <string>
#include <string_view>

namespace DependencyResolution
{
bool isFullCommitHash(std::string_view ref);

// A cached tree: src/<hash>, where hash is its Merkle hash.
struct SourceSnapshot
{
    std::string commit;
    std::string hash;
    eacp::FilePath directory;
};

} // namespace DependencyResolution
