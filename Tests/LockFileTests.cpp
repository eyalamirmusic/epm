#include "TestDirectory.h"

#include <epm/Lock/LockFile.h>

#include <NanoTest/NanoTest.h>

using namespace nano;

namespace LockFileTests
{
epm::LockedPackage createPackage(const std::string& name, const std::string& hash)
{
    return {name,
            epm::PackageKind::git,
            "https://example.com/" + name,
            "main",
            {},
            {},
            "abc123",
            {},
            false,
            hash};
}
} // namespace LockFileTests

auto tRoundTrip = test("LockFile/writeThenReadRoundTrips") = []
{
    auto directory = createEmptyTestDirectory("lock-roundtrip");
    auto path = directory / "epm.lock";

    auto lock = epm::LockFile {};
    lock.storePackage(LockFileTests::createPackage("zeta", "11"));
    lock.storePackage(LockFileTests::createPackage("alpha", "22"));
    epm::writeLockFile(path, lock);

    auto read = epm::readLockFile(path);
    check(read.packages.size() == 2);
    check(read.packages[0].name == "alpha");
    check(read.findPackage("zeta")->hash == "11");
    check(read.root == read.computeRootHash());
};

auto tMissingIsEmpty = test("LockFile/missingFileReadsEmpty") = []
{
    auto directory = createEmptyTestDirectory("lock-missing");
    check(epm::readLockFile(directory / "epm.lock").packages.empty());
};

auto tStoreReplaces = test("LockFile/storeReplacesSameName") = []
{
    auto lock = epm::LockFile {};
    lock.storePackage(LockFileTests::createPackage("a", "1"));
    lock.storePackage(LockFileTests::createPackage("a", "2"));
    check(lock.packages.size() == 1);
    check(lock.findPackage("a")->hash == "2");
};

auto tRootDependsOnOrderFreeContent = test("LockFile/rootIgnoresInsertionOrder") = []
{
    auto first = epm::LockFile {};
    first.storePackage(LockFileTests::createPackage("a", "1"));
    first.storePackage(LockFileTests::createPackage("b", "2"));

    auto second = epm::LockFile {};
    second.storePackage(LockFileTests::createPackage("b", "2"));
    second.storePackage(LockFileTests::createPackage("a", "1"));

    check(first.computeRootHash() == second.computeRootHash());

    second.storePackage(LockFileTests::createPackage("b", "3"));
    check(first.computeRootHash() != second.computeRootHash());
};

auto tOlderLockIsNotCurrent = test("LockFile/lockMissingNewFieldsIsNotCurrent") = []
{
    auto directory = createEmptyTestDirectory("lock-current");
    auto path = directory / "epm.lock";

    auto lock = epm::LockFile {};
    lock.storePackage(LockFileTests::createPackage("a", "1"));
    epm::writeLockFile(path, lock);
    check(epm::isLockFileCurrent(path));

    auto text = eacp::Files::readFile(path);
    auto field = text.find("      \"submodules\": [],\n");
    check(field != std::string::npos);
    text.erase(field, std::string_view {"      \"submodules\": [],\n"}.size());
    eacp::Files::writeFile(path, std::string_view {text});
    check(!epm::isLockFileCurrent(path));
};
