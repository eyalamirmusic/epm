#include "CacheVerify.h"

#include <epm/Build/Harvest.h>
#include <epm/Spec/JsonFile.h>
#include <epm/Util/Sha256.h>

#include <eacp/Core/Utils/Files.h>

namespace epm
{
using namespace eacp;

namespace
{
Vector<FilePath> subdirectories(const FilePath& directory, bool hidden = false)
{
    auto options = Files::DirectoryOptions {};
    options.includeHidden = hidden;
    auto directories = Vector<FilePath> {};

    for (const auto& entry: Files::listDirectory(directory, options))
        if (entry.kind == Files::EntryKind::directory)
            directories.add(entry.path);

    return directories;
}

std::optional<std::string> fileProblem(const FilePath& file,
                                       const UnitFile& recorded)
{
    if (!Files::isFile(file))
        return "missing";

    const auto bytes = Files::readFile(file);

    if (static_cast<std::int64_t>(bytes.size()) != recorded.size)
        return "size " + std::to_string(bytes.size()) + ", recorded "
               + std::to_string(recorded.size);

    if (sha256Hex(bytes) != recorded.sha256)
        return "sha256 differs from the recorded one";

    return std::nullopt;
}

struct SchemaOnly
{
    int schema = 0;

    MIRO_REFLECT(schema)
};

// The manifest's schema, when the directory has a manifest at all.
std::optional<int> manifestSchema(const FilePath& entry)
{
    if (const auto manifest = loadJsonFile<SchemaOnly>(entry / "manifest.json"))
        return manifest->schema;

    return std::nullopt;
}

// bin/<name>/<key12> with <key12>.json beside it: epm 0.1.2 and older.
bool isOldLayoutEntry(const FilePath& directory)
{
    return Files::isFile(directory.sibling(directory.fileName() + ".json"));
}

void verifyEntry(const FilePath& prefix,
                 const std::string& label,
                 CacheVerification& verification)
{
    const auto problem = [&](const std::string& where, const std::string& what)
    { verification.problems.add(where + ": " + what); };

    const auto schema = manifestSchema(prefix);

    if (!schema)
        return problem(label, "no readable manifest.json (an incomplete entry)");

    if (*schema != entrySchema)
    {
        ++verification.ignored;
        return;
    }

    const auto entry = readEntry(prefix);

    if (!entry)
        return problem(label, "manifest.json does not hold a fingerprint");

    ++verification.entries;

    if (Files::readFile(entry->targetsFile()) != targetsFileText())
        problem(label, "epm-targets.cmake is missing or not epm's facade");

    for (const auto& unit: subdirectories(prefix / "configs"))
    {
        const auto config = unit.fileName();
        const auto* marker = entry->markerOf(config);

        if (!isCanonicalConfigName(config))
        {
            problem(label + " " + config,
                    "not a config's canonical spelling, so never read");
            continue;
        }

        if (marker == nullptr)
        {
            problem(label + " " + config,
                    "no valid " + std::string {configMarkerName}
                        + " (an incomplete config)");
            continue;
        }

        ++verification.configs;

        for (const auto& line: verifyConfig(prefix, *marker))
            problem(label + " " + config, line);
    }
}
} // namespace

Vector<std::string> verifyConfig(const FilePath& entry, const ConfigMarker& marker)
{
    const auto unit = configDirectory(entry, marker.config);
    auto problems = Vector<std::string> {};
    const auto loaded = loadConfigFiles(unit, marker);

    if (!loaded.files)
        return {loaded.error};

    auto listed = Vector<std::string> {std::string {configMarkerName},
                                       std::string {configFilesName}};

    for (const auto& file: loaded.files->files)
    {
        listed.add(file.path);

        if (auto problem = fileProblem(unit / file.path, file))
            problems.add(file.path + ": " + *problem);
    }

    for (const auto& file: loaded.files->shared)
        if (auto problem = fileProblem(entry / file.path, file))
            problems.add(file.path + ": " + *problem);

    auto options = Files::DirectoryOptions {};
    options.recursive = true;
    options.includeHidden = true;

    for (const auto& found: Files::listDirectory(unit, options))
    {
        if (Files::isDirectory(found.path))
            continue;

        const auto relative = found.path.relativeTo(unit).str();

        if (!listed.contains(relative))
            problems.add(relative + ": not recorded in the marker");
    }

    return problems;
}

CacheVerification verifyCache(const CacheRoot& cache)
{
    auto verification = CacheVerification {};

    for (const auto& nameDirectory: subdirectories(cache.bin()))
    {
        for (const auto& platformDirectory: subdirectories(nameDirectory))
        {
            if (isOldLayoutEntry(platformDirectory))
            {
                ++verification.ignored;
                continue;
            }

            for (const auto& prefix: subdirectories(platformDirectory))
                verifyEntry(prefix,
                            nameDirectory.fileName() + "/"
                                + platformDirectory.fileName() + "/"
                                + prefix.fileName(),
                            verification);

            for (const auto& hidden: subdirectories(platformDirectory, true))
                if (hidden.fileName() == ".staging")
                    verification.staging += subdirectories(hidden, true).size();
        }
    }

    return verification;
}

std::string verificationText(const CacheVerification& verification)
{
    auto text = std::string {};

    for (const auto& problem: verification.problems)
        text += problem + "\n";

    const auto count = verification.problems.size();
    text += "verified " + std::to_string(verification.configs) + " configs in "
            + std::to_string(verification.entries) + " entries: "
            + (count == 0 ? std::string {"no problems"}
                          : std::to_string(count)
                                + (count == 1 ? " problem" : " problems"));

    if (verification.ignored > 0)
        text += "; " + std::to_string(verification.ignored)
                + " entries of an older layout ignored";

    if (verification.staging > 0)
        text += "; " + std::to_string(verification.staging)
                + " staging directories (builds in progress, or left by a crash)";

    return text;
}
} // namespace epm
