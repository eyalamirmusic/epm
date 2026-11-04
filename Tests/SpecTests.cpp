#include <epm/Spec/Env.h>
#include <epm/Spec/JsonFile.h>
#include <epm/Spec/Request.h>

#include <eacp/Core/Utils/TemporaryFile.h>

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace epm;

namespace
{
// Verbatim from Docs/PoC.md §2.
constexpr auto envFixture = R"({ "schema": 1,
  "cmake": "/path/to/cmake", "cmakeVersion": "3.31.6", "generator": "Ninja",
  "isMultiConfig": false,
  "buildType": "Debug", "configurationTypes": [],
  "cCompiler": "/usr/bin/cc", "cxxCompiler": "/usr/bin/c++",
  "cxxCompilerId": "AppleClang", "cxxCompilerVersion": "17.0.0.17000013",
  "systemName": "Darwin", "systemProcessor": "arm64",
  "osxArchitectures": "", "osxDeploymentTarget": "11.0",
  "osxSysroot": "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk",
  "androidAbi": "",
  "cxxStandard": "20",
  "toolchainFile": "" })";

constexpr auto requestFixture = R"({ "schema": 2,
  "name": "eacp",
  "gitUrl": "https://github.com/eyalamirmusic/eacp.git",
  "gitRef": "main",
  "mode": "auto",
  "options": { "EACP_BUILD_GRAPHICS": "OFF", "EACP_ENABLE_TESTS": "OFF" },
  "buildMissingBinaries": true,
  "configs": ["Debug"],
  "abiMatch": "compatible" })";

void writeText(const eacp::FilePath& path, std::string_view text)
{
    eacp::Files::writeFile(
        path,
        eacp::Span<const std::uint8_t> {
            reinterpret_cast<const std::uint8_t*>(text.data()), text.size()});
}
} // namespace

auto tEnvParses = test("Spec/env/parsesPoCFixture") = []
{
    auto file = eacp::TemporaryFile {".json"};
    writeText(file.path(), envFixture);

    const auto env = loadJsonFile<Env>(file.path());

    check(env.has_value());
    check(env->cmakeVersion == "3.31.6");
    check(env->generator == "Ninja");
    check(!env->isMultiConfig);
    check(env->configurationTypes.empty());
    check(env->cxxCompilerId == "AppleClang");
    check(env->osxDeploymentTarget == "11.0");
    check(env->osxSysroot == "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk");
    check(env->androidAbi.empty());
    check(env->cxxStandard == "20");
};

auto tEnvRoundTrip = test("Spec/env/roundTrip") = []
{
    auto env = Env {};
    env.generator = "Ninja Multi-Config";
    env.isMultiConfig = true;
    env.configurationTypes = {"Debug", "Release"};
    env.cxxCompiler = "/usr/bin/c++";

    auto file = eacp::TemporaryFile {".json"};
    check(saveJsonFile(file.path(), env));

    const auto loaded = loadJsonFile<Env>(file.path());

    check(loaded.has_value());
    check(loaded->generator == env.generator);
    check(loaded->isMultiConfig);
    check(loaded->configurationTypes.size() == 2);
    check(loaded->configurationTypes[1] == "Release");
    check(loaded->cxxCompiler == env.cxxCompiler);
};

auto tRequestParses = test("Spec/request/parsesPoCFixture") = []
{
    auto file = eacp::TemporaryFile {".json"};
    writeText(file.path(), requestFixture);

    const auto request = loadJsonFile<Request>(file.path());

    check(request.has_value());
    check(request->name == "eacp");
    check(request->gitUrl == "https://github.com/eyalamirmusic/eacp.git");
    check(request->gitRef == "main");
    check(request->mode == "auto");
    check(request->options.size() == 2);
    check(request->options.at("EACP_BUILD_GRAPHICS") == "OFF");
    check(request->buildMissingBinaries);
    check(request->schema == 2);
    check(request->configs.size() == 1 && request->configs[0] == "Debug");
    check(request->abiMatch == "compatible");
};

auto tRequestRoundTrip = test("Spec/request/roundTrip") = []
{
    auto request = Request {};
    request.name = "eacp";
    request.gitUrl = "https://github.com/eyalamirmusic/eacp.git";
    request.gitRef = "v1.0";
    request.mode = "source";
    request.options = {{"B", "ON"}, {"A", "OFF"}};
    request.buildMissingBinaries = false;

    auto file = eacp::TemporaryFile {".json"};
    check(saveJsonFile(file.path(), request));

    const auto text = eacp::Files::readFile(file.path());
    check(text.find("\"gitUrl\"") != std::string::npos);
    check(text.find("\"buildMissingBinaries\"") != std::string::npos);

    const auto loaded = loadJsonFile<Request>(file.path());

    check(loaded.has_value());
    check(loaded->mode == "source");
    check(loaded->gitRef == "v1.0");
    check(loaded->options == request.options);
    check(!loaded->buildMissingBinaries);
};

auto tSpecMissingFile = test("Spec/load/missingFile") = []
{
    auto file = eacp::TemporaryFile {".json"};
    check(!loadJsonFile<Request>(file.path()).has_value());
};

auto tSpecMalformed = test("Spec/load/malformed") = []
{
    auto file = eacp::TemporaryFile {".json"};
    writeText(file.path(), "{ not json");
    check(!loadJsonFile<Request>(file.path()).has_value());
};
