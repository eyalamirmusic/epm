#include <epm/Util/LongPath.h>

#include <eacp/Core/Utils/StdPath.h>

namespace epm
{
namespace
{
#if defined(_WIN32)
constexpr auto extendedLengthPrefix = std::wstring_view {L"\\\\?\\"};
constexpr auto extendedLengthUncPrefix = std::wstring_view {L"\\\\?\\UNC\\"};
constexpr auto uncPrefix = std::wstring_view {L"\\\\"};

std::filesystem::path addExtendedLengthPrefix(const std::filesystem::path& path)
{
    auto absolute = std::filesystem::absolute(path).lexically_normal();
    absolute.make_preferred();
    auto text = absolute.wstring();

    if (text.starts_with(extendedLengthPrefix))
        return absolute;

    if (text.starts_with(uncPrefix))
        return std::wstring {extendedLengthUncPrefix}
               + text.substr(uncPrefix.size());

    return std::wstring {extendedLengthPrefix} + text;
}
#endif
} // namespace

std::filesystem::path getLongPathSafeStdPath(const eacp::FilePath& path)
{
    auto stdPath = eacp::toStdPath(path);

#if defined(_WIN32)
    return addExtendedLengthPrefix(stdPath);
#else
    return stdPath;
#endif
}

void removeTreeAtAnyLength(const eacp::FilePath& path)
{
    auto error = std::error_code {};
    std::filesystem::remove_all(getLongPathSafeStdPath(path), error);
}

bool renameAtAnyLength(const eacp::FilePath& from, const eacp::FilePath& to)
{
    auto error = std::error_code {};
    std::filesystem::rename(
        getLongPathSafeStdPath(from), getLongPathSafeStdPath(to), error);
    return !error;
}
} // namespace epm
