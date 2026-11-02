#include <epm/Cache/BinaryCache.h>

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace epm;

namespace
{
Env keyEnv()
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

Request keyRequest()
{
    auto request = Request {};
    request.name = "eacp";
    request.gitUrl = "https://github.com/eyalamirmusic/eacp.git";
    request.gitRef = "main";
    return request;
}

std::string
    keyWith(std::initializer_list<std::pair<std::string, std::string>> options)
{
    auto request = keyRequest();

    for (const auto& [name, value]: options)
        request.options[name] = value;

    return binaryKey(makeManifest(request, keyEnv(), "abc123"));
}
} // namespace

auto tKeyOptionOrder = test("BinaryCache/key/optionOrderDoesNotMatter") = []
{
    const auto forward = keyWith({{"EACP_BUILD_GRAPHICS", "OFF"},
                                  {"EACP_ENABLE_TESTS", "OFF"},
                                  {"A_FIRST", "ON"}});
    const auto backward = keyWith({{"A_FIRST", "ON"},
                                   {"EACP_ENABLE_TESTS", "OFF"},
                                   {"EACP_BUILD_GRAPHICS", "OFF"}});

    check(forward == backward);
    check(forward.size() == 64);
};

auto tKeyOptionValue = test("BinaryCache/key/changedOptionChangesKey") = []
{
    check(keyWith({{"EACP_ENABLE_TESTS", "OFF"}})
          != keyWith({{"EACP_ENABLE_TESTS", "ON"}}));
    check(keyWith({}) != keyWith({{"EACP_ENABLE_TESTS", "OFF"}}));
};

auto tKeyToolchain = test("BinaryCache/key/toolchainChangesKey") = []
{
    const auto request = keyRequest();
    auto other = keyEnv();
    other.cxxCompilerVersion = "18.0.0";

    check(binaryKey(makeManifest(request, keyEnv(), "abc123"))
          != binaryKey(makeManifest(request, other, "abc123")));
    check(binaryKey(makeManifest(request, keyEnv(), "abc123"))
          != binaryKey(makeManifest(request, keyEnv(), "def456")));
};

auto tKeyIgnoresMode = test("BinaryCache/key/ignoresModeAndRef") = []
{
    auto request = keyRequest();
    const auto before = binaryKey(makeManifest(request, keyEnv(), "abc123"));

    request.mode = "prebuilt";
    request.gitRef = "v1.0";
    request.buildMissingBinaries = false;

    check(binaryKey(makeManifest(request, keyEnv(), "abc123")) == before);
};

auto tCanonicalForm = test("BinaryCache/key/canonicalFormIsCompactSorted") = []
{
    auto request = keyRequest();
    request.options = {{"B", "2"}, {"A", "1"}};

    const auto text = canonicalManifest(makeManifest(request, keyEnv(), "abc123"));

    check(text.find('\n') == std::string::npos);
    check(text.find("\"options\":{\"A\":\"1\",\"B\":\"2\"}") != std::string::npos);
    check(text.find("\"configs\":[\"Debug\",\"Release\"]") != std::string::npos);
};
