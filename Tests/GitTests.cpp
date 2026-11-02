#include <epm/Util/Git.h>

#include <NanoTest/NanoTest.h>

using namespace nano;
using epm::Git::parseLsRemote;

namespace
{
constexpr auto lsRemoteFixture =
    "d948b1da076878517d2daf9ed29aa23ed6732d16\tHEAD\n"
    "57b2ef604bd12d340d7c62873da74525611e9071\trefs/heads/develop\n"
    "d948b1da076878517d2daf9ed29aa23ed6732d16\trefs/heads/main\n"
    "1111111111111111111111111111111111111111\trefs/heads/feature/with/slashes\r\n"
    "2222222222222222222222222222222222222222\trefs/tags/v1.0\n"
    "3333333333333333333333333333333333333333\trefs/tags/v1.0^{}\n"
    "\n"
    "not a ref line\n";
}

auto tParsesEveryRef = test("Git/lsRemote/parsesEveryRef") = []
{
    const auto refs = parseLsRemote(lsRemoteFixture);

    check(refs.size() == 6);
    check(refs[0].ref == "HEAD");
    check(refs[0].sha == "d948b1da076878517d2daf9ed29aa23ed6732d16");
    check(refs[2].ref == "refs/heads/main");
};

auto tKeepsSlashesAndStripsCr = test("Git/lsRemote/slashesAndCr") = []
{
    const auto refs = parseLsRemote(lsRemoteFixture);

    check(refs[3].ref == "refs/heads/feature/with/slashes");
    check(refs[3].sha == "1111111111111111111111111111111111111111");
};

auto tPeeledTag = test("Git/lsRemote/peeledTag") = []
{
    const auto refs = parseLsRemote(lsRemoteFixture);

    check(refs[4].ref == "refs/tags/v1.0");
    check(refs[5].ref == "refs/tags/v1.0^{}");
    check(refs[5].sha == "3333333333333333333333333333333333333333");
};

auto tEmptyOutput =
    test("Git/lsRemote/empty") = [] { check(parseLsRemote("").empty()); };
