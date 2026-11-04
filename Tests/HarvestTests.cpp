#include <epm/Build/Harvest.h>
#include <epm/Util/Text.h>

#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/TemporaryFile.h>

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace epm;

namespace
{
constexpr auto harvestBuildDir = "/cache/build/eacp/9a8b7c6d5e4f/Debug";
constexpr auto harvestGenDir = "/cache/bin/eacp/9a8b7c6d5e4f/gen/Debug";

// Excerpts of a real export-Debug.cmake from eacp, with the paths shortened.
constexpr auto exportFixture = R"(
set_target_properties(epm_debug::Miro PROPERTIES
  INTERFACE_INCLUDE_DIRECTORIES "/cache/cpm/miro/e80f/Lib;/cache/build/eacp/9a8b7c6d5e4f/Debug/_deps/miro-build/Lib/Miro-MiroResources-Generated"
  INTERFACE_LINK_LIBRARIES "epm_debug::miro_warnings;\$<LINK_ONLY:Threads::Threads>"
  INTERFACE_SOURCES "\$<\$<NOT:\$<OR:\$<STREQUAL:\$<TARGET_PROPERTY:TYPE>,STATIC_LIBRARY>,\$<STREQUAL:\$<TARGET_PROPERTY:TYPE>,OBJECT_LIBRARY>>>:/cache/build/eacp/9a8b7c6d5e4f/Debug/_deps/miro-build/Lib/Miro-MiroResources-Generated/MiroResources_Register.cpp>"
)
set_target_properties(epm_debug::MiroFormats PROPERTIES
  IMPORTED_OBJECTS_DEBUG "/cache/build/eacp/9a8b7c6d5e4f/Debug/_deps/miro-build/Lib/CMakeFiles/MiroFormats.dir/./Miro/TypeExport/BuiltinFormats.cpp.o"
)
set_target_properties(epm_debug::eacp-core PROPERTIES
  IMPORTED_LOCATION_DEBUG "/cache/bin/eacp/9a8b7c6d5e4f/lib/Debug/libeacp-core.a"
  INTERFACE_INCLUDE_DIRECTORIES "/cache/src/eacp/3f1c0a9b2d4e/Lib/eacp/Core/../../"
)
)";
} // namespace

auto tNamesParse = test("Harvest/names/parse") = []
{
    const auto names =
        parseTargetNames("eacp-core\r\nMiro\n\n  fmt  \nMiro\neacp-network\n");

    check(names.size() == 4);
    check(names[0] == "eacp-core");
    check(names[1] == "Miro");
    check(names[2] == "fmt");
    check(names[3] == "eacp-network");
    check(parseTargetNames("").empty());
};

auto tScanPaths = test("Harvest/scan/buildTreePaths") = []
{
    const auto paths = buildTreePaths(exportFixture, harvestBuildDir);

    check(paths.size() == 3);
    check(paths[0] == "_deps/miro-build/Lib/Miro-MiroResources-Generated");
    check(paths[1]
          == "_deps/miro-build/Lib/Miro-MiroResources-Generated/"
             "MiroResources_Register.cpp");
    check(paths[2]
          == "_deps/miro-build/Lib/CMakeFiles/MiroFormats.dir/./Miro/TypeExport/"
             "BuiltinFormats.cpp.o");
};

auto tScanTrailingSlash = test("Harvest/scan/trailingSlashOnBuildDir") = []
{
    const auto withSlash =
        buildTreePaths(exportFixture, std::string {harvestBuildDir} + "/");

    check(withSlash == buildTreePaths(exportFixture, harvestBuildDir));
};

auto tScanNothing = test("Harvest/scan/noBuildTreeReferences") = []
{
    check(buildTreePaths(exportFixture, "/cache/build/eacp/other/Debug").empty());
    check(buildTreePaths("", harvestBuildDir).empty());
};

auto tRelocate = test("Harvest/relocate/rewritesEveryReference") = []
{
    const auto text =
        relocateBuildTree(exportFixture, harvestBuildDir, harvestGenDir);

    check(referencesBuildTree(exportFixture, harvestBuildDir));
    check(!referencesBuildTree(text, harvestBuildDir));
    check(buildTreePaths(text, harvestBuildDir).empty());
    check(text.find("/cache/bin/eacp/9a8b7c6d5e4f/gen/Debug/_deps/miro-build/Lib/"
                    "Miro-MiroResources-Generated/MiroResources_Register.cpp>\"")
          != std::string::npos);
    check(
        text.find("IMPORTED_OBJECTS_DEBUG \"/cache/bin/eacp/9a8b7c6d5e4f/gen/Debug/"
                  "_deps/miro-build/Lib/CMakeFiles/MiroFormats.dir/")
        != std::string::npos);
    check(text.find("/cache/bin/eacp/9a8b7c6d5e4f/lib/Debug/libeacp-core.a")
          != std::string::npos);
    check(text.find("/cache/src/eacp/3f1c0a9b2d4e/Lib/eacp/Core/../../")
          != std::string::npos);
};

auto tReplacePath = test("Harvest/replacePath/wholePathsOnly") = []
{
    const auto text = std::string {
        R"(INTERFACE_INCLUDE_DIRECTORIES "/p/gen/Debug/Gen;/x"
INTERFACE_SOURCES "\$<\$<NOT:...>:/p/gen/Debug/Gen/Reg.cpp>"
OTHER "/p/gen/Debug/Gen/Reg.cpp")"};

    const auto replaced =
        replacePath(text, "/p/gen/Debug/Gen/Reg.cpp", "/p/gen/common/Gen/Reg.cpp");

    check(replaced.find("\"/p/gen/Debug/Gen;/x\"") != std::string::npos);
    check(replaced.find(":/p/gen/common/Gen/Reg.cpp>\"") != std::string::npos);
    check(replaced.find("OTHER \"/p/gen/common/Gen/Reg.cpp\"") != std::string::npos);
    check(replaced.find("Debug/Gen/Reg.cpp") == std::string::npos);

    // The directory alone must not drag the file inside it along.
    const auto directoryOnly = replacePath(text, "/p/gen/Debug/Gen", "/q");

    check(directoryOnly.find("\"/q;/x\"") != std::string::npos);
    check(directoryOnly.find("/p/gen/Debug/Gen/Reg.cpp>") != std::string::npos);
};

auto tThreads = test("Harvest/threads") = []
{
    check(mentionsThreads(exportFixture));
    check(!mentionsThreads("INTERFACE_LINK_LIBRARIES \"epm_debug::fmt\""));
};

auto tTargetsFile = test("Harvest/targetsFile") = []
{
    const auto text =
        targetsFileText({"eacp-core", "Miro"}, {}, true, {"Release", "Debug"});
    const auto threads = text.find("find_package(Threads REQUIRED)");
    const auto debug =
        text.find(R"(include("${CMAKE_CURRENT_LIST_DIR}/export-Debug.cmake"))");
    const auto release =
        text.find(R"(include("${CMAKE_CURRENT_LIST_DIR}/export-Release.cmake"))");

    check(threads != std::string::npos);
    check(debug != std::string::npos && release != std::string::npos);
    check(threads < debug && debug < release);
    check(text.find("        eacp-core\n        Miro)") != std::string::npos);
    check(text.find("add_library(${name} INTERFACE IMPORTED GLOBAL)")
          != std::string::npos);
    check(text.find("$<IF:$<CONFIG:Debug>,epm_debug::${name},epm_release::${name}>")
          != std::string::npos);

    check(targetsFileText({"fmt"}, {}, false, {"Release"}).find("Threads")
          == std::string::npos);
};

auto tTargetsFileOneConfig = test("Harvest/targetsFile/releaseOnly") = []
{
    const auto text = targetsFileText({"fmt"}, {}, false, {"Release"});

    check(text.find("export-Release.cmake") != std::string::npos);
    check(text.find("export-Debug.cmake") == std::string::npos);
    check(text.find("\"epm_release::${name}\"") != std::string::npos);
    check(text.find("$<IF:") == std::string::npos);
    check(text.find("ALIAS") == std::string::npos);
};

auto tAliasesParse = test("Harvest/aliases/parse") = []
{
    const auto aliases = parseTargetAliases("fmt::fmt fmt\r\n\nbroken\n  Miro::Miro "
                                            "Miro  \na b c\nfmt::fmt other\n");

    check(aliases.size() == 2);
    check(aliases[0] == TargetAlias {"fmt::fmt", "fmt"});
    check(aliases[1] == TargetAlias {"Miro::Miro", "Miro"});
    check(parseTargetAliases("").empty());
};

auto tTargetsFileAliases = test("Harvest/targetsFile/aliases") = []
{
    const auto text =
        targetsFileText({"fmt", "ghc_filesystem"},
                        {{"fmt::fmt", "fmt"},
                         {"ghcFilesystem::ghc_filesystem", "ghc_filesystem"},
                         {"other::other", "other"}},
                        false,
                        {"Debug", "Release"});
    const auto loop = text.find("endforeach ()");
    const auto fmt = text.find("if (NOT TARGET fmt::fmt)\n"
                               "    add_library(fmt::fmt ALIAS fmt)\n"
                               "endif ()\n");
    const auto ghc = text.find(
        "if (NOT TARGET ghcFilesystem::ghc_filesystem)\n"
        "    add_library(ghcFilesystem::ghc_filesystem ALIAS ghc_filesystem)\n"
        "endif ()\n");

    check(fmt != std::string::npos && ghc != std::string::npos);
    check(loop < fmt && fmt < ghc);
    check(text.find("other") == std::string::npos);
    check(text.find("$<IF:$<CONFIG:Debug>,epm_debug::${name},epm_release::${name}>")
          != std::string::npos);
};

auto tWriteTargetsAliases = test("Harvest/writeTargetsFile/aliases") = []
{
    auto root = eacp::TemporaryFile {};
    const auto prefix = root.path();

    for (const auto config: {"Debug", "Release"})
    {
        const auto file = prefix / ("export-" + std::string {config} + ".cmake");

        eacp::Files::writeFile(file, "# export\n");
        eacp::Files::writeFile(file.sibling(file.fileName() + ".names"), "fmt\n");
    }

    eacp::Files::writeFile(prefix / "export-Debug.cmake.aliases", "");
    eacp::Files::writeFile(prefix / "export-Release.cmake.aliases",
                           "fmt::fmt fmt\n");
    check(!writeTargetsFile(prefix, {"Debug", "Release"}).has_value());

    const auto text = eacp::Files::readFile(prefix / "epm-targets.cmake");

    check(text.find("add_library(fmt::fmt ALIAS fmt)") != std::string::npos);
    check(text.find("ALIAS") == text.rfind("ALIAS"));
};

auto tInterfaceSources = test("Harvest/scan/interfaceSourcePaths") = []
{
    const auto paths = interfaceSourcePaths(exportFixture, harvestBuildDir);

    check(paths.size() == 1);
    check(paths[0]
          == "_deps/miro-build/Lib/Miro-MiroResources-Generated/"
             "MiroResources_Register.cpp");
    check(interfaceSourcePaths(exportFixture, "/elsewhere").empty());
};

auto tSharedSourcePath = test("Harvest/sharedSourcePath/contentAddressed") = []
{
    const auto relative = "_deps/miro-build/Lib/Gen/MiroResources_Register.cpp";
    const auto debug = sharedSourcePath("int registered = 1;\n", relative);
    const auto release = sharedSourcePath("int registered = 1;\n", relative);
    const auto other = sharedSourcePath("int registered = 2;\n", relative);

    check(debug == release);
    check(debug != other);
    check(debug.rfind("gen/shared/", 0) == 0);
    check(debug.size()
          == std::string {"gen/shared/"}.size() + 12 + 1
                 + std::string {relative}.size());
    check(debug.substr(24) == relative);
    check(debug.find("Debug") == std::string::npos);
};

auto tHarvestShares = test("Harvest/harvestConfig/sharesInterfaceSources") = []
{
    auto root = eacp::TemporaryFile {};
    const auto prefix = root.path() / "prefix";
    const auto generated = std::string {"Gen/Miro-Generated"};
    const auto source = generated + "/Register.cpp";

    // Two configs harvested one after the other, as when Debug is added to
    // an entry that has Release: the registration file must land on one path.
    for (const auto config: {"Release", "Debug"})
    {
        const auto build = root.path() / "build" / config;
        const auto tree = build.str();

        eacp::Files::writeFile(build / source, "int registered = 1;\n");
        eacp::Files::writeFile(build / generated / "Header.h", config);
        eacp::Files::writeFile(
            prefix / ("export-" + std::string {config} + ".cmake"),
            "  INTERFACE_INCLUDE_DIRECTORIES \"" + tree + "/" + generated
                + "\"\n  INTERFACE_SOURCES \"$<$<NOT:x>:" + tree + "/" + source
                + ">\"\n");
        eacp::Files::writeFile(
            prefix / ("export-" + std::string {config} + ".cmake.names"), "Miro\n");
        eacp::Files::writeFile(
            prefix / ("export-" + std::string {config} + ".cmake.aliases"), "");

        const auto result = harvestConfig(build, prefix, config);

        check(result.ok);
        check(result.sharedPaths.size() == 1);
        check(result.relocatedPaths.size() == 1);
        check(!eacp::Files::exists(build));
    }

    const auto release = eacp::Files::readFile(prefix / "export-Release.cmake");
    const auto debug = eacp::Files::readFile(prefix / "export-Debug.cmake");
    const auto shared =
        prefix.str() + "/" + sharedSourcePath("int registered = 1;\n", source);

    check(release.find(":" + shared + ">") != std::string::npos);
    check(debug.find(":" + shared + ">") != std::string::npos);
    check(eacp::Files::isFile(eacp::FilePath {shared}));
    check(debug.find((prefix / "gen" / "Debug").str() + "/" + generated + "\"")
          != std::string::npos);
    check(eacp::Files::readFile(prefix / "gen" / "Debug" / generated / "Header.h")
          == "Debug");

    check(!writeTargetsFile(prefix, {"Debug", "Release"}).has_value());
    check(eacp::Files::readFile(prefix / "epm-targets.cmake").find("export-Debug")
          != std::string::npos);
    check(writeTargetsFile(prefix, {"MinSizeRel"}).has_value());
};
