#pragma once

#include <eacp/Core/Utils/FilePath.h>

#include <string>

namespace epm
{
// The SHA-256 of a source tree built from each entry's path, type and content,
// so two trees hash alike exactly when their files do, whatever produced them.
// `.git` is left out at every level.
std::string computeMerkleHash(const eacp::FilePath& directory);

// A cheap fingerprint of every entry's path, size and modification time, used
// to tell whether a tree has changed since its Merkle hash was computed.
std::string computeTreeStamp(const eacp::FilePath& directory);
} // namespace epm
