#include "TestDirectory.h"

#include <LongPath.h>
#include <MerkleHash.h>

#include <fstream>

#include <NanoTest/NanoTest.h>

using namespace nano;
using lockstep::computeMerkleHash;
using lockstep::computeTreeStamp;

namespace MerkleHashTests
{
eacp::FilePath createSampleTree(const std::string& name)
{
    auto root = createEmptyTestDirectory(name);
    writeTestFile(root / "CMakeLists.txt", "project(sample)\n");
    writeTestFile(root / "src" / "a.cpp", "int a;\n");
    writeTestFile(root / "src" / "b.cpp", "int b;\n");
    return root;
}

constexpr auto windowsMaxPathLength = std::size_t {260};
constexpr auto deepDirectoryNameLength = std::size_t {50};
constexpr auto deepDirectoryCount = 6;

eacp::FilePath createTreeDeeperThanMaxPath(const std::string& name)
{
    auto root = createEmptyTestDirectory(name);
    auto deepest = root;

    for (auto level = 0; level < deepDirectoryCount; ++level)
        deepest = deepest / std::string(deepDirectoryNameLength, 'a' + level);

    auto file = deepest / "box_curve25519xchacha20poly1305.c";
    std::filesystem::create_directories(lockstep::getLongPathSafeStdPath(deepest));
    std::ofstream {lockstep::getLongPathSafeStdPath(file)} << "int deep;\n";
    check(file.str().size() > windowsMaxPathLength);
    return root;
}
} // namespace MerkleHashTests

auto tSameContentSameHash = test("Merkle/sameContentSameHash") = []
{
    auto first = MerkleHashTests::createSampleTree("merkle-first");
    auto second = MerkleHashTests::createSampleTree("merkle-second");
    check(computeMerkleHash(first) == computeMerkleHash(second));
};

auto tKnownHash = test("Merkle/emptyDirectoryIsHashOfNothing") = []
{
    auto empty = createEmptyTestDirectory("merkle-empty");
    check(computeMerkleHash(empty)
          == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
};

auto tContentChanges = test("Merkle/contentChangeChangesHash") = []
{
    auto tree = MerkleHashTests::createSampleTree("merkle-content");
    auto before = computeMerkleHash(tree);
    writeTestFile(tree / "src" / "b.cpp", "int c;\n");
    check(computeMerkleHash(tree) != before);
};

auto tRenameChanges = test("Merkle/renameChangesHash") = []
{
    auto tree = MerkleHashTests::createSampleTree("merkle-rename");
    auto before = computeMerkleHash(tree);
    eacp::Files::moveFile(tree / "src" / "b.cpp", tree / "src" / "c.cpp");
    check(computeMerkleHash(tree) != before);
};

auto tEmptyDirectoryCounts = test("Merkle/emptySubdirectoryChangesHash") = []
{
    auto tree = MerkleHashTests::createSampleTree("merkle-subdir");
    auto before = computeMerkleHash(tree);
    eacp::Files::createDirectories(tree / "extra");
    check(computeMerkleHash(tree) != before);
};

auto tGitExcluded = test("Merkle/gitDirectoryIsIgnored") = []
{
    auto tree = MerkleHashTests::createSampleTree("merkle-git");
    auto before = computeMerkleHash(tree);
    writeTestFile(tree / ".git" / "HEAD", "ref: refs/heads/main\n");
    writeTestFile(tree / "src" / ".git", "gitdir: ../.git/modules/src\n");
    check(computeMerkleHash(tree) == before);
};

auto tStampTracksChanges = test("Merkle/stampChangesWhenAFileGrows") = []
{
    auto tree = MerkleHashTests::createSampleTree("merkle-stamp");
    auto before = computeTreeStamp(tree);
    check(computeTreeStamp(tree) == before);
    writeTestFile(tree / "src" / "a.cpp", "int a = 1;\n");
    check(computeTreeStamp(tree) != before);
};

auto tLongPaths = test("Merkle/hashesAndRemovesTreesDeeperThanMaxPath") = []
{
    auto first = MerkleHashTests::createTreeDeeperThanMaxPath("merkle-long-first");
    auto second = MerkleHashTests::createTreeDeeperThanMaxPath("merkle-long-second");
    check(computeMerkleHash(first) == computeMerkleHash(second));
    check(!computeTreeStamp(first).empty());

    lockstep::removeTreeAtAnyLength(first);
    lockstep::removeTreeAtAnyLength(second);
    check(!std::filesystem::exists(lockstep::getLongPathSafeStdPath(first)));
};
