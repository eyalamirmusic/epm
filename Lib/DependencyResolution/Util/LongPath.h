#pragma once

#include <eacp/Core/Utils/FilePath.h>

#include <filesystem>

namespace DependencyResolution
{
// The path std::filesystem and Win32 can open at any length. On Windows it is
// absolute with a "\\?\" prefix and backslashes, which lifts MAX_PATH without
// the LongPathsEnabled registry key. eacp passes such a path through intact
// since wide() writes native separators, but nothing in eacp adds the prefix.
std::filesystem::path getLongPathSafeStdPath(const eacp::FilePath& path);

void removeTreeAtAnyLength(const eacp::FilePath& path);

bool renameAtAnyLength(const eacp::FilePath& from, const eacp::FilePath& to);
} // namespace DependencyResolution
