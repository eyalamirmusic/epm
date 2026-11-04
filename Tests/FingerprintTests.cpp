#include <epm/Cache/Fingerprint.h>

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace epm;

namespace
{
Env fingerprintEnv()
{
    auto env = Env {};
    env.cxxCompiler = "/usr/bin/c++";
    env.cxxCompilerId = "AppleClang";
    env.cxxCompilerVersion = "17.0.0.17000013";
    env.systemName = "Darwin";
    env.systemProcessor = "arm64";
    env.osxDeploymentTarget = "11.0";
    env.cxxStandard = "20";
    return env;
}

Request fingerprintRequest()
{
    auto request = Request {};
    request.name = "eacp";
    request.gitUrl = "https://github.com/eyalamirmusic/eacp.git";
    request.gitRef = "main";
    request.configs = {"Release"};
    request.options = {{"EACP_ENABLE_TESTS", "OFF"}};
    return request;
}

AbiFingerprint fingerprintWith(const Env& env, const std::string& sourceHash = "abc")
{
    return fingerprintFor(fingerprintRequest(), env, sourceHash);
}

// Xcode's clang against the command line tools' clang, same major version.
Env xcodeEnv()
{
    auto env = fingerprintEnv();
    env.cxxCompiler = "/Applications/Xcode.app/Contents/Developer/Toolchains/"
                      "XcodeDefault.xctoolchain/usr/bin/clang++";
    env.cxxCompilerVersion = "17.0.0.17000319";
    return env;
}
} // namespace

auto tFpFields = test("Fingerprint/fields") = []
{
    const auto fingerprint = fingerprintWith(fingerprintEnv());

    check(fingerprint.platform == "macos-arm64");
    check(fingerprint.compilerId == "AppleClang");
    check(fingerprint.compilerVersion == "17.0.0.17000013");
    check(fingerprint.compilerMajor == "17");
    check(fingerprint.compilerPath == "/usr/bin/c++");
    check(fingerprint.sourceHash == "abc");
    check(fingerprint.toolchainFileHash.empty());
};

auto tFpMajor = test("Fingerprint/compilerMajor") = []
{
    check(compilerMajor("17.0.0.17000013") == "17");
    check(compilerMajor("19.38.33130") == "19");
    check(compilerMajor("13") == "13");
    check(compilerMajor("").empty());
};

auto tFpCanonical = test("Fingerprint/canonicalIsCompactAndSorted") = []
{
    auto request = fingerprintRequest();
    request.options = {{"B", "2"}, {"A", "1"}};

    const auto text =
        canonicalFingerprint(fingerprintFor(request, fingerprintEnv(), "abc"));

    check(text.find('\n') == std::string::npos);
    check(text.find("\"options\":{\"A\":\"1\",\"B\":\"2\"}") != std::string::npos);
    check(text.find("\"sourceHash\":\"abc\"") != std::string::npos);
    check(fingerprintId(fingerprintFor(request, fingerprintEnv(), "abc")).size()
          == 64);
};

auto tFpIdIgnoresRequestDetails = test("Fingerprint/idIgnoresModeRefAndConfigs") = []
{
    auto request = fingerprintRequest();
    const auto before =
        fingerprintId(fingerprintFor(request, fingerprintEnv(), "a"));

    request.mode = "prebuilt";
    request.gitRef = "v1.0";
    request.configs = {"Debug", "Release"};
    request.abiMatch = "strict";
    request.buildMissingBinaries = false;

    check(fingerprintId(fingerprintFor(request, fingerprintEnv(), "a")) == before);
    check(fingerprintId(fingerprintFor(request, fingerprintEnv(), "b")) != before);
};

auto tFpPolicies = test("Fingerprint/policies") = []
{
    const auto ninja = fingerprintWith(fingerprintEnv());
    const auto xcode = fingerprintWith(xcodeEnv());

    check(abiMatches(ninja, ninja, AbiMatch::strict));
    check(!abiMatches(ninja, xcode, AbiMatch::strict));
    check(abiMatches(ninja, xcode, AbiMatch::compatible));
    check(abiMatches(ninja, xcode, AbiMatch::any));

    auto newerMajor = xcodeEnv();
    newerMajor.cxxCompilerVersion = "18.1.0.18010040";
    const auto newer = fingerprintWith(newerMajor);

    check(!abiMatches(ninja, newer, AbiMatch::compatible));
    check(abiMatches(ninja, newer, AbiMatch::any));

    auto gnu = fingerprintEnv();
    gnu.cxxCompilerId = "GNU";
    check(!abiMatches(ninja, fingerprintWith(gnu), AbiMatch::compatible));
    check(abiMatches(ninja, fingerprintWith(gnu), AbiMatch::any));
};

auto tFpCompatibleIgnores = test("Fingerprint/compatibleIgnoresProcessor") = []
{
    auto other = fingerprintEnv();
    other.systemProcessor = "x86_64";
    other.osxArchitectures = "arm64";

    // Same platform (macos-arm64) through CMAKE_OSX_ARCHITECTURES.
    check(abiMatches(fingerprintWith(fingerprintEnv()),
                     fingerprintWith(other),
                     AbiMatch::compatible));
    check(!abiMatches(fingerprintWith(fingerprintEnv()),
                      fingerprintWith(other),
                      AbiMatch::strict));
};

auto tFpEveryPolicyChecks = test("Fingerprint/sourceOptionsPlatformAlwaysCount") = []
{
    const auto base = fingerprintWith(fingerprintEnv());
    auto otherOptions = base;
    otherOptions.options["EACP_ENABLE_TESTS"] = "ON";

    auto universal = fingerprintEnv();
    universal.osxArchitectures = "arm64;x86_64";

    for (const auto policy: {AbiMatch::strict, AbiMatch::compatible, AbiMatch::any})
    {
        check(!abiMatches(base, fingerprintWith(fingerprintEnv(), "other"), policy));
        check(!abiMatches(base, otherOptions, policy));
        check(!abiMatches(base, fingerprintWith(universal), policy));
    }
};

auto tFpCompatibleCounts = test("Fingerprint/compatibleChecksAbiFields") = []
{
    const auto base = fingerprintWith(fingerprintEnv());

    auto target = base;
    target.osxDeploymentTarget = "13.0";
    auto standard = base;
    standard.cxxStandard = "23";
    auto toolchain = base;
    toolchain.toolchainFileHash = "ff00";

    for (const auto& other: {target, standard, toolchain})
    {
        check(!abiMatches(base, other, AbiMatch::compatible));
        check(abiMatches(base, other, AbiMatch::any));
    }
};

auto tFpParse = test("Fingerprint/parseAbiMatch") = []
{
    check(parseAbiMatch("strict") == AbiMatch::strict);
    check(parseAbiMatch("compatible") == AbiMatch::compatible);
    check(parseAbiMatch("any") == AbiMatch::any);
    check(!parseAbiMatch("Compatible").has_value());
    check(!parseAbiMatch("").has_value());
};

auto tFpNote = test("Fingerprint/note") = []
{
    const auto ninja = fingerprintWith(fingerprintEnv());

    check(!abiNote(ninja, ninja).has_value());
    check(abiNote(ninja, fingerprintWith(xcodeEnv()))
          == "built with AppleClang 17.0.0.17000013, current 17.0.0.17000319");

    auto samePath = xcodeEnv();
    samePath.cxxCompilerVersion = "17.0.0.17000013";
    check(abiNote(ninja, fingerprintWith(samePath))
          == "built with /usr/bin/c++, current " + samePath.cxxCompiler);

    auto gnu = fingerprintEnv();
    gnu.cxxCompilerId = "GNU";
    gnu.cxxCompilerVersion = "13.2.0";
    check(abiNote(ninja, fingerprintWith(gnu))
          == "built with AppleClang 17.0.0.17000013, current GNU 13.2.0");

    auto processor = fingerprintEnv();
    processor.systemProcessor = "x86_64";
    processor.osxArchitectures = "arm64";
    check(abiNote(ninja, fingerprintWith(processor))
          == "built with a different systemProcessor, osxArchitectures");
};

auto tFpDepsChangeId = test("Fingerprint/depsChangeTheId") = []
{
    auto request = fingerprintRequest();
    const auto without = fingerprintFor(request, fingerprintEnv(), "abc");

    request.deps = {{"fmt", "aaaa"}};
    const auto with = fingerprintFor(request, fingerprintEnv(), "abc");

    request.deps = {{"fmt", "bbbb"}};
    const auto changed = fingerprintFor(request, fingerprintEnv(), "abc");

    check(with.deps.at("fmt") == "aaaa");
    check(fingerprintId(without) != fingerprintId(with));
    check(fingerprintId(with) != fingerprintId(changed));
    check(canonicalFingerprint(with).find("\"deps\":{\"fmt\":\"aaaa\"}")
          != std::string::npos);
};

auto tFpDepsEveryPolicy = test("Fingerprint/depsCountInEveryPolicy") = []
{
    auto request = fingerprintRequest();
    request.deps = {{"fmt", "aaaa"}, {"zlib", "cccc"}};
    const auto base = fingerprintFor(request, fingerprintEnv(), "abc");

    auto otherHash = base;
    otherHash.deps["fmt"] = "bbbb";
    auto fewer = base;
    fewer.deps.erase("zlib");

    for (const auto policy: {AbiMatch::strict, AbiMatch::compatible, AbiMatch::any})
    {
        check(abiMatches(base, base, policy));
        check(!abiMatches(base, otherHash, policy));
        check(!abiMatches(base, fewer, policy));
    }

    // Same deps, other compiler path: still compatible.
    check(abiMatches(
        base, fingerprintFor(request, xcodeEnv(), "abc"), AbiMatch::compatible));
    check(abiNote(base, otherHash) == "built with a different deps");
};
