#include <DependencyResolution/Source/SourceSnapshot.h>

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace DependencyResolution;

auto tFullCommitHash = test("SourceCache/isFullCommitHash") = []
{
    check(isFullCommitHash("0123456789abcdef0123456789abcdef01234567"));
    check(!isFullCommitHash("0123456"));
    check(!isFullCommitHash("0123456789abcdef0123456789abcdef0123456g"));
};
