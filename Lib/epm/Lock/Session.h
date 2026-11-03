#pragma once

#include <eacp/Core/Utils/FilePath.h>

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

    MIRO_REFLECT(name, requestedRef, sourceDirectory, binaryDirectory, mode, hash)
};

struct SessionOverride
{
    std::string name;
    std::string sourceDirectory;
    std::string variable;

    MIRO_REFLECT(name, sourceDirectory, variable)
};

struct Session
{
    EA::Vector<SessionOverride> overrides;
    EA::Vector<SessionPackage> packages;

    // Locked mode with no lock yet: this configure writes it, as update would.
    bool isWritingNewLock = false;

    MIRO_REFLECT(overrides, packages, isWritingNewLock)

    std::optional<SessionPackage> findPackage(const std::string& name) const;
    std::optional<SessionOverride> findOverride(const std::string& name) const;
};

eacp::FilePath getSessionPath(const eacp::FilePath& buildDirectory);
Session readSession(const eacp::FilePath& buildDirectory);
void writeSession(const eacp::FilePath& buildDirectory, const Session& session);
} // namespace epm
