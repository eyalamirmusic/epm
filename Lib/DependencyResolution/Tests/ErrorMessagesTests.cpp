#include <DependencyResolution/Lock/ErrorMessages.h>

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace DependencyResolution;

namespace
{
constexpr auto requestLine = 5;
} // namespace

auto tLockMismatch = test("Messages/lockMismatchNamesBothRefsAndTheFix") = []
{
    auto text = describeLockMismatch({{"CMakeLists.txt", requestLine},
                                      "fmt",
                                      "epm-lock.json",
                                      "at '11.1.0'",
                                      "at '11.0.2'",
                                      "cmake -B build -DEPM_LOCK_MODE=update"});
    check(text
          == "CMakeLists.txt:5 asks for fmt at '11.1.0', but epm-lock.json pins it "
             "at '11.0.2'.\n"
             "  To lock what is asked for, run: cmake -B build "
             "-DEPM_LOCK_MODE=update");
};

auto tUnlockedRequest = test("Messages/unlockedRequestSaysHowToAddIt") = []
{
    auto text = describeUnlockedRequest({{"CMakeLists.txt", requestLine},
                                         "fmt",
                                         "epm-lock.json",
                                         true,
                                         "cmake -B build -DEPM_LOCK_MODE=update"});
    check(text
          == "CMakeLists.txt:5 asks for fmt, which epm-lock.json does not have.\n"
             "  To add it, run: cmake -B build -DEPM_LOCK_MODE=update");
};

auto tNoLockFile = test("Messages/missingLockFileSaysHowToWriteIt") = []
{
    auto text = describeUnlockedRequest(
        {{"CMakeLists.txt", requestLine}, "fmt", "epm-lock.json", false, {}});
    check(text
          == "CMakeLists.txt:5 asks for fmt, and there is no epm-lock.json yet; "
             "offline mode cannot write one.\n"
             "  To write it, resolve once outside offline mode.");
};

auto tUnknownUpdateName = test("Messages/unknownUpdateNameListsTheLock") = []
{
    auto text = describeUnknownUpdateName({"fnt", "epm-lock.json", {"fmt", "Miro"}});
    check(text
          == "epm-lock.json has no package named fnt. It has: fmt, Miro.\n"
             "  To add a new package, request it and resolve in update mode.");
};

auto tFailedDownload = test("Messages/failedDownloadNamesPackageAndCommit") = []
{
    auto text = describeFailedDownload(
        {"fmt", "https://github.com/fmtlib/fmt.git", "0c9fce2"});
    check(text
          == "Could not download fmt at commit 0c9fce2 from "
             "https://github.com/fmtlib/fmt.git, and the cache does not have it "
             "yet.\n"
             "  Check the network connection and resolve again; the error above "
             "says why the download failed.");
};

auto tChangedContent = test("Messages/changedContentSuggestsUpdate") = []
{
    auto text = describeChangedContent(
        {"fmt", "https://example.com/fmt.zip", "epm-lock.json"});
    check(text
          == "fmt downloaded from https://example.com/fmt.zip has different files "
             "than epm-lock.json records: the source changed upstream.\n"
             "  If the change is expected, re-lock fmt in update mode.");
};

auto tDamagedCache = test("Messages/damagedCacheEntrySaysConfigureAgain") = []
{
    auto text = describeDamagedCacheEntry({"fmt", "/cache/src/abc"});
    check(text
          == "The cached copy of fmt in /cache/src/abc is missing or its files were "
             "changed after download.\n"
             "  Resolve again to download a fresh copy.");
};
