#include <Messages.h>

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace lockstep;

namespace
{
constexpr auto requestLine = 5;
} // namespace

auto tLockMismatch = test("Messages/lockMismatchNamesBothRefsAndTheFix") = []
{
    auto text = describeLockMismatch({{"CMakeLists.txt", requestLine},
                                      "fmt",
                                      "lockstep.lock",
                                      "at '11.1.0'",
                                      "at '11.0.2'",
                                      "build"});
    check(text
          == "CMakeLists.txt:5 asks for fmt at '11.1.0', but lockstep.lock pins it "
             "at '11.0.2'.\n"
             "  To lock what the CMake asks for, run: cmake -B build "
             "-DLOCKSTEP_MODE=update");
};

auto tUnlockedRequest = test("Messages/unlockedRequestSaysHowToAddIt") = []
{
    auto text = describeUnlockedRequest(
        {{"CMakeLists.txt", requestLine}, "fmt", "lockstep.lock", true, "build"});
    check(text
          == "CMakeLists.txt:5 asks for fmt, which lockstep.lock does not have.\n"
             "  To add it, run: cmake -B build -DLOCKSTEP_MODE=update");
};

auto tNoLockFile = test("Messages/missingLockFileSaysHowToWriteIt") = []
{
    auto text = describeUnlockedRequest(
        {{"CMakeLists.txt", requestLine}, "fmt", "lockstep.lock", false, "build"});
    check(text
          == "CMakeLists.txt:5 asks for fmt, and there is no lockstep.lock yet; "
             "offline mode cannot write one.\n"
             "  To write it, configure once without -DLOCKSTEP_MODE=offline.");
};

auto tUnknownUpdateName = test("Messages/unknownUpdateNameListsTheLock") = []
{
    auto text = describeUnknownUpdateName({"fnt", "lockstep.lock", {"fmt", "Miro"}});
    check(text
          == "lockstep.lock has no package named fnt. It has: fmt, Miro.\n"
             "  To add a new package, add it to CMakeLists.txt and run: cmake -B "
             "<build directory> -DLOCKSTEP_MODE=update");
};

auto tFailedDownload = test("Messages/failedDownloadNamesPackageAndCommit") = []
{
    auto text = describeFailedDownload(
        {"fmt", "https://github.com/fmtlib/fmt.git", "0c9fce2"});
    check(text
          == "Could not download fmt at commit 0c9fce2 from "
             "https://github.com/fmtlib/fmt.git, and the cache does not have it "
             "yet.\n"
             "  Check the network connection and configure again; the error above "
             "says why the download failed.");
};

auto tChangedContent = test("Messages/changedContentSuggestsUpdate") = []
{
    auto text = describeChangedContent(
        {"fmt", "https://example.com/fmt.zip", "lockstep.lock"});
    check(text
          == "fmt downloaded from https://example.com/fmt.zip has different files "
             "than lockstep.lock records: the source changed upstream.\n"
             "  If the change is expected, run: lockstep update fmt");
};

auto tDamagedCache = test("Messages/damagedCacheEntrySaysConfigureAgain") = []
{
    auto text = describeDamagedCacheEntry({"fmt", "/cache/src/abc"});
    check(text
          == "The cached copy of fmt in /cache/src/abc is missing or its files were "
             "changed after download.\n"
             "  Configure again to download a fresh copy: cmake -B <build "
             "directory>");
};

auto tRequestLine = test("Messages/findsTheLineOfTheOneCallNamingThePackage") = []
{
    auto text =
        std::string {"project(x)\n\nlockstep_add_package(NAME fmtlib\n"
                     "  GITHUB a/b)\nCPMAddPackage(\"gh:fmtlib/fmt#11.0.2\")\n"};
    check(findPackageRequestLine(text, "fmt") == 5);
    check(findPackageRequestLine(text, "fmtlib") == std::nullopt);
    check(findPackageRequestLine(text, "zlib") == std::nullopt);
};
