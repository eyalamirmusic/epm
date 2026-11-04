#include <DependencyResolution/Lock/MerkleHash.h>
#include <DependencyResolution/Util/LongPath.h>
#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/Sha256.h>
#include <eacp/Core/Utils/Strings.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <eacp/Core/Utils/Containers.h>

namespace DependencyResolution
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
    return eacp::computeSha256Hex(
        eacp::Files::readSymlink(eacp::FilePath {link}).str());
}

std::string computeMerkleHashOfStdPath(const std::filesystem::path& directory,
                                       const std::string& relativeDirectory,
                                       const DeclaredSymlinks& symlinks);

std::string computeFileHash(const std::filesystem::path& file)
{
    auto hash = eacp::computeFileSha256Hex(eacp::FilePath {file});

    if (hash.empty())
        throw std::runtime_error("cannot open " + eacp::FilePath {file}.str()
                                 + " to hash it");

    return hash;
}

std::string joinRelativePath(const std::string& directory, const std::string& name)
{
    if (directory.empty())
        return name;

    return directory + '/' + name;
}

// The file holds the link text, which is what a symlink's hash is taken over.
bool isSymlinkCheckedOutAsFile(const TreeEntry& entry,
                               const std::string& relativePath,
                               const DeclaredSymlinks& symlinks)
{
    return entry.kind == TreeEntryKind::file && symlinks.contains(relativePath);
}

std::string computeEntryHash(const TreeEntry& entry,
                             const std::string& relativePath,
                             const DeclaredSymlinks& symlinks)
{
    switch (entry.kind)
    {
        case TreeEntryKind::file:
            return computeFileHash(entry.path);
        case TreeEntryKind::directory:
            return computeMerkleHashOfStdPath(entry.path, relativePath, symlinks);
        case TreeEntryKind::symlink:
            return computeSymlinkHash(entry.path);
    }

    throw std::runtime_error("cannot hash a special file");
}

std::string computeMerkleHashOfStdPath(const std::filesystem::path& directory,
                                       const std::string& relativeDirectory,
                                       const DeclaredSymlinks& symlinks)
{
    auto hasher = eacp::Sha256 {};

    for (const auto& entry: listHashedEntries(directory))
    {
        auto relativePath = joinRelativePath(relativeDirectory, entry.name);
        auto kind = entry.kind;

        if (isSymlinkCheckedOutAsFile(entry, relativePath, symlinks))
            kind = TreeEntryKind::symlink;

        auto line = describeEntryKind(kind) + ' '
                    + computeEntryHash(entry, relativePath, symlinks) + ' '
                    + entry.name + '\n';
        hasher.update(line);
    }

    auto digest = hasher.finish();
    return eacp::Strings::toHex(digest);
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

void addTreeToStamp(eacp::Sha256& hasher,
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

std::string computeMerkleHash(const eacp::FilePath& directory,
                              const DeclaredSymlinks& symlinks)
{
    return computeMerkleHashOfStdPath(
        getLongPathSafeStdPath(directory), {}, symlinks);
}

std::string computeTreeStamp(const eacp::FilePath& directory)
{
    auto hasher = eacp::Sha256 {};
    addTreeToStamp(hasher, getLongPathSafeStdPath(directory), {});
    auto digest = hasher.finish();
    return eacp::Strings::toHex(digest);
}
} // namespace DependencyResolution
