#include <epm/Cache/Fingerprint.h>
#include <epm/Spec/Platform.h>
#include <epm/Util/Text.h>

#include <eacp/Core/Utils/Environment.h>
#include <eacp/Core/Utils/TemporaryFile.h>

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
    env.sdkVersion = "15.2";
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

AbiFingerprint fingerprintWith(const Env& env, const std::string& sourceId = "abc")
{
    return fingerprintFor(fingerprintRequest(), env, sourceId);
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
    check(fingerprint.sdkVersion == "15.2");
    check(fingerprint.schema == 4);
    check(fingerprint.sourceId == "abc");
    check(fingerprint.toolchainFileHash.empty());
    check(fingerprint.envFlags.empty());
};

auto tFpMajor = test("Fingerprint/majorVersion") = []
{
    check(majorVersion("17.0.0.17000013") == "17");
    check(majorVersion("19.38.33130") == "19");
    check(majorVersion("13") == "13");
    check(majorVersion("").empty());
};

// No machine path is part of the key: another machine's compiler and
// toolchain file count by identity and contents, not by where they live.
auto tFpNoPaths = test("Fingerprint/idIgnoresMachinePaths") = []
{
    const auto base = fingerprintWith(fingerprintEnv());

    auto elsewhere = fingerprintEnv();
    elsewhere.cxxCompiler = "/opt/ci/toolchain/bin/c++";
    elsewhere.cCompiler = "/opt/ci/toolchain/bin/cc";
    elsewhere.cmake = "/opt/ci/cmake/bin/cmake";
    elsewhere.epmExecutable = "/opt/ci/epm/bin/epm";
    elsewhere.osxSysroot = "/opt/ci/Xcode.app/MacOSX.sdk";
    elsewhere.osxSysrootPath = "/opt/ci/Xcode.app/MacOSX15.2.sdk";
    elsewhere.generatorInstance = "/opt/ci/VisualStudio";

    check(fingerprintId(fingerprintWith(elsewhere)) == fingerprintId(base));
    check(abiMatches(base, fingerprintWith(elsewhere), AbiMatch::strict));
    check(!abiNote(base, fingerprintWith(elsewhere)).has_value());

    const auto text = canonicalFingerprint(base);
    check(text.find("compilerPath") == std::string::npos);
    check(text.find("\"toolchainFile\"") == std::string::npos);
    check(text.find("/usr/bin/c++") == std::string::npos);
};

auto tFpToolchainContents = test("Fingerprint/toolchainFileByContents") = []
{
    auto directory = eacp::TemporaryFile {"-toolchains"};
    const auto first = directory.path() / "a" / "toolchain.cmake";
    const auto second = directory.path() / "b" / "toolchain.cmake";
    const auto changed = directory.path() / "c" / "toolchain.cmake";

    check(writeTextFile(first, "set(CMAKE_SYSTEM_NAME Linux)\n"));
    check(writeTextFile(second, "set(CMAKE_SYSTEM_NAME Linux)\n"));
    check(writeTextFile(changed, "set(CMAKE_SYSTEM_NAME Android)\n"));

    const auto withToolchain = [&](const eacp::FilePath& file)
    {
        auto env = fingerprintEnv();
        env.toolchainFile = file.str();
        return makeFingerprint(fingerprintRequest(), env, "abc");
    };

    const auto a = withToolchain(first);

    check(!a.toolchainFileHash.empty());
    check(fingerprintId(withToolchain(second)) == fingerprintId(a));
    check(fingerprintId(withToolchain(changed)) != fingerprintId(a));
    check(!abiMatches(a, withToolchain(changed), AbiMatch::compatible));
};

// Strict wants the same SDK, compatible the same major version (an SDK
// update within it keeps the entries), any ignores it.
auto tFpSdkVersion = test("Fingerprint/sdkVersion") = []
{
    const auto base = fingerprintWith(fingerprintEnv());

    auto minor = fingerprintEnv();
    minor.sdkVersion = "15.4";
    auto major = fingerprintEnv();
    major.sdkVersion = "26.0";

    check(fingerprintId(fingerprintWith(minor)) != fingerprintId(base));
    check(!abiMatches(base, fingerprintWith(minor), AbiMatch::strict));
    check(abiMatches(base, fingerprintWith(minor), AbiMatch::compatible));
    check(!abiMatches(base, fingerprintWith(major), AbiMatch::compatible));
    check(abiMatches(base, fingerprintWith(major), AbiMatch::any));
    check(abiNote(base, fingerprintWith(minor))
          == "built with a different sdkVersion");
};

// The C compiler counts only when the project enabled C with another compiler
// than its C++ one: the same toolchain, or a C driver epm.cmake found beside
// the C++ one, keeps the key a C++-only project gets.
auto tFpCCompiler = test("Fingerprint/cCompiler") = []
{
    const auto base = fingerprintWith(fingerprintEnv());

    auto sameToolchain = fingerprintEnv();
    sameToolchain.cCompiler = "/usr/bin/cc";
    sameToolchain.cCompilerId = "AppleClang";
    sameToolchain.cCompilerVersion = "17.0.0.17000013";
    check(fingerprintId(fingerprintWith(sameToolchain)) == fingerprintId(base));

    const auto withC = [](std::string id, std::string version)
    {
        auto env = fingerprintEnv();
        env.cCompilerId = std::move(id);
        env.cCompilerVersion = std::move(version);
        return fingerprintWith(env);
    };

    const auto gcc = withC("GNU", "14.2.0");
    check(gcc.cCompilerId == "GNU" && gcc.cCompilerVersion == "14.2.0");
    check(fingerprintId(gcc) != fingerprintId(base));
    check(!abiMatches(base, gcc, AbiMatch::compatible));
    check(abiMatches(base, gcc, AbiMatch::any));
    check(abiMatches(gcc, withC("GNU", "14.3.0"), AbiMatch::compatible));
    check(!abiMatches(gcc, withC("GNU", "15.1.0"), AbiMatch::compatible));
    check(abiNote(base, gcc)
          == "built with a different cCompilerId, cCompilerVersion");

    // Another version of the same compiler counts too.
    check(withC("AppleClang", "16.0.0.16000026").cCompilerVersion
          == "16.0.0.16000026");
};

// The flag variables the child build inherits: compared by strict and
// compatible (any of them can change the ABI: -stdlib, -D_GLIBCXX_DEBUG,
// -fno-exceptions), ignored by any.
auto tFpEnvFlags = test("Fingerprint/envFlags") = []
{
    const auto base = fingerprintWith(fingerprintEnv());
    auto flagged = base;
    flagged.envFlags = {{"CXXFLAGS", "-D_GLIBCXX_DEBUG"}};

    check(fingerprintId(flagged) != fingerprintId(base));
    check(!abiMatches(base, flagged, AbiMatch::strict));
    check(!abiMatches(base, flagged, AbiMatch::compatible));
    check(abiMatches(base, flagged, AbiMatch::any));
    check(abiNote(base, flagged) == "built with a different envFlags");
};

auto tFpEnvFlagsRead = test("Fingerprint/envFlagsFromTheEnvironment") = []
{
    auto saved = std::map<std::string, std::optional<std::string>> {};

    for (const auto name: inheritedFlagVariables)
    {
        saved[std::string {name}] = eacp::getEnv(name);
        eacp::unsetEnv(name);
    }

    const auto plain = makeFingerprint(fingerprintRequest(), fingerprintEnv(), "a");

    eacp::setEnv("CXXFLAGS", "-fno-exceptions");
    eacp::setEnv("LDFLAGS", "");
    eacp::setEnv("UNRELATED_FLAGS", "-O3");
    const auto flagged =
        makeFingerprint(fingerprintRequest(), fingerprintEnv(), "a");

    eacp::unsetEnv("UNRELATED_FLAGS");

    for (const auto& [name, value]: saved)
    {
        if (value)
            eacp::setEnv(name, *value);
        else
            eacp::unsetEnv(name);
    }

    check(plain.envFlags.empty());
    check(flagged.envFlags
          == std::map<std::string, std::string> {{"CXXFLAGS", "-fno-exceptions"}});
    check(fingerprintId(plain) != fingerprintId(flagged));
};

auto tFpCanonical = test("Fingerprint/canonicalIsCompactAndSorted") = []
{
    auto request = fingerprintRequest();
    request.options = {{"B", "2"}, {"A", "1"}};

    const auto text =
        canonicalFingerprint(fingerprintFor(request, fingerprintEnv(), "abc"));

    check(text.find('\n') == std::string::npos);
    check(text.find("\"options\":{\"A\":\"1\",\"B\":\"2\"}") != std::string::npos);
    check(text.find("\"sourceId\":\"abc\"") != std::string::npos);
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

// On the MSVC ABI the runtime library is part of every policy: archives of
// one never link with objects of another. Elsewhere it is empty.
auto tFpMsvcRuntime = test("Fingerprint/msvcRuntime") = []
{
    auto env = fingerprintEnv();
    env.systemName = "Windows";
    env.systemProcessor = "ARM64";
    env.cxxCompilerId = "Clang";
    env.cxxCompilerFrontend = "MSVC";

    const auto base = fingerprintWith(env);
    check(base.schema == 4);
    check(base.msvcRuntime == staticMsvcRuntime);
    check(fingerprintWith(fingerprintEnv()).msvcRuntime.empty());

    auto dll = env;
    dll.msvcRuntime = "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL";
    const auto dynamic = fingerprintWith(dll);
    check(fingerprintId(dynamic) != fingerprintId(base));

    for (const auto policy: {AbiMatch::strict, AbiMatch::compatible, AbiMatch::any})
        check(!abiMatches(base, dynamic, policy));

    check(abiNote(base, dynamic) == "built with a different msvcRuntime");
};

// On Android the ABI is in the platform name; the API level and STL are
// compared under strict and compatible, not under any, and so are the
// dependency keys a dependent records. The NDK counts by its revision
// (sdkVersion), never by its directory.
auto tFpAndroid = test("Fingerprint/android") = []
{
    auto env = fingerprintEnv();
    env.systemName = "Android";
    env.systemProcessor = "aarch64";
    env.cxxCompilerId = "Clang";
    env.osxDeploymentTarget.clear();
    env.sdkVersion = "27.0.12077973";
    env.androidAbi = "arm64-v8a";
    env.androidApi = "26";
    env.androidNdk = "/sdk/ndk/27.0.12077973";
    env.androidStl = "c++_static";

    const auto base = fingerprintWith(env);
    check(base.platform == "android-arm64-v8a");
    check(base.androidApi == "26");
    check(base.androidStl == "c++_static");
    check(fingerprintWith(fingerprintEnv()).androidApi.empty());
    check(canonicalFingerprint(base).find("/sdk/ndk") == std::string::npos);

    auto api = env;
    api.androidApi = "28";
    auto stl = env;
    stl.androidStl = "c++_shared";

    for (const auto& other: {fingerprintWith(api), fingerprintWith(stl)})
    {
        check(fingerprintId(other) != fingerprintId(base));
        check(!abiMatches(base, other, AbiMatch::compatible));
        check(abiMatches(base, other, AbiMatch::any));
        check(abiKey(other, AbiMatch::compatible)
              != abiKey(base, AbiMatch::compatible));
        check(abiKey(other, AbiMatch::any) == abiKey(base, AbiMatch::any));
    }

    check(abiNote(base, fingerprintWith(api))
          == "built with a different androidApi");

    // The same NDK elsewhere is the same key; another revision of the same
    // major version only compatible accepts, another major version none but
    // any.
    auto elsewhere = env;
    elsewhere.androidNdk = "/opt/ci/android-ndk-r27";
    auto patched = env;
    patched.sdkVersion = "27.2.12479018";
    auto newer = env;
    newer.sdkVersion = "28.0.13004108";

    check(fingerprintId(fingerprintWith(elsewhere)) == fingerprintId(base));
    check(!abiMatches(base, fingerprintWith(patched), AbiMatch::strict));
    check(abiMatches(base, fingerprintWith(patched), AbiMatch::compatible));
    check(abiKey(fingerprintWith(patched), AbiMatch::compatible)
          == abiKey(base, AbiMatch::compatible));
    check(!abiMatches(base, fingerprintWith(newer), AbiMatch::compatible));
    check(abiMatches(base, fingerprintWith(newer), AbiMatch::any));
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

    // Another path to the same compiler version is the same toolchain.
    auto samePath = xcodeEnv();
    samePath.cxxCompilerVersion = "17.0.0.17000013";
    check(!abiNote(ninja, fingerprintWith(samePath)).has_value());

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

    // Same deps, other compiler build: still compatible.
    check(abiMatches(
        base, fingerprintFor(request, xcodeEnv(), "abc"), AbiMatch::compatible));
    check(abiNote(base, otherHash) == "built with a different deps");
};
