#include <epm/Build/ChildBuild.h>
#include <epm/Emit/Response.h>
#include <epm/Resolve/Resolve.h>

#include <eacp/Core/Platform/Platform.h>

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace epm;

namespace
{
Request modeRequest(std::string mode, bool buildMissing = true)
{
    auto request = Request {};
    request.name = "eacp";
    request.gitUrl = "https://github.com/eyalamirmusic/eacp.git";
    request.gitRef = "main";
    request.mode = std::move(mode);
    request.buildMissingBinaries = buildMissing;
    return request;
}

Env generatorEnv(std::string generator, bool multiConfig)
{
    auto env = Env {};
    env.generator = std::move(generator);
    env.isMultiConfig = multiConfig;
    env.makeProgram = "/opt/tools/ninja";
    return env;
}

ChildBuild sampleChildBuild()
{
    auto build = ChildBuild {};
    build.request = modeRequest("auto");
    build.request.options = {{"EACP_ENABLE_TESTS", "OFF"}};
    build.env = generatorEnv("Ninja", false);
    build.env.cmake = "/usr/bin/cmake";
    build.env.cxxCompiler = "/usr/bin/c++";
    build.env.osxDeploymentTarget = "11.0";
    build.generator = {"Ninja", "/opt/tools/ninja"};
    build.sourceDirectory = "/cache/src/eacp/abc";
    build.buildDirectory = "/cache/build/eacp/def/Release";
    build.prefix = "/cache/bin/eacp/def";
    build.cpmCache = "/cache/cpm";
    build.config = "Release";
    return build;
}
} // namespace

auto tPrebuiltTargets = test("Response/prebuiltTargets") = []
{
    const auto text = prebuiltTargetsText("/c/bin/eacp/9a8b7c6d5e4f");

    check(text.find("include(\"/c/bin/eacp/9a8b7c6d5e4f/epm-targets.cmake\")")
          != std::string::npos);
    check(text.find("IMPORTED_GLOBAL TRUE") != std::string::npos);
};

auto tResponseQuoting = test("Response/quoting") = []
{ check(cmakeQuoted(R"(a "b" $c \d)") == R"("a \"b\" \$c \\d")"); };

auto tDecideSource = test("Resolve/decide/source") = []
{
    check(decide(modeRequest("source"), true) == Decision::useSource);
    check(decide(modeRequest("source"), false) == Decision::useSource);
};

auto tDecideAuto = test("Resolve/decide/auto") = []
{
    check(decide(modeRequest("auto"), true) == Decision::useBinary);
    check(decide(modeRequest("auto"), false) == Decision::buildBinary);
    check(decide(modeRequest("auto", false), false) == Decision::useSource);
    check(decide(modeRequest("auto", false), true) == Decision::useBinary);
};

auto tDecidePrebuilt = test("Resolve/decide/prebuilt") = []
{
    check(decide(modeRequest("prebuilt"), true) == Decision::useBinary);
    check(decide(modeRequest("prebuilt", false), false) == Decision::buildBinary);
};

auto tRequestErrors = test("Resolve/requestErrors") = []
{
    check(!requestError(modeRequest("auto")).has_value());
    check(requestError(modeRequest("sometimes")).has_value());

    // An archive has no ref, so only the URL is required.
    auto noUrl = modeRequest("auto");
    noUrl.gitUrl.clear();
    check(requestError(noUrl).has_value());
};

auto tGeneratorSingleConfig = test("ChildBuild/generator/consumersSingleConfig") = []
{
    const auto generator = chooseGenerator(generatorEnv("Unix Makefiles", false),
                                           eacp::FilePath {"/x/ninja"});

    check(generator.name == "Unix Makefiles");
    check(generator.makeProgram == "/opt/tools/ninja");
};

auto tGeneratorMultiConfig =
    test("ChildBuild/generator/multiConfigPrefersNinja") = []
{
    const auto withNinja =
        chooseGenerator(generatorEnv("Xcode", true), eacp::FilePath {"/x/ninja"});

    check(withNinja.name == "Ninja");
    check(withNinja.makeProgram == "/x/ninja");

    const auto withoutNinja = chooseGenerator(generatorEnv("Xcode", true), {});

    // Windows leaves -G out for CMake's own default.
    check(withoutNinja.name
          == (eacp::Platform::isWindows() ? "" : "Unix Makefiles"));
    check(withoutNinja.makeProgram.empty());
};

auto tConfigureArguments = test("ChildBuild/configureArguments") = []
{
    const auto arguments = configureArguments(sampleChildBuild());
    const auto has = [&](const std::string& argument)
    { return arguments.contains(argument); };

    check(arguments[0] == "-S" && arguments[1] == "/cache/src/eacp/abc");
    check(arguments[2] == "-B" && arguments[3] == "/cache/build/eacp/def/Release");
    check(arguments[4] == "-G" && arguments[5] == "Ninja");
    check(has("-DCMAKE_MAKE_PROGRAM=/opt/tools/ninja"));
    check(has("-DCMAKE_BUILD_TYPE=Release"));
    check(has("-DCMAKE_CXX_COMPILER=/usr/bin/c++"));
    check(has("-DCMAKE_OSX_DEPLOYMENT_TARGET=11.0"));
    check(has("-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=/cache/bin/eacp/def/lib/Release"));
    check(has("-DCPM_SOURCE_CACHE=/cache/cpm"));
    check(has("-DCMAKE_PROJECT_TOP_LEVEL_INCLUDES=/cache/build/eacp/def/Release/"
              "epm-export.cmake"));
    check(has("-DEPM_EXPORT_FILE=/cache/bin/eacp/def/export-Release.cmake"));
    check(has("-DEPM_EXPORT_NAMESPACE=epm_release::"));
    check(has("-DEACP_ENABLE_TESTS=OFF"));

    for (const auto& argument: arguments)
    {
        check(argument.find("CMAKE_C_COMPILER") == std::string::npos);
        check(argument.find("CMAKE_OSX_ARCHITECTURES") == std::string::npos);
        check(argument.find("CMAKE_TOOLCHAIN_FILE") == std::string::npos);
        check(argument.find("CMAKE_CXX_STANDARD") == std::string::npos);
    }
};

auto tEmbeddedScript = test("ChildBuild/embeddedExportScript") = []
{
    const auto script = embeddedExportScript();

    check(script.find("epm_export_all_targets") != std::string_view::npos);
    check(script.find("${EPM_EXPORT_FILE}.names") != std::string_view::npos);
};
