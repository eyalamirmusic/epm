#include <epm/Build/Configs.h>
#include <epm/Spec/Platform.h>

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace epm;

using Configs = eacp::Vector<std::string>;

auto tCfgBuildConfig = test("Configs/buildConfigFor") = []
{
    check(buildConfigFor("Debug") == "Debug");
    check(buildConfigFor("Release") == "Release");
    check(buildConfigFor("RelWithDebInfo") == "RelWithDebInfo");
    check(buildConfigFor("relwithdebinfo") == "RelWithDebInfo");
    check(buildConfigFor("MinSizeRel") == "Release");
    check(buildConfigFor("debug") == "Debug");
    check(!buildConfigFor("Profile").has_value());
    check(!buildConfigFor("").has_value());

    check(isDebugConfig("debug"));
    check(!isDebugConfig("RelWithDebInfo"));
    check(!isDebugConfig(""));
};

auto tCfgCanonical = test("Configs/canonicalOrder") = []
{
    check(canonicalConfigs({"Release", "Debug", "Release"})
          == Configs {"Debug", "Release"});
    check(canonicalConfigs({"MinSizeRel", "Custom", "RelWithDebInfo", "Debug"})
          == Configs {"Debug", "RelWithDebInfo", "MinSizeRel", "Custom"});
    check(buildConfigsFor({"RelWithDebInfo", "Debug", "MinSizeRel"})
          == Configs {"Debug", "Release", "RelWithDebInfo"});
    check(buildConfigsFor({"MinSizeRel", "Release"}) == Configs {"Release"});
};

auto tCfgMissing = test("Configs/missing") = []
{
    check(missingConfigs({"Release"}, {"Debug"}) == Configs {"Debug"});
    check(missingConfigs({"Release"}, {"Release"}).empty());
    check(missingConfigs({}, {"Release", "Debug"}) == Configs {"Debug", "Release"});
    check(missingConfigs({"Debug", "Release"}, {"Debug", "Release"}).empty());
    check(missingConfigs({"Debug"}, {"Debug", "Release"}) == Configs {"Release"});
};

auto tCfgMap = test("Configs/mapConsumerConfig") = []
{
    const auto both = Configs {"Debug", "Release"};

    check(mapConsumerConfig("Debug", both) == "Debug");
    check(mapConsumerConfig("debug", both) == "Debug");
    check(mapConsumerConfig("RelWithDebInfo", both) == "Release");
    check(mapConsumerConfig("Profile", both) == "Release");
    check(mapConsumerConfig("Debug", {"Release"}) == "Release");
    check(mapConsumerConfig("Release", {"Debug"}) == "Debug");
    check(mapConsumerConfig("RelWithDebInfo", {"Debug", "Release", "RelWithDebInfo"})
          == "RelWithDebInfo");
    check(mapConsumerConfig("Debug", {"RelWithDebInfo"}) == "RelWithDebInfo");
    check(mapConsumerConfig("MinSizeRel", {"Debug", "RelWithDebInfo"})
          == "RelWithDebInfo");
    check(mapConsumerConfig("", {"Debug", "RelWithDebInfo"}) == "RelWithDebInfo");
};

auto tCfgNamespace = test("Configs/exportNamespace") = []
{
    check(exportNamespace("Debug") == "epm_debug::");
    check(exportNamespace("RelWithDebInfo") == "epm_relwithdebinfo::");
};

auto tCfgSelectorRelease = test("Configs/selector/releaseOnly") = []
{ check(configSelector({"Release"}, "T") == "epm_release::T"); };

auto tCfgSelectorDebug = test("Configs/selector/debugOnly") = []
{ check(configSelector({"Debug"}, "T") == "epm_debug::T"); };

auto tCfgSelectorBoth = test("Configs/selector/debugAndRelease") = []
{
    const auto expected = "$<IF:$<CONFIG:Debug>,epm_debug::T,epm_release::T>";

    check(configSelector({"Debug", "Release"}, "T") == expected);
    check(configSelector({"Release", "Debug"}, "T") == expected);
};

auto tCfgSelectorThree = test("Configs/selector/withRelWithDebInfo") = []
{
    check(configSelector({"Debug", "Release", "RelWithDebInfo"}, "${name}")
          == "$<IF:$<CONFIG:Debug>,epm_debug::${name},"
             "$<IF:$<CONFIG:RelWithDebInfo>,epm_relwithdebinfo::${name},"
             "epm_release::${name}>>");
};

auto tCfgSelectorNoRelease =
    test("Configs/selector/noReleaseFallsBackToRelWithDebInfo") = []
{
    check(configSelector({"Debug", "RelWithDebInfo"}, "T")
          == "$<IF:$<CONFIG:Debug>,epm_debug::T,epm_relwithdebinfo::T>");
    check(configSelector({"Debug", "MinSizeRel"}, "T")
          == "$<IF:$<CONFIG:MinSizeRel>,epm_minsizerel::T,epm_debug::T>");
};

namespace
{
Env msvcEnv(std::string buildType)
{
    auto env = Env {};
    env.cxxCompilerId = "Clang";
    env.cxxCompilerFrontend = "MSVC";
    env.buildType = std::move(buildType);
    return env;
}
} // namespace

// On the MSVC ABI a Debug build links only a Debug config, and a release
// build only a release one; elsewhere any config links.
auto tCfgLinkProblem = test("Configs/linkProblem/msvcDebugAgainstRelease") = []
{
    const auto problem = configLinkProblem(msvcEnv("Debug"), {"Release"});

    check(problem.has_value());
    check(problem.value_or("").find("a Debug build of this project cannot link its "
                                    "Release build")
          == 0);
    check(problem.value_or("").find("releaseIfPossible()") != std::string::npos);

    check(!configLinkProblem(msvcEnv("Debug"), {"Debug"}).has_value());
    check(!configLinkProblem(msvcEnv("Debug"), {"Debug", "Release"}).has_value());
    check(!configLinkProblem(msvcEnv("Release"), {"Release"}).has_value());
    check(!configLinkProblem(msvcEnv("RelWithDebInfo"), {"Release"}).has_value());
    check(!configLinkProblem(msvcEnv(""), {"RelWithDebInfo"}).has_value());
    check(configLinkProblem(msvcEnv("Release"), {"Debug"})
              .value_or("")
              .find("a Release build of this project cannot link its Debug build")
          == 0);
    check(configLinkProblem(msvcEnv(""), {"Debug"})
              .value_or("")
              .find("a Release build")
          == 0);

    auto gnu = msvcEnv("Debug");
    gnu.cxxCompilerFrontend = "GNU";
    check(!configLinkProblem(gnu, {"Release"}).has_value());

    auto cl = msvcEnv("Debug");
    cl.cxxCompilerId = "MSVC";
    cl.cxxCompilerFrontend = {};
    check(configLinkProblem(cl, {"Release"}).has_value());
};

// A Debug build given the release runtime links release archives, and every
// configuration of a multi-config generator must link.
auto tCfgLinkProblemRuntime = test("Configs/linkProblem/runtimeAndMultiConfig") = []
{
    auto releaseRuntime = msvcEnv("Debug");
    releaseRuntime.msvcRuntime = "MultiThreaded";
    check(!configLinkProblem(releaseRuntime, {"Release"}).has_value());

    auto debugRuntime = msvcEnv("Debug");
    debugRuntime.msvcRuntime = "MultiThreadedDebugDLL";
    check(configLinkProblem(debugRuntime, {"Release"}).has_value());

    auto multi = msvcEnv("");
    multi.isMultiConfig = true;
    multi.configurationTypes = {"Debug", "Release", "RelWithDebInfo"};
    check(!configLinkProblem(multi, {"Debug", "Release"}).has_value());
    check(configLinkProblem(multi, {"Release"})
              .value_or("")
              .find("a Debug build of this project cannot link its Release build")
          == 0);
    check(configLinkProblem(multi, {"Debug"})
              .value_or("")
              .find("a Release build of this project cannot link its Debug build")
          == 0);
};
