#include "CacheListing.h"
#include "BinaryCache.h"
#include "SourceCache.h"

#include <epm/Spec/JsonFile.h>
#include <epm/Util/Text.h>

#include <eacp/Core/Utils/File.h>
#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/Format.h>

#include <algorithm>

namespace epm
{
using namespace eacp;

// tool/<ref>.json, written by get_epm.cmake. Its "ref" key is left out: Miro's
// macro takes `ref` for its own parameter, and the file name says it anyway.
struct ToolMarker
{
    int schema = 1;
    std::string origin;
    std::string url;
    std::string version;
    std::string builtAt;

    MIRO_REFLECT(schema, origin, url, version, builtAt)
};

namespace
{
using Row = Vector<std::string>;

Vector<FilePath> entriesOfKind(const FilePath& directory,
                               Files::EntryKind kind,
                               const std::string& pattern = {})
{
    auto options = Files::DirectoryOptions {};
    options.pattern = pattern;
    auto paths = Vector<FilePath> {};

    for (const auto& entry: Files::listDirectory(directory, options))
        if (entry.kind == kind)
            paths.add(entry.path);

    return paths;
}

std::string table(const Vector<Row>& rows)
{
    auto widths = Vector<std::size_t> {};

    for (const auto& row: rows)
        for (auto column = 0; column < row.size(); ++column)
        {
            if (widths.size() <= column)
                widths.add(0);

            widths[column] = std::max(widths[column], row[column].size());
        }

    auto text = std::string {};

    for (const auto& row: rows)
    {
        auto line = std::string {" "};

        for (auto column = 0; column < row.size(); ++column)
        {
            line += " " + row[column];

            if (column + 1 < row.size())
                line += std::string(widths[column] - row[column].size() + 1, ' ');
        }

        text += line + "\n";
    }

    return text;
}

std::string section(const std::string& title, const Vector<Row>& rows)
{
    return title + "\n" + (rows.empty() ? std::string {"  (none)\n"} : table(rows));
}

Vector<Row> binaryRows(const CacheRoot& cache)
{
    auto rows = Vector<Row> {};

    for (const auto& entry: allEntries(cache))
    {
        const auto name =
            entry.prefix.parentDirectory().parentDirectory().fileName();
        const auto& fingerprint = entry.fingerprint;

        rows.add({name,
                  fingerprint.platform,
                  entry.configs.empty() ? std::string {"(none)"}
                                        : joinList(entry.configs, "+"),
                  fingerprint.compilerId + " " + fingerprint.compilerMajor,
                  humanSize(directorySize(entry.prefix)),
                  entry.prefix.fileName()});
    }

    return rows;
}

// bin/<name>/<key12> with its <key12>.json beside it: epm 0.1.2 and older.
Vector<FilePath> oldLayoutEntries(const CacheRoot& cache)
{
    auto entries = Vector<FilePath> {};

    for (const auto& nameDirectory:
         entriesOfKind(cache.bin(), Files::EntryKind::directory))
        for (const auto& marker:
             entriesOfKind(nameDirectory, Files::EntryKind::file, "*.json"))
        {
            const auto directory = nameDirectory / marker.stem();

            if (Files::isFile(directory / "epm-targets.cmake"))
                entries.add(directory);
        }

    return entries;
}

Vector<Row> sourceRows(const CacheRoot& cache)
{
    auto rows = Vector<Row> {};

    for (const auto& nameDirectory:
         entriesOfKind(cache.src(), Files::EntryKind::directory))
        for (const auto& file:
             entriesOfKind(nameDirectory, Files::EntryKind::file, "*.json"))
            if (const auto marker = loadJsonFile<SourceMarker>(file))
                rows.add({nameDirectory.fileName(),
                          file.stem(),
                          marker->commit.substr(0, 12),
                          humanSize(directorySize(nameDirectory / file.stem())),
                          marker->url});

    return rows;
}

Vector<Row> toolRows(const CacheRoot& cache)
{
    auto rows = Vector<Row> {};
    const auto tools = cache.path() / "tool";

    for (const auto& file: entriesOfKind(tools, Files::EntryKind::file, "*.json"))
    {
        if (file.fileName() == "latest.json")
            continue;

        if (const auto marker = loadJsonFile<ToolMarker>(file))
            rows.add({file.stem(),
                      marker->version,
                      marker->origin,
                      humanSize(directorySize(tools / file.stem()))});
    }

    return rows;
}
} // namespace

std::string humanSize(std::uint64_t bytes)
{
    constexpr const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    auto value = static_cast<double>(bytes);
    auto unit = 0;

    while (value >= 1024.0 && unit < 4)
    {
        value /= 1024.0;
        ++unit;
    }

    if (unit == 0)
        return std::to_string(bytes) + " B";

    return Strings::format("{:.1f} {}", value, units[unit]);
}

std::uint64_t directorySize(const FilePath& directory)
{
    auto options = Files::DirectoryOptions {};
    options.recursive = true;
    options.includeHidden = true;
    auto total = std::uint64_t {0};

    for (const auto& entry: Files::listDirectory(directory, options))
        if (entry.kind == Files::EntryKind::file)
            total += entry.file().size();

    return total;
}

std::string cacheListing(const CacheRoot& cache)
{
    auto text = "cache " + cache.path().str() + "\n\n"
                + section("binaries", binaryRows(cache)) + "\n"
                + section("sources", sourceRows(cache)) + "\n"
                + section("tools", toolRows(cache));

    const auto old = oldLayoutEntries(cache);

    if (!old.empty())
    {
        auto bytes = std::uint64_t {0};

        for (const auto& directory: old)
            bytes += directorySize(directory);

        text += "\nignored: " + std::to_string(old.size())
                + (old.size() == 1 ? " entry" : " entries")
                + " of the old bin/<name>/<key> layout (epm 0.1.2 and older), "
                + humanSize(bytes) + "; safe to delete, or delete all of "
                + cache.bin().str() + " once\n";
    }

    text.pop_back();
    return text;
}
} // namespace epm
