#include <epm/Build/Configs.h>

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace epm;

using Configs = EA::Vector<std::string>;

auto tCfgBuildConfig = test("Configs/buildConfigFor") = []
{
    check(buildConfigFor("Debug") == "Debug");
    check(buildConfigFor("Release") == "Release");
    check(buildConfigFor("RelWithDebInfo") == "Release");
    check(buildConfigFor("MinSizeRel") == "Release");
    check(buildConfigFor("debug") == "Debug");
    check(!buildConfigFor("Profile").has_value());
    check(!buildConfigFor("").has_value());
};

auto tCfgCanonical = test("Configs/canonicalOrder") = []
{
    check(canonicalConfigs({"Release", "Debug", "Release"})
          == Configs {"Debug", "Release"});
    check(canonicalConfigs({"MinSizeRel", "Custom", "RelWithDebInfo", "Debug"})
          == Configs {"Debug", "RelWithDebInfo", "MinSizeRel", "Custom"});
    check(buildConfigsFor({"RelWithDebInfo", "Debug", "MinSizeRel"})
          == Configs {"Debug", "Release"});
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

auto tCfgSelectorNoRelease = test("Configs/selector/noReleaseFallsBackToFirst") = []
{
    check(configSelector({"Debug", "RelWithDebInfo"}, "T")
          == "$<IF:$<CONFIG:RelWithDebInfo>,epm_relwithdebinfo::T,epm_debug::T>");
};
