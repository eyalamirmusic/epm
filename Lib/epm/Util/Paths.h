#pragma once

#include <eacp/Core/Utils/FilePath.h>

namespace epm
{
// `path` made absolute against the working directory, with the symlinks of
// the part that exists resolved; `path` unchanged when that fails.
// (Upstream candidate: an eacp Files::canonical, std::filesystem's
// weakly_canonical without the throwing overload.)
eacp::FilePath resolvedPath(const eacp::FilePath& path);
} // namespace epm
