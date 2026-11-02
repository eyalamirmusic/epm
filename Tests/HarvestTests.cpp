#include <epm/Build/Harvest.h>

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
    const auto text = targetsFileText({"eacp-core", "Miro"}, true);
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

    check(targetsFileText({"fmt"}, false).find("Threads") == std::string::npos);
};
