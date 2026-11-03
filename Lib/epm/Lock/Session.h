#pragma once

#include <eacp/Core/Utils/FilePath.h>

#include <epm/Lock/Requirements.h>

#include <Miro/Reflect.h>

#include <optional>
#include <string>
#include <eacp/Core/Utils/Containers.h>

namespace epm
{
// What one configure has added so far, kept in <build>/epm/session.json
// so a package a second subproject asks for is added once, as CPM does.
struct SessionPackage
{
    std::string name;
    std::string requestedRef;
    std::string sourceDirectory;
    std::string binaryDirectory;

    // "source" or "prebuilt": how epm added it.
    std::string mode = "source";

    // The Merkle hash of the cached tree; empty for a local checkout.
    std::string hash;

    // The commit it was added at, or an archive's hash; what a later
    // requirement is compared against.
    std::string commit;

    MIRO_REFLECT(
        name, requestedRef, sourceDirectory, binaryDirectory, mode, hash, commit)
};

struct SessionOverride
{
    std::string name;
    std::string sourceDirectory;
    std::string variable;

    MIRO_REFLECT(name, sourceDirectory, variable)
};

// A requirement on `name` known before or after it is added: one a project
// made, or one a dependency's own epm.lock lists.
struct SessionRequirement
{
    std::string name;
    Requirement requirement;

    MIRO_REFLECT(name, requirement)
};

struct Session
{
    EA::Vector<SessionOverride> overrides;
    EA::Vector<SessionPackage> packages;

    // Locked mode with no lock yet: this configure writes it, as update would.
    bool isWritingNewLock = false;

    EA::Vector<SessionRequirement> requirements;

    MIRO_REFLECT(overrides, packages, isWritingNewLock, requirements)

    EA::Vector<Requirement> findRequirements(const std::string& name) const;
    std::optional<Requirement> findRequirementBy(const std::string& name,
                                                 const std::string& requester) const;
    void addRequirement(const std::string& name, const Requirement& requirement);

    std::optional<SessionPackage> findPackage(const std::string& name) const;
    std::optional<SessionOverride> findOverride(const std::string& name) const;
};

eacp::FilePath getSessionPath(const eacp::FilePath& buildDirectory);
Session readSession(const eacp::FilePath& buildDirectory);
void writeSession(const eacp::FilePath& buildDirectory, const Session& session);
} // namespace epm
