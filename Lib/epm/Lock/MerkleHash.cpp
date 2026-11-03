#include <epm/Lock/MerkleHash.h>
#include <epm/Util/LongPath.h>
#include <epm/Util/Sha256.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <eacp/Core/Utils/Containers.h>

namespace epm
{
namespace
{
constexpr auto excludedDirectoryName = std::string_view {".git"};

enum class TreeEntryKind
{
    file,
    directory,
    symlink
};

struct TreeEntry
{
    std::filesystem::path path;
    std::string name;
    TreeEntryKind kind;
};

TreeEntryKind getTreeEntryKind(const std::filesystem::directory_entry& entry)
{
    auto status = entry.symlink_status();

    if (std::filesystem::is_symlink(status))
        return TreeEntryKind::symlink;

    if (std::filesystem::is_directory(status))
        return TreeEntryKind::directory;

    if (std::filesystem::is_regular_file(status))
        return TreeEntryKind::file;

    throw std::runtime_error(eacp::FilePath {entry.path()}.str()
                             + " is neither a file, a directory "
                               "nor a symlink, so it cannot be hashed");
}

// Sorted by name and without .git, so the hash is the same on every platform.
EA::Vector<TreeEntry> listHashedEntries(const std::filesystem::path& directory)
{
    auto entries = EA::Vector<TreeEntry> {};
    auto error = std::error_code {};

    for (auto iterator = std::filesystem::directory_iterator {directory, error};
         !error && iterator != std::filesystem::directory_iterator {};
         iterator.increment(error))
    {
        auto name = eacp::FilePath {iterator->path()}.fileName();

        if (name != excludedDirectoryName)
            entries.push_back({iterator->path(), name, getTreeEntryKind(*iterator)});
    }

    if (error)
        throw std::runtime_error("cannot read " + eacp::FilePath {directory}.str()
                                 + ": " + error.message());

    std::sort(entries.begin(),
              entries.end(),
              [](const auto& left, const auto& right)
              { return left.name < right.name; });

    return entries;
}

std::string describeEntryKind(TreeEntryKind kind)
{
    switch (kind)
    {
        case TreeEntryKind::file:
            return "file";
        case TreeEntryKind::directory:
            return "directory";
        case TreeEntryKind::symlink:
            return "symlink";
    }

    throw std::runtime_error("cannot hash a special file");
}

std::string computeSymlinkHash(const std::filesystem::path& link)
{
    auto target = eacp::FilePath(std::filesystem::read_symlink(link));
    return computeSha256Hex(target.str());
}

std::string computeMerkleHashOfStdPath(const std::filesystem::path& directory);

std::string computeEntryHash(const TreeEntry& entry)
{
    switch (entry.kind)
    {
        case TreeEntryKind::file:
            return computeFileSha256Hex(entry.path);
        case TreeEntryKind::directory:
            return computeMerkleHashOfStdPath(entry.path);
        case TreeEntryKind::symlink:
            return computeSymlinkHash(entry.path);
    }

    throw std::runtime_error("cannot hash a special file");
}

std::string computeMerkleHashOfStdPath(const std::filesystem::path& directory)
{
    auto hasher = Sha256 {};

    for (const auto& entry: listHashedEntries(directory))
    {
        auto line = describeEntryKind(entry.kind) + ' ' + computeEntryHash(entry)
                    + ' ' + entry.name + '\n';
        hasher.update(line);
    }

    return convertDigestToHex(hasher.finish());
}

std::string describeFileForStamp(const std::filesystem::path& file)
{
    auto error = std::error_code {};
    auto size = std::filesystem::file_size(file, error);
    auto written = std::filesystem::last_write_time(file, error);
    auto writtenTicks =
        static_cast<std::int64_t>(written.time_since_epoch().count());
    return ' ' + std::to_string(size) + ' ' + std::to_string(writtenTicks);
}

void addTreeToStamp(Sha256& hasher,
                    const std::filesystem::path& directory,
                    const std::string& relativeDirectory)
{
    for (const auto& entry: listHashedEntries(directory))
    {
        auto relative = relativeDirectory + '/' + entry.name;
        auto line = relative;

        if (entry.kind == TreeEntryKind::file)
            line += describeFileForStamp(entry.path);

        hasher.update(line + '\n');

        if (entry.kind == TreeEntryKind::directory)
            addTreeToStamp(hasher, entry.path, relative);
    }
}
} // namespace

std::string computeMerkleHash(const eacp::FilePath& directory)
{
    return computeMerkleHashOfStdPath(getLongPathSafeStdPath(directory));
}

std::string computeTreeStamp(const eacp::FilePath& directory)
{
    auto hasher = Sha256 {};
    addTreeToStamp(hasher, getLongPathSafeStdPath(directory), {});
    return convertDigestToHex(hasher.finish());
}
} // namespace epm
