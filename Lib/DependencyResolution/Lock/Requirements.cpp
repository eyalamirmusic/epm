#include <DependencyResolution/Lock/Requirements.h>
#include <DependencyResolution/Lock/ErrorMessages.h>

#include <eacp/Core/Utils/Strings.h>

#include <ea_data_structures/Structures/Array.h>

namespace DependencyResolution
{
namespace
{
constexpr auto versionPrefix = 'v';
constexpr auto maximumVersionParts = 3;

bool haveOneUrl(const EA::Vector<Requirement>& requirements)
{
    for (const auto& requirement: requirements)
    {
        if (requirement.url != requirements.front().url)
            return false;
    }

    return true;
}

bool haveOneCommit(const EA::Vector<Requirement>& requirements)
{
    for (const auto& requirement: requirements)
    {
        if (requirement.commit != requirements.front().commit)
            return false;
    }

    return true;
}

std::optional<Requirement>
    findHighestVersion(const EA::Vector<Requirement>& requirements)
{
    auto highest = std::optional<Requirement> {};
    auto highestVersion = SemanticVersion {};

    for (const auto& requirement: requirements)
    {
        auto version = parseSemanticVersion(requirement.requestedRef);

        if (!version)
            return std::nullopt;

        if (!highest || isOlderVersion(highestVersion, *version))
        {
            highest = requirement;
            highestVersion = *version;
        }
    }

    return highest;
}
} // namespace

std::optional<SemanticVersion> parseSemanticVersion(std::string_view tag)
{
    if (!tag.empty() && tag.front() == versionPrefix)
        tag.remove_prefix(1);

    auto parts = eacp::Strings::split(tag, '.');

    if (parts.empty() || parts.size() > maximumVersionParts)
        return std::nullopt;

    auto numbers = EA::Array<int, maximumVersionParts> {};

    for (auto index = 0; index < parts.size(); ++index)
    {
        const auto& part = parts[index];
        auto isDigits = !part.empty()
                        && part.find_first_not_of("0123456789") == std::string::npos;

        if (!isDigits)
            return std::nullopt;

        numbers[index] = eacp::Strings::parseIntOr(part);
    }

    return SemanticVersion {numbers[0], numbers[1], numbers[2]};
}

bool isOlderVersion(const SemanticVersion& first, const SemanticVersion& second)
{
    if (first.major != second.major)
        return first.major < second.major;

    if (first.minor != second.minor)
        return first.minor < second.minor;

    return first.patch < second.patch;
}

RequirementSelection selectRequirement(const EA::Vector<Requirement>& requirements)
{
    if (requirements.empty() || !haveOneUrl(requirements))
        return {true, {}};

    if (auto highest = findHighestVersion(requirements))
        return {false, *highest};

    if (haveOneCommit(requirements))
        return {false, requirements.front()};

    return {true, {}};
}

bool isSameRequirement(const Requirement& first, const Requirement& second)
{
    return first.requestedBy == second.requestedBy && first.url == second.url
           && first.requestedRef == second.requestedRef;
}

std::string describeRequirementConflict(const std::string& name,
                                        const EA::Vector<Requirement>& requirements)
{
    auto problem = "the projects disagree on " + name + ":";

    for (const auto& requirement: requirements)
        problem += "\n  " + requirement.requestedBy + " asks for " + requirement.url
                   + " at '" + requirement.requestedRef + "'";

    return formatUserError(problem,
                           "Settle it with a root override of " + name
                               + " naming one commit or tag.");
}
} // namespace DependencyResolution
