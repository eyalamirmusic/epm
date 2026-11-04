#include <epm/Lock/ErrorMessages.h>

#include <eacp/Core/Utils/Format.h>
#include <eacp/Core/Utils/Strings.h>

#include <algorithm>
#include <ea_data_structures/Structures/Array.h>
#include <cctype>
#include <string_view>

namespace epm
{
namespace
{
constexpr auto packageCommands =
    EA::Array<std::string_view, 5> {"epm_add_package(",
                                    "cpmaddpackage(",
                                    "cpmfindpackage(",
                                    "cpmdeclarepackage(",
                                    "fetchcontent_declare("};

constexpr auto findPackageCommands =
    EA::Array<std::string_view, 1> {"find_package("};

std::string formatUpdateConfigureCommand(const std::string& buildDirectory)
{
    return eacp::Strings::format("cmake -B {} -DEPM_LOCK_MODE=update",
                                 buildDirectory);
}

bool isNameCharacter(char character)
{
    auto isAlphanumeric = std::isalnum(static_cast<unsigned char>(character)) != 0;
    return isAlphanumeric || character == '_' || character == '-'
           || character == '.';
}

bool containsNameAsWord(std::string_view text, const std::string& name)
{
    auto position = text.find(name);

    while (position != std::string_view::npos)
    {
        auto end = position + name.size();
        auto startsWord = position == 0 || !isNameCharacter(text[position - 1]);
        auto endsWord = end == text.size() || !isNameCharacter(text[end]);

        if (startsWord && endsWord)
            return true;

        position = text.find(name, position + 1);
    }

    return false;
}

template <typename Commands>
bool startsCommand(const Commands& commands,
                   std::string_view loweredText,
                   std::size_t position)
{
    return std::any_of(
        commands.begin(),
        commands.end(),
        [&](std::string_view command)
        { return loweredText.substr(position).starts_with(command); });
}

int countLinesBefore(std::string_view text, std::size_t position)
{
    auto newlines = std::count(text.begin(), text.begin() + position, '\n');
    return static_cast<int>(newlines) + 1;
}
} // namespace

std::string formatUserError(const std::string& problem, const std::string& fix)
{
    return eacp::Strings::format("{}\n  {}", problem, fix);
}

std::string formatRequestLocation(const RequestLocation& location)
{
    if (!location.line)
        return location.file;

    return eacp::Strings::format("{}:{}", location.file, *location.line);
}

std::string describeLockMismatch(const LockMismatch& mismatch)
{
    return formatUserError(
        eacp::Strings::format("{} asks for {} {}, but {} pins it {}.",
                              formatRequestLocation(mismatch.requestedAt),
                              mismatch.name,
                              mismatch.requestedDescription,
                              mismatch.lockFile,
                              mismatch.lockedDescription),
        eacp::Strings::format(
            "To lock what the CMake asks for, run: {}",
            formatUpdateConfigureCommand(mismatch.buildDirectory)));
}

std::string describeUnlockedRequest(const UnlockedRequest& request)
{
    auto location = formatRequestLocation(request.requestedAt);
    auto fix = formatUpdateConfigureCommand(request.buildDirectory);

    if (!request.lockFileExists)
    {
        return formatUserError(
            eacp::Strings::format("{} asks for {}, and there is no {} yet; offline "
                                  "mode cannot write one.",
                                  location,
                                  request.name,
                                  request.lockFile),
            "To write it, configure once without -DEPM_LOCK_MODE=offline.");
    }

    return formatUserError(eacp::Strings::format("{} asks for {}, which {} does "
                                                 "not have.",
                                                 location,
                                                 request.name,
                                                 request.lockFile),
                           eacp::Strings::format("To add it, run: {}", fix));
}

std::string describeUnknownUpdateName(const UnknownUpdateName& unknown)
{
    auto lockedNames = eacp::Strings::join(unknown.lockedNames, ", ");

    if (lockedNames.empty())
        lockedNames = "none";

    return formatUserError(
        eacp::Strings::format("{} has no package named {}. It has: {}.",
                              unknown.lockFile,
                              unknown.name,
                              lockedNames),
        "To add a new package, add it to CMakeLists.txt and run: "
        "cmake -B <build directory> -DEPM_LOCK_MODE=update");
}

std::string describeFailedDownload(const FailedDownload& download)
{
    auto what = download.name;

    if (!download.commit.empty())
        what =
            eacp::Strings::format("{} at commit {}", download.name, download.commit);

    return formatUserError(
        eacp::Strings::format("Could not download {} from {}, and the cache "
                              "does not have it yet.",
                              what,
                              download.url),
        "Check the network connection and configure again; the error above "
        "says why the download failed.");
}

std::string describeChangedContent(const ChangedContent& changed)
{
    return formatUserError(
        eacp::Strings::format("{} downloaded from {} has different files than {} "
                              "records: the source changed upstream.",
                              changed.name,
                              changed.url,
                              changed.lockFile),
        eacp::Strings::format("If the change is expected, run: epm lock update {}",
                              changed.name));
}

std::string describeDamagedCacheEntry(const DamagedCacheEntry& entry)
{
    return formatUserError(
        eacp::Strings::format("The cached copy of {} in {} is missing or its files "
                              "were changed after download.",
                              entry.name,
                              entry.cacheDirectory),
        "Configure again to download a fresh copy: cmake -B <build directory>");
}

namespace
{
template <typename Commands>
std::optional<int> findCommandLine(const Commands& commands,
                                   const std::string& cmakeText,
                                   const std::string& name)
{
    auto lowered = eacp::Strings::toLower(cmakeText);
    auto foundLine = std::optional<int> {};

    for (auto position = std::size_t {0}; position < lowered.size(); ++position)
    {
        if (!startsCommand(commands, lowered, position))
            continue;

        auto end = cmakeText.find(')', position);

        if (end == std::string::npos)
            end = cmakeText.size();

        auto call = std::string_view {cmakeText}.substr(position, end - position);

        if (!containsNameAsWord(call, name))
            continue;

        if (foundLine)
            return std::nullopt;

        foundLine = countLinesBefore(cmakeText, position);
    }

    return foundLine;
}
} // namespace

std::optional<int> findPackageRequestLine(const std::string& cmakeText,
                                          const std::string& name)
{
    return findCommandLine(packageCommands, cmakeText, name);
}

std::optional<int> findFindPackageLine(const std::string& cmakeText,
                                       const std::string& name)
{
    return findCommandLine(findPackageCommands, cmakeText, name);
}
} // namespace epm
