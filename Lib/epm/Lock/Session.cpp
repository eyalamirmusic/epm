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
