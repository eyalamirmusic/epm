#include "Paths.h"

#include <eacp/Core/Utils/StdPath.h>

#include <filesystem>

namespace epm
{
using namespace eacp;

FilePath resolvedPath(const FilePath& path)
{
    // weakly_canonical leaves a relative path relative when none of it exists.
    auto failed = std::error_code {};
    const auto absolute = std::filesystem::absolute(toStdPath(path), failed);

    if (failed)
        return path;

    const auto resolved = std::filesystem::weakly_canonical(absolute, failed);
    return FilePath {failed ? absolute : resolved};
}
} // namespace epm
