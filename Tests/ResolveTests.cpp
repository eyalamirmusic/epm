#include <epm/Build/ChildBuild.h>
#include <epm/Emit/Response.h>
#include <epm/Resolve/Resolve.h>

#include <eacp/Core/Platform/Platform.h>
#include <eacp/Core/Utils/Files.h>

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
    request.configs = {"Release"};
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

auto tResponsePrebuilt = test("Response/prebuilt") = []
{
    const auto text = responseText({"eacp",
                                    "prebuilt",
                                    "/c/src/eacp/3f1c0a9b2d4e",
                                    "/c/bin/eacp/macos-arm64/9a8b7c6d5e4f",
                                    "macos-arm64",
                                    {"Debug", "Release"},
                                    {}});

    check(text
          == "set(epm_eacp_MODE \"prebuilt\")\n"
             "set(epm_eacp_SOURCE_DIR \"/c/src/eacp/3f1c0a9b2d4e\")\n"
             "set(epm_eacp_PREFIX \"/c/bin/eacp/macos-arm64/9a8b7c6d5e4f\")\n"
             "set(epm_eacp_TARGETS_FILE \"${epm_eacp_PREFIX}/epm-targets.cmake\")\n"
             "set(epm_eacp_PLATFORM \"macos-arm64\")\n"
             "set(epm_eacp_CONFIGS \"Debug;Release\")\n");
};

auto tResponseNote = test("Response/prebuiltWithNote") = []
{
    const auto text = responseText(
        {"eacp",
         "prebuilt",
         "/c/src/eacp/3f1c0a9b2d4e",
         "/c/bin/eacp/macos-arm64/9a8b7c6d5e4f",
         "macos-arm64",
         {"Release"},
         "built with AppleClang 17.0.0.17000013, current 17.0.0.17000319"});

    check(text.find("set(epm_eacp_CONFIGS \"Release\")\n") != std::string::npos);
    check(text.ends_with("set(epm_eacp_NOTE \"built with AppleClang "
                         "17.0.0.17000013, current 17.0.0.17000319\")\n"));
};

auto tResponseSource = test("Response/source") = []
{
    const auto text =
        responseText({"eacp", "source", "/c/src/eacp/3f1c0a9b2d4e", {}});

    check(text
          == "set(epm_eacp_MODE \"source\")\n"
             "set(epm_eacp_SOURCE_DIR \"/c/src/eacp/3f1c0a9b2d4e\")\n");
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

    auto noRef = modeRequest("auto");
    noRef.gitRef.clear();
    check(requestError(noRef).has_value());

    auto noConfigs = modeRequest("auto");
    noConfigs.configs.clear();
    check(requestError(noConfigs).has_value());

    auto mapped = modeRequest("auto");
    mapped.configs = {"Debug", "RelWithDebInfo", "MinSizeRel"};
    check(!requestError(mapped).has_value());

    auto unknownConfig = modeRequest("auto");
    unknownConfig.configs = {"Debug", "Profile"};
    check(requestError(unknownConfig).value_or("").find("'Profile'")
          != std::string::npos);

    auto unknownMatch = modeRequest("auto");
    unknownMatch.abiMatch = "loose";
    check(requestError(unknownMatch).value_or("").find("'loose'")
          != std::string::npos);
};

auto tBuildInputEpmExecutable = test("Resolve/buildInputFor/epmExecutable") = []
{
    const auto cache = CacheRoot {eacp::FilePath {"/cache"}};
    auto env = Env {};

    const auto running =
        buildInputFor(modeRequest("auto"), env, cache, SourceSnapshot {});
    check(running.epmExecutable == eacp::Files::executablePath().str());

    env.epmExecutable = "/opt/epm/bin/epm";
    const auto given =
        buildInputFor(modeRequest("auto"), env, cache, SourceSnapshot {});
    check(given.epmExecutable == "/opt/epm/bin/epm");
    check(given.env.epmExecutable == "/opt/epm/bin/epm");
};

auto tRequestSchema = test("Resolve/requestErrors/schema") = []
{
    for (const auto schema: {0, 1, 3})
    {
        auto request = modeRequest("auto");
        request.schema = schema;

        const auto error = requestError(request);

        check(error.has_value());
        check(error.value_or("").find("has schema " + std::to_string(schema)
                                      + ", but this epm reads schema 2")
              != std::string::npos);
        check(error.value_or("").find("get_epm.cmake") != std::string::npos);
    }

    check(modeRequest("auto").schema == requestSchema);
    check(requestSchema == 2);
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

namespace
{
// Whether any argument sets the target system, processor, sysroot or ABI.
bool namesTargetSystem(const eacp::Vector<std::string>& arguments)
{
    for (const auto& argument: arguments)
        for (const auto* name: {"-DCMAKE_SYSTEM_NAME=",
                                "-DCMAKE_SYSTEM_PROCESSOR=",
                                "-DCMAKE_OSX_SYSROOT=",
                                "-DCMAKE_ANDROID_ARCH_ABI="})
            if (argument.starts_with(name))
                return true;

    return false;
}
} // namespace

auto tConfigureArgumentsIos = test("ChildBuild/configureArguments/iosSimulator") = []
{
    auto build = sampleChildBuild();
    build.env.systemName = "iOS";
    build.env.crossCompiling = true;
    build.env.osxSysroot = "iphonesimulator";
    build.env.osxArchitectures = "arm64";
    build.env.osxDeploymentTarget = "15.0";
    const auto arguments = configureArguments(build);
    const auto has = [&](const std::string& argument)
    { return arguments.contains(argument); };

    check(has("-DCMAKE_SYSTEM_NAME=iOS"));
    check(has("-DCMAKE_OSX_SYSROOT=iphonesimulator"));
    check(has("-DCMAKE_OSX_ARCHITECTURES=arm64"));
    check(has("-DCMAKE_OSX_DEPLOYMENT_TARGET=15.0"));

    // CMake leaves CMAKE_SYSTEM_PROCESSOR empty for iOS.
    for (const auto& argument: arguments)
        check(!argument.starts_with("-DCMAKE_SYSTEM_PROCESSOR="));

    build.env.systemProcessor = "arm64";
    check(configureArguments(build).contains("-DCMAKE_SYSTEM_PROCESSOR=arm64"));

    // A toolchain file says the target system itself.
    build.env.toolchainFile = "/x/ios.toolchain.cmake";
    const auto withToolchain = configureArguments(build);
    check(withToolchain.contains("-DCMAKE_TOOLCHAIN_FILE=/x/ios.toolchain.cmake"));
    check(!namesTargetSystem(withToolchain));
};

auto tConfigureArgumentsHost = test("ChildBuild/configureArguments/hostSystem") = []
{
    auto build = sampleChildBuild();
    build.env.systemName = "Darwin";
    build.env.systemProcessor = "arm64";
    build.env.osxSysroot = "/Applications/Xcode.app/Contents/Developer/Platforms/"
                           "MacOSX.platform/Developer/SDKs/MacOSX.sdk";
    check(!namesTargetSystem(configureArguments(build)));

    build.env.systemName = "Linux";
    build.env.osxSysroot.clear();
    check(!namesTargetSystem(configureArguments(build)));
};

auto tConfigureArgumentsAndroid = test("ChildBuild/configureArguments/android") = []
{
    auto build = sampleChildBuild();
    build.env.systemName = "Android";
    build.env.crossCompiling = true;
    build.env.systemProcessor = "aarch64";
    build.env.androidAbi = "arm64-v8a";
    const auto arguments = configureArguments(build);

    check(arguments.contains("-DCMAKE_SYSTEM_NAME=Android"));
    check(arguments.contains("-DCMAKE_SYSTEM_PROCESSOR=aarch64"));
    check(arguments.contains("-DCMAKE_ANDROID_ARCH_ABI=arm64-v8a"));
};

auto tConfigureArgumentsNested = test("ChildBuild/configureArguments/nested") = []
{
    auto build = sampleChildBuild();
    const auto plain = configureArguments(build);

    for (const auto& argument: plain)
    {
        check(!argument.starts_with("-DEPM_NESTED"));
        check(!argument.starts_with("-DEPM_GRAPH"));
        check(!argument.starts_with("-DEPM_EXECUTABLE"));
    }

    build.dependencyGraph = "set(epm_PACKAGES \"Leaf\")\n";
    build.epmExecutable = "/opt/epm/bin/epm";
    const auto nested = configureArguments(build);
    const auto has = [&](const std::string& argument)
    { return nested.contains(argument); };

    check(nested.size() == plain.size() + 3);
    check(has("-DCMAKE_PROJECT_TOP_LEVEL_INCLUDES=/cache/build/eacp/def/Release/"
              "epm-export.cmake;/cache/build/eacp/def/Release/epm-nested.cmake"));
    check(has("-DEPM_NESTED=ON"));
    check(has("-DEPM_GRAPH=/cache/build/eacp/def/Release/epm-graph.cmake"));
    check(has("-DEPM_EXECUTABLE=/opt/epm/bin/epm"));
    check(nested == configureArguments(build));
};

auto tEmbeddedScript = test("ChildBuild/embeddedExportScript") = []
{
    const auto script = embeddedExportScript();

    check(script.find("epm_export_all_targets") != std::string_view::npos);
    check(script.find("${EPM_EXPORT_FILE}.names") != std::string_view::npos);
};

auto tEmbeddedNestedScript = test("ChildBuild/embeddedNestedScript") = []
{
    const auto script = embeddedNestedScript();

    check(script.find("function(epm_provide_dependency") != std::string_view::npos);
    check(script.ends_with("epm_provider_register(\"${EPM_GRAPH}\")\n"));
};
