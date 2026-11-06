#pragma once

#include <eacp/Core/Utils/Containers.h>
#include <eacp/Core/Utils/FilePath.h>

#include <system_error>

namespace epm
{
// `path` made absolute against the working directory, with the symlinks of
// the part that exists resolved; `path` unchanged when that fails.
// (Upstream candidate: an eacp Files::canonical, std::filesystem's
// weakly_canonical without the throwing overload.)
eacp::FilePath resolvedPath(const eacp::FilePath& path);

// The directories directly inside `directory`, hidden ones (a cache entry's
// staging) only with `includeHidden`; none when it cannot be listed.
eacp::Vector<eacp::FilePath> subdirectories(const eacp::FilePath& directory,
                                            bool includeHidden = false);

// Renames `from` to `to` (creating to's parent), never replacing a directory
// that is there already: std::filesystem's rename of a directory fails when
// the target is a directory that is not empty (on Windows, any directory),
// where eacp's moveFile would delete the target and retry. A file is
// replaced. On Windows a rename right after writing can fail for a moment on
// a handle an antivirus or indexer holds (access denied, a sharing
// violation): that is retried a few times over about a second, a target that
// is there is not. Both paths must be on one filesystem.
// (Upstream candidate: an eacp Files::renameNoReplace.)
bool renameNoReplace(const eacp::FilePath& from, const eacp::FilePath& to);

// Whether a rename that failed with `error` may succeed when retried: on
// Windows access denied, a sharing or a lock violation; nothing elsewhere.
bool isTransientRenameError(const std::error_code& error);
} // namespace epm
