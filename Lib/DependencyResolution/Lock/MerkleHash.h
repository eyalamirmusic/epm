#pragma once

#include <eacp/Core/Utils/FilePath.h>

#include <set>
#include <string>

namespace DependencyResolution
{
// The paths, relative to the tree and '/'-separated, that the git tree or the
// archive header says are symlinks. Where symlinks cannot be made they are
// plain files holding the link text, as git's core.symlinks=false leaves them.
using DeclaredSymlinks = std::set<std::string>;

// The SHA-256 of a source tree built from each entry's path, type and content,
// so two trees hash alike exactly when their files do, whatever produced them.
// `.git` is left out at every level. A declared symlink checked out as a plain
// file hashes as the symlink it stands for, so the hash does not depend on
// whether this machine could create it.
std::string computeMerkleHash(const eacp::FilePath& directory,
                              const DeclaredSymlinks& symlinks = {});

// A cheap fingerprint of every entry's path, size and modification time, used
// to tell whether a tree has changed since its Merkle hash was computed.
std::string computeTreeStamp(const eacp::FilePath& directory);
} // namespace DependencyResolution
