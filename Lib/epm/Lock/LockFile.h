#pragma once

#include <epm/Spec/PackageKind.h>
#include <epm/Source/Submodules.h>

#include <eacp/Core/Utils/Containers.h>
#include <eacp/Core/Utils/FilePath.h>

#include <Miro/Reflect.h>

#include <optional>
#include <eacp/Core/Utils/Containers.h>
#include <string>

namespace epm
{
constexpr auto lockFileFormatVersion = 1;
constexpr auto lockFileName = "epm.lock";

struct LockedPackage
{
    std::string name;
    PackageKind kind = PackageKind::git;
    std::string url;
    std::string requestedRef;
    EA::Vector<std::string> patches;
    SubmoduleSelection submoduleSelection;
    std::string commit;
    EA::Vector<LockedSubmodule> submodules;
    bool usesGitLfs = false;
    std::string hash;

    MIRO_REFLECT(name,
                 kind,
                 url,
                 requestedRef,
                 patches,
                 submoduleSelection,
                 commit,
                 submodules,
                 usesGitLfs,
                 hash)
};

struct LockFile
{
    int version = lockFileFormatVersion;
    std::string root;
    EA::Vector<LockedPackage> packages;

    MIRO_REFLECT(version, root, packages)

    std::optional<LockedPackage> findPackage(const std::string& name) const;
    void storePackage(const LockedPackage& package);
    std::string computeRootHash() const;
};

// An empty lock when the file does not exist yet.
LockFile readLockFile(const eacp::FilePath& path);

// Whether the file holds exactly what this epm would write for it, so a
// lock from an older version, missing fields this one records, is noticed.
bool isLockFileCurrent(const eacp::FilePath& path);

// Sorts the packages by name and recomputes the root hash before writing, so
// the file only changes when a package does.
void writeLockFile(const eacp::FilePath& path, LockFile lock);
} // namespace epm
