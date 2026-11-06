#include "Paths.h"

#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/StdPath.h>

#include <chrono>
#include <filesystem>
#include <thread>

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

bool isTransientRenameError(const std::error_code& error)
{
#if defined(_WIN32)
    // ERROR_ACCESS_DENIED, ERROR_SHARING_VIOLATION, ERROR_LOCK_VIOLATION.
    constexpr int transient[] = {5, 32, 33};

    if (error.category() == std::system_category())
        for (const auto code: transient)
            if (error.value() == code)
                return true;

    return error == std::errc::permission_denied;
#else
    (void) error;
    return false;
#endif
}

bool renameNoReplace(const FilePath& from, const FilePath& to)
{
    if (!Files::createDirectories(to.parentDirectory()))
        return false;

    constexpr auto attempts = 10;

    for (auto attempt = 1;; ++attempt)
    {
        auto failed = std::error_code {};
        std::filesystem::rename(toStdPath(from), toStdPath(to), failed);

        if (!failed)
            return true;

        if (attempt == attempts || !isTransientRenameError(failed)
            || Files::exists(to))
            return false;

        std::this_thread::sleep_for(std::chrono::milliseconds {20 * attempt});
    }
}
} // namespace epm
