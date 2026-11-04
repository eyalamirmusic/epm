#include <DependencyResolution/Lock/LockFile.h>
#include <eacp/Core/Utils/Sha256.h>
#include <eacp/Core/Utils/Strings.h>
#include <DependencyResolution/Util/Text.h>

#include <eacp/Core/Utils/Files.h>
#include <ea_data_structures/Utilities/VectorUtilities.h>

#include <Miro/Json.h>

#include <stdexcept>

namespace DependencyResolution
{
namespace
{
constexpr auto lockFileIndent = 2;

bool isOrderedByName(const LockedPackage& left, const LockedPackage& right)
{
    return left.name < right.name;
}

auto isNamed(const std::string& name)
{
    return [&name](const LockedPackage& package) { return package.name == name; };
}
} // namespace

std::optional<LockedPackage> LockFile::findPackage(const std::string& name) const
{
    if (auto found = EA::Vectors::findIf(packages, isNamed(name)))
        return *found;

    return std::nullopt;
}

void LockFile::storePackage(const LockedPackage& package)
{
    if (auto existing = EA::Vectors::findIf(packages, isNamed(package.name)))
        *existing = package;
    else
        packages.push_back(package);
}

std::string LockFile::computeRootHash() const
{
    auto sorted = packages;
    EA::Vectors::sort(sorted, isOrderedByName);

    auto hasher = eacp::Sha256 {};

    for (const auto& package: sorted)
        hasher.update(package.name + ' ' + package.hash + '\n');

    auto digest = hasher.finish();
    return eacp::Strings::toHex(digest);
}

LockFile readLockFile(const eacp::FilePath& path)
{
    if (!eacp::Files::exists(path))
        return {};

    auto lock = Miro::createFromJSONString<LockFile>(eacp::Files::readFile(path));

    if (lock.version != lockFileFormatVersion)
    {
        throw std::runtime_error(path.str() + " has format version "
                                 + std::to_string(lock.version) + "; this epm reads "
                                 + std::to_string(lockFileFormatVersion));
    }

    return lock;
}

std::string formatLockFile(LockFile lock)
{
    EA::Vectors::sort(lock.packages, isOrderedByName);
    lock.root = lock.computeRootHash();
    return Miro::toJSONString(lock, lockFileIndent) + '\n';
}

bool isLockFileCurrent(const eacp::FilePath& path)
{
    if (!eacp::Files::exists(path))
        return true;

    auto text = eacp::Files::readFile(path);
    return formatLockFile(Miro::createFromJSONString<LockFile>(text)) == text;
}

void writeLockFile(const eacp::FilePath& path, LockFile lock)
{
    eacp::Files::writeFileAtomically(path, formatLockFile(std::move(lock)));
}
} // namespace DependencyResolution
