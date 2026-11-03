#include <epm/Util/Sha256.h>

#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/TemporaryFile.h>

#include <NanoTest/NanoTest.h>

using namespace nano;
using epm::computeSha256Hex;

// FIPS 180-2 appendix B vectors, plus the empty message.
auto tEmpty = test("Sha256/empty") = []
{
    check(computeSha256Hex("")
          == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
};

auto tAbc = test("Sha256/abc") = []
{
    check(computeSha256Hex("abc")
          == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
};

auto tTwoBlocks = test("Sha256/twoBlocks") = []
{
    check(
        computeSha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")
        == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
};

auto tMillionA = test("Sha256/millionA") = []
{
    check(computeSha256Hex(std::string(1000000, 'a'))
          == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
};

auto tFile = test("Sha256/file") = []
{
    auto file = eacp::TemporaryFile {".txt"};
    eacp::Files::writeFile(file.path(),
                           eacp::Span<const std::uint8_t> {
                               reinterpret_cast<const std::uint8_t*>("abc"), 3});

    check(epm::computeFileSha256Hex(file.path()) == computeSha256Hex("abc"));
};

auto tEmptyFile = test("Sha256/emptyFile") = []
{
    auto file = eacp::TemporaryFile {".txt"};
    eacp::Files::writeFile(file.path(), eacp::Span<const std::uint8_t> {});

    check(epm::computeFileSha256Hex(file.path()) == computeSha256Hex(""));
};

auto tShaMissingFile = test("Sha256/missingFile") = []
{
    auto file = eacp::TemporaryFile {".txt"};
    auto threw = false;

    try
    {
        epm::computeFileSha256Hex(file.path());
    }
    catch (const std::runtime_error&)
    {
        threw = true;
    }

    check(threw);
};
