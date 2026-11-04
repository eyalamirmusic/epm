#include <epm/Lock/Session.h>

#include <eacp/Core/Utils/Files.h>

#include <ea_data_structures/Utilities/VectorUtilities.h>

#include <Miro/Json.h>

namespace epm
{
std::optional<SessionPackage> Session::findPackage(const std::string& name) const
{
    auto isNamed = [&name](const SessionPackage& package)
    { return package.name == name; };

    if (auto found = EA::Vectors::findIf(packages, isNamed))
        return *found;

    return std::nullopt;
}

std::optional<SessionOverride> Session::findOverride(const std::string& name) const
{
    auto isNamed = [&name](const SessionOverride& entry)
    { return entry.name == name; };

    if (auto found = EA::Vectors::findIf(overrides, isNamed))
        return *found;

    return std::nullopt;
}

EA::Vector<Requirement> Session::findRequirements(const std::string& name) const
{
    auto found = EA::Vector<Requirement> {};

    for (const auto& entry: requirements)
    {
        if (entry.name == name)
            found.add(entry.requirement);
    }

    return found;
}

std::optional<Requirement>
    Session::findRequirementBy(const std::string& name,
                               const std::string& requester) const
{
    for (const auto& entry: requirements)
    {
        if (entry.name == name && entry.requirement.requestedBy == requester)
            return entry.requirement;
    }

    return std::nullopt;
}

void Session::addRequirement(const std::string& name, const Requirement& requirement)
{
    for (const auto& entry: requirements)
    {
        if (entry.name == name && isSameRequirement(entry.requirement, requirement))
            return;
    }

    requirements.add({name, requirement});
}

eacp::FilePath getSessionPath(const eacp::FilePath& buildDirectory)
{
    return buildDirectory / "epm" / "session.json";
}

Session readSession(const eacp::FilePath& buildDirectory)
{
    auto path = getSessionPath(buildDirectory);

    if (!eacp::Files::isFile(path))
        return {};

    return Miro::createFromJSONString<Session>(eacp::Files::readFile(path));
}

void writeSession(const eacp::FilePath& buildDirectory, const Session& session)
{
    auto path = getSessionPath(buildDirectory);
    eacp::Files::createDirectories(path.parentDirectory());
    eacp::Files::writeFile(path, std::string_view {Miro::toJSONString(session)});
}
} // namespace epm
