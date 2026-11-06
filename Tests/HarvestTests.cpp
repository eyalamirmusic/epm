#include <epm/Build/Harvest.h>
#include <epm/Util/Command.h>
#include <epm/Util/Paths.h>
#include <epm/Util/Text.h>

#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/TemporaryFile.h>

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace epm;

namespace
{
constexpr auto harvestBuildDir = "/cache/build/eacp/9a8b7c6d5e4f/Debug";

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

auto tScanPaths = test("Harvest/scan/pathsUnder") = []
{
    const auto paths = pathsUnder(exportFixture, harvestBuildDir);

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
        pathsUnder(exportFixture, std::string {harvestBuildDir} + "/");

    check(withSlash == pathsUnder(exportFixture, harvestBuildDir));
};

auto tScanNothing = test("Harvest/scan/noBuildTreeReferences") = []
{
    check(pathsUnder(exportFixture, "/cache/build/eacp/other/Debug").empty());
    check(pathsUnder("", harvestBuildDir).empty());
    check(pathsUnder(exportFixture, "").empty());
};

// The directory itself counts, as "", and a longer name starting with it
// does not.
auto tScanRoot = test("Harvest/scan/directoryItself") = []
{
    const auto text = std::string {R"(A "/src/x;/src/x/include/;/src/xy/z")"};
    const auto paths = pathsUnder(text, "/src/x");

    check(paths.size() == 2);
    check(paths[0].empty());
    check(paths[1] == "include");
};

auto tRelocate = test("Harvest/relocate/rewritesEveryReference") = []
{
    const auto text = rewritePathsUnder(
        exportFixture,
        harvestBuildDir,
        [](std::string_view relative) { return "@gen/" + std::string {relative}; });

    check(pathsUnder(text, harvestBuildDir).empty());
    check(text.find(":@gen/_deps/miro-build/Lib/Miro-MiroResources-Generated/"
                    "MiroResources_Register.cpp>\"")
          != std::string::npos);
    check(text.find("IMPORTED_OBJECTS_DEBUG \"@gen/_deps/miro-build/Lib/CMakeFiles/"
                    "MiroFormats.dir/./Miro/")
          != std::string::npos);
    check(text.find("/cache/bin/eacp/9a8b7c6d5e4f/lib/Debug/libeacp-core.a")
          != std::string::npos);
    check(text.find("/cache/src/eacp/3f1c0a9b2d4e/Lib/eacp/Core/../../")
          != std::string::npos);

    const auto root =
        rewritePathsUnder(R"("/b;/b/;/b/x;/bx")",
                          "/b",
                          [](std::string_view relative)
                          { return "[" + std::string {relative} + "]"; });

    check(root == R"("[];[];[x];/bx")");
};

auto tMentioned = test("Harvest/firstMentionedPath") = []
{
    const auto text =
        std::string {R"(X "${CMAKE_CURRENT_LIST_DIR}/lib/a.a;/cachex/y")"};

    check(!firstMentionedPath(text, {"/cache", "/elsewhere"}).has_value());
    check(firstMentionedPath(text + ";/cache\"", {"/cache"}) == "/cache");
    check(firstMentionedPath("\"/cache/src/a\"", {"/other", "/cache/"}) == "/cache");
    check(firstMentionedPath("-I/cache/x", {"/cache"}) == "/cache");
};

auto tCopiedIntoEntry = test("Harvest/isCopiedIntoEntry") = []
{
    for (const auto* header: {"a/Core",
                              "a/b.h",
                              "a/b.hpp",
                              "a/b.inl",
                              "a/b.ipp",
                              "a/version",
                              "a/b.def",
                              "a/data.json",
                              "a/b.H"})
        check(isCopiedIntoEntry(header, false));

    for (const auto* build: {"a/CMakeLists.txt",
                             "a/x.cmake",
                             "a/Makefile",
                             "a/build.ninja",
                             "a/meson.build",
                             "a/b.o",
                             "a/b.obj",
                             "a/libx.a",
                             "a/x.lib",
                             "a/x.dylib",
                             "a/x.pdb",
                             "a/x.d",
                             "a/x.log"})
    {
        check(!isCopiedIntoEntry(build, false));
        check(!isCopiedIntoEntry(build, true));
    }

    for (const auto* source: {"a/b.cpp", "a/b.c", "a/b.cc", "a/b.mm", "a/b.S"})
    {
        check(!isCopiedIntoEntry(source, false));
        check(isCopiedIntoEntry(source, true));
    }
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

auto tTargetsFile = test("Harvest/targetsFile/fixedAndDiscovering") = []
{
    const auto text = targetsFileText();

    // The same text whatever the entry holds: it finds the configs itself.
    check(text == targetsFileText());
    check(text.find("foreach (_epm_config IN ITEMS Debug Release RelWithDebInfo)")
          != std::string::npos);
    check(text.find(R"(EXISTS "${_epm_unit}/epm-config.json")")
          != std::string::npos);

    const auto threads = text.find("find_package(Threads REQUIRED)");
    const auto include = text.find("include(\"${CMAKE_CURRENT_LIST_DIR}/configs/"
                                   "${_epm_config}/export-${_epm_config}.cmake\")");
    const auto targets =
        text.find("add_library(${_epm_target} INTERFACE IMPORTED GLOBAL)");

    check(threads != std::string::npos && include != std::string::npos);
    check(threads < include && include < targets);
};

auto tTargetsFileSelectors = test("Harvest/targetsFile/selectorPerConfigSet") = []
{
    const auto text = targetsFileText();
    const auto branch = [&](const std::string& configs, const std::string& selector)
    {
        const auto at = text.find("(_epm_configs STREQUAL \"" + configs
                                  + "\")\n"
                                    "        target_link_libraries(${_epm_target} "
                                    "INTERFACE\n                \""
                                  + selector + "\")");
        return at != std::string::npos;
    };

    check(branch("Release", "epm_release::${_epm_target}"));
    check(branch("Debug;Release",
                 "$<IF:$<CONFIG:Debug>,epm_debug::${_epm_target},"
                 "epm_release::${_epm_target}>"));
    check(branch("Debug", "epm_debug::${_epm_target}"));

    // Every non-empty set of the three configs has its branch.
    auto branches = 0;

    for (auto at = text.find("(_epm_configs STREQUAL"); at != std::string::npos;
         at = text.find("(_epm_configs STREQUAL", at + 1))
        ++branches;

    check(branches == 7);
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
    const auto text = targetsFileText();
    const auto targets = text.find("foreach (_epm_target IN LISTS _epm_targets)");
    const auto aliases = text.find("foreach (_epm_line IN LISTS _epm_aliases)");

    // An alias is defined after the targets, only for an exported target.
    check(targets != std::string::npos && targets < aliases);
    check(text.find("if (_epm_target IN_LIST _epm_targets AND NOT TARGET "
                    "${_epm_name})\n"
                    "        add_library(${_epm_name} ALIAS ${_epm_target})")
          != std::string::npos);
};

namespace
{
// What the facade of an entry holding Debug and Release, each exporting X,
// makes X link in a configure of a project with `arguments`: its
// INTERFACE_LINK_LIBRARIES, then whether epm_debug::X was created. Empty
// when cmake cannot run.
std::string facadeLinks(const eacp::FilePath& root,
                        const eacp::Vector<std::string>& arguments)
{
    const auto entry = root / "entry";

    for (const auto* config: {"Debug", "Release"})
    {
        const auto lower = std::string {config} == "Debug" ? "debug" : "release";
        const auto unit = configDirectory(entry, config);
        const auto exportFile = unit / ("export-" + std::string {config} + ".cmake");
        check(writeTextFile(unit / "epm-config.json", "{}"));
        check(writeTextFile(exportFile,
                            "add_library(epm_" + std::string {lower}
                                + "::X INTERFACE IMPORTED)\n"));
        check(writeTextFile(exportFile.sibling(exportFile.fileName() + ".names"),
                            "X\n"));
    }

    check(!writeTargetsFile(entry).has_value());

    const auto project = root / "project";
    const auto binary = root / ("build-" + std::to_string(arguments.size()));
    check(writeTextFile(project / "CMakeLists.txt",
                        "cmake_minimum_required(VERSION 3.24)\n"
                        "project(Facade NONE)\n"
                        "include(\""
                            + entry.str()
                            + "/epm-targets.cmake\")\n"
                              "get_target_property(links X "
                              "INTERFACE_LINK_LIBRARIES)\n"
                              "if (TARGET epm_debug::X)\n"
                              "    set(debug yes)\n"
                              "else ()\n"
                              "    set(debug no)\n"
                              "endif ()\n"
                              "file(WRITE \"${CMAKE_BINARY_DIR}/links.txt\" "
                              "\"${links} ${debug}\")\n"));

    auto command =
        eacp::Vector<std::string> {"-S", project.str(), "-B", binary.str()};
    command.addFrom(arguments);

    if (!runCommand(EPM_TEST_CMAKE, command).ok)
        return {};

    return eacp::Files::readFile(binary / "links.txt");
}
} // namespace

auto tTargetsFileConfigs = test("Harvest/targetsFile/loadsOnlyUsableConfigs") = []
{
    if (!eacp::Files::isFile(eacp::FilePath {EPM_TEST_CMAKE}))
        return;

    auto root = eacp::TemporaryFile {};

    // Without EPM_FACADE_CONFIGS, every published config.
    check(facadeLinks(root.path(), {})
          == "$<IF:$<CONFIG:Debug>,epm_debug::X,epm_release::X> yes");

    // With it, only those: the consumer's Debug links Release, and Debug's
    // archives (LTO objects of another compiler, say) are never loaded.
    check(facadeLinks(root.path(), {"-DEPM_FACADE_CONFIGS=Release"})
          == "epm_release::X no");
};

auto tWriteTargetsFile = test("Harvest/writeTargetsFile") = []
{
    auto root = eacp::TemporaryFile {};

    check(!writeTargetsFile(root.path()).has_value());
    check(eacp::Files::readFile(root.path() / "epm-targets.cmake")
          == targetsFileText());
};

auto tConfigDirectory = test("Harvest/configDirectory") = []
{
    check(configDirectory(eacp::FilePath {"/c/bin/x/p/fp"}, "Debug")
          == eacp::FilePath {"/c/bin/x/p/fp/configs/Debug"});
    check(sharedRootLine()
          == "get_filename_component(_epm_entry_dir "
             "\"${CMAKE_CURRENT_LIST_DIR}/../..\" ABSOLUTE)\n");
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
    const auto entry = root.path() / "entry";
    const auto generated = std::string {"Gen/Miro-Generated"};
    const auto source = generated + "/Register.cpp";

    // Two configs harvested one after the other, as when Debug is added to
    // an entry that has Release: the registration file must land on one path.
    for (const auto config: {"Release", "Debug"})
    {
        const auto build = root.path() / "build" / config;
        const auto tree = build.str();
        const auto unit = configDirectory(entry, config);

        check(writeTextFile(build / source, "int registered = 1;\n"));
        check(writeTextFile(build / generated / "Header.h", config));
        check(writeTextFile(unit / ("export-" + std::string {config} + ".cmake"),
                            "  INTERFACE_INCLUDE_DIRECTORIES \"" + tree + "/"
                                + generated + "\"\n  INTERFACE_SOURCES \"$<$<NOT:x>:"
                                + tree + "/" + source + ">\"\n"));
        check(writeTextFile(
            unit / ("export-" + std::string {config} + ".cmake.names"), "Miro\n"));

        const auto result = harvestConfig(build, entry, config, {});

        check(result.ok);
        check(result.sharedPaths.size() == 1);
        check(result.relocatedPaths.size() == 1);
        check(!eacp::Files::exists(build));
    }

    const auto release = eacp::Files::readFile(configDirectory(entry, "Release")
                                               / "export-Release.cmake");
    const auto debugUnit = configDirectory(entry, "Debug");
    const auto debug = eacp::Files::readFile(debugUnit / "export-Debug.cmake");
    const auto shared = sharedSourcePath("int registered = 1;\n", source);
    const auto named = std::string {sharedRootVariable} + "/" + shared;

    // Both name the shared file through the entry, with the line that sets
    // it first.
    check(release.rfind(sharedRootLine(), 0) == 0);
    check(debug.rfind(sharedRootLine(), 0) == 0);
    check(release.find(":" + named + ">") != std::string::npos);
    check(debug.find(":" + named + ">") != std::string::npos);
    check(eacp::Files::isFile(entry / shared));
    check(debug.find("\"" + std::string {entryDirectory} + "/gen/Debug/" + generated
                     + "\"")
          != std::string::npos);
    check(eacp::Files::readFile(debugUnit / "gen" / "Debug" / generated / "Header.h")
          == "Debug");

    // The registration file went to gen/shared alone; the generated directory
    // keeps no sources.
    check(!eacp::Files::exists(debugUnit / "gen" / "Debug" / source));
};

namespace
{
// A scratch cache with one package's source, CPM cache, build tree and entry.
struct HarvestCache
{
    HarvestCache()
    {
        for (const auto& [path, text]:
             {std::pair {source / "include/pkg/Pkg.h", "h"},
              {source / "include/pkg/version", "v"},
              {source / "include/pkg/impl.cpp", "c"},
              {source / "include/CMakeLists.txt", "c"},
              {source / "include/.git/HEAD", "g"},
              {source / "Lib/eacp/Core/Core.h", "h"},
              {source / "module/module.cpp", "m"},
              {source / "module/inner.cpp", "i"},
              {source / "lib/libfmod.a", "a"},
              {source / "lib/fmod.dylib", "d"},
              {source / "lib/.git/HEAD", "g"},
              {source / "sdk/Foo.xcframework/Info.plist", "p"},
              {source / "sdk/Foo.xcframework/ios-arm64/libFoo.a", "a"},
              {source / "standalone/unit.cpp", "u"},
              {source / "standalone/impl_detail.h", "h"},
              {source / "standalone/helper.cpp", "h"},
              {cpm / "dep/1234/include/dep.h", "d"},
              {cache / "src/Leaf/def/include/Leaf.h", "l"},
              {cache / "src/Leaf/def/CMakeLists.txt", "c"},
              {cache / "bin/Leaf/macos-arm64/fp2/configs/Release/lib/libLeaf.a",
               "a"},
              {build / "gen/Gen.h", "g"}})
            check(writeTextFile(path, text));

        check(writeTextFile(prefix / "export-Release.cmake.names", "pkg\n"));
    }

    // Harvests an export file holding `properties`, in which @SRC@, @CPM@,
    // @BUILD@, @PREFIX@ and @CACHE@ stand for those directories.
    HarvestResult harvest(std::string properties) const
    {
        for (const auto& [token, path]: {std::pair {"@SRC@", source},
                                         {"@CPM@", cpm},
                                         {"@BUILD@", build},
                                         {"@PREFIX@", prefix},
                                         {"@CACHE@", cache}})
            properties = replaceAll(properties, token, path.str());

        check(writeTextFile(prefix / "export-Release.cmake", properties));
        return harvestConfig(build, entry, "Release", {cache, source, cpm});
    }

    std::string exported() const
    {
        return eacp::Files::readFile(prefix / "export-Release.cmake");
    }

    eacp::TemporaryFile root;
    eacp::FilePath cache = root.path() / "cache";
    eacp::FilePath source = cache / "src/pkg/abc";
    eacp::FilePath cpm = cache / "cpm";
    eacp::FilePath build = cache / "build/pkg/macos-arm64/fp/Release";
    eacp::FilePath entry = cache / "bin/pkg/macos-arm64/.staging/fp.1";

    // The unit of Release in the staged entry, where the build wrote.
    eacp::FilePath prefix = configDirectory(entry, "Release");
};
} // namespace

auto tHarvestRelocatable = test("Harvest/harvestConfig/relocatable") = []
{
    const auto scratch = HarvestCache {};
    const auto result = scratch.harvest(
        "  INTERFACE_INCLUDE_DIRECTORIES \"@SRC@/include;@SRC@/include/pkg/;"
        "@SRC@/Lib/eacp/Core/../../;@CPM@/dep/1234/include;@BUILD@/gen;@SRC@/module\"\n"
        "  INTERFACE_SOURCES \"$<$<NOT:x>:@SRC@/module/module.cpp>\"\n"
        "  IMPORTED_LOCATION_RELEASE \"@PREFIX@/lib/Release/libpkg.a\"\n");
    const auto in = std::string {entryDirectory};
    const auto text = scratch.exported();
    const auto& prefix = scratch.prefix;

    check(result.ok);
    check(text
          == "  INTERFACE_INCLUDE_DIRECTORIES \"" + in + "/include/src/include;" + in
                 + "/include/src/include/pkg;" + in + "/include/src/Lib;" + in
                 + "/include/cpm/dep/1234/include;" + in + "/gen/Release/gen;" + in
                 + "/include/src/module\"\n  INTERFACE_SOURCES \"$<$<NOT:x>:" + in
                 + "/include/src/module/module.cpp>\"\n"
                   "  IMPORTED_LOCATION_RELEASE \""
                 + in + "/lib/Release/libpkg.a\"\n");

    // One copy per outermost directory; the module tree keeps its sources.
    check(result.includedPaths.size() == 5);
    check(result.includedPaths.contains("include/src/include"));
    check(result.includedPaths.contains("include/cpm/dep/1234/include"));
    check(eacp::Files::isFile(prefix / "include/src/include/pkg/Pkg.h"));
    check(eacp::Files::isFile(prefix / "include/src/include/pkg/version"));
    check(eacp::Files::isFile(prefix / "include/src/Lib/eacp/Core/Core.h"));
    check(eacp::Files::isFile(prefix / "include/cpm/dep/1234/include/dep.h"));
    check(eacp::Files::isFile(prefix / "include/src/module/inner.cpp"));
    check(eacp::Files::isFile(prefix / "gen/Release/gen/Gen.h"));
    check(!eacp::Files::exists(prefix / "include/src/include/pkg/impl.cpp"));
    check(!eacp::Files::exists(prefix / "include/src/include/CMakeLists.txt"));
    check(!eacp::Files::exists(prefix / "include/src/include/.git"));
    check(!eacp::Files::exists(scratch.build));
};

auto tHarvestRejects = test("Harvest/harvestConfig/rejectsOutsidePaths") = []
{
    const auto failsWith =
        [](const std::string& properties, const std::string& error)
    {
        const auto scratch = HarvestCache {};
        const auto result = scratch.harvest(properties);

        check(!result.ok);
        check(result.error.find(error) != std::string::npos);
        check(eacp::Files::exists(scratch.build));
    };

    failsWith("  X \"@CACHE@/tmp/x/include\"\n", "still names");
    failsWith("  INTERFACE_LINK_DIRECTORIES \"@CACHE@/build/other/x\"\n",
              "still names");
    failsWith("  X \"@SRC@/missing\"\n", "X names");
    failsWith("  X \"@SRC@/missing\"\n", "does not exist");
    failsWith("  X \"@SRC@/../other\"\n", "outside");
    failsWith("  X \"@PREFIX@/../other\"\n", "outside the entry");
    failsWith("  INTERFACE_INCLUDE_DIRECTORIES \"@CACHE@/src/Leaf/zzz/include\"\n",
              "INTERFACE_INCLUDE_DIRECTORIES names");
    failsWith("  INTERFACE_INCLUDE_DIRECTORIES \"@CACHE@/src/Leaf/zzz/include\"\n",
              "does not exist");
    failsWith("  X \"@CACHE@/src/Leaf\"\n", "in no package's snapshot");
    failsWith("  X \"@CACHE@/bin/other/x\"\n", "in no package's entry");
};

auto tScanBoundaries = test("Harvest/scan/pathStartBoundaries") = []
{
    const auto root = std::string {"/opt/epm"};

    check(!firstMentionedPath(R"("/usr/local/opt/epm/include")", {root}));
    check(!firstMentionedPath(R"("x-I/opt/epm/include")", {root}));
    check(firstMentionedPath(R"("-isystem/opt/epm/include")", {root}) == root);
    check(firstMentionedPath("$<BUILD_INTERFACE:/opt/epm/x>", {root}) == root);
    check(firstMentionedPath(R"("SHELL:-I /opt/epm/x")", {root}) == root);
    check(firstMentionedPath("LINKER:-rpath,/opt/epm/lib", {root}) == root);

    const auto paths = pathsUnder(
        R"("/usr/local/opt/epm/include;/opt/epm/a;-L/opt/epm/b;-F/opt/epm/c")",
        root);

    check(paths == eacp::Vector<std::string> {"a", "b", "c"});
};

auto tScanTerminators = test("Harvest/scan/genexAndShellTerminators") = []
{
    const auto paths =
        pathsUnder(R"("$<IF:$<CONFIG:Debug>,/s/a,/s/b>;SHELL:-include /s/c.h -DX")"
                   "\n  X \"/s/d e/f\"\n",
                   "/s");

    // A space ends a path only inside a SHELL: option.
    check(paths == eacp::Vector<std::string> {"a", "b", "c.h", "d e/f"});

    const auto rewritten =
        rewritePathsUnder(R"("$<IF:c,/s/a,/s/b>;SHELL:-isystem /s/c -DX;-I/s/d")",
                          "/s",
                          [](std::string_view relative)
                          { return "<" + std::string {relative} + ">"; });

    check(rewritten == R"("$<IF:c,<a>,<b>>;SHELL:-isystem <c> -DX;-I<d>")");
};

auto tScanMentions = test("Harvest/scan/pathMentionsUnder") = []
{
    const auto mentions =
        pathMentionsUnder("  INTERFACE_INCLUDE_DIRECTORIES \"/s/a\"\n"
                          "  INTERFACE_COMPILE_OPTIONS \"-isystem/s/b;-L/s/c\"\n"
                          "      BASE_DIRS \"/s/d\"\n"
                          "set(x \"/s/e\")\n"
                          "  INTERFACE_SOURCES \"/s/a\"\n",
                          "/s");

    check(mentions.size() == 6);
    check(mentions[0] == PathMention {"a", "INTERFACE_INCLUDE_DIRECTORIES", true});
    check(mentions[1] == PathMention {"b", "INTERFACE_COMPILE_OPTIONS", true});
    check(mentions[2] == PathMention {"c", "INTERFACE_COMPILE_OPTIONS", false});
    check(mentions[3] == PathMention {"d", "BASE_DIRS", true});
    check(mentions[4] == PathMention {"e", "", false});
    check(mentions[5] == PathMention {"a", "INTERFACE_SOURCES", false});
};

auto tSourceRoot = test("Harvest/sourceRootOf") = []
{
    using eacp::FilePath;

    check(sourceRootOf("/c", "/c/src/zstd/abc/build/cmake")
          == FilePath {"/c/src/zstd/abc"});
    check(sourceRootOf("/c", "/c/src/zstd/abc") == FilePath {"/c/src/zstd/abc"});
    check(sourceRootOf("/c/", "/c/src/zstd/abc/build")
          == FilePath {"/c/src/zstd/abc"});
    check(sourceRootOf("/c", "/c/src/zstd") == FilePath {"/c/src/zstd"});
    check(sourceRootOf("/c", "/work/zstd/build/cmake")
          == FilePath {"/work/zstd/build/cmake"});
    check(sourceRootOf({}, "/c/src/zstd/abc/build")
          == FilePath {"/c/src/zstd/abc/build"});
};

// Directories named other than as include directories are copied whole: a
// vendored SDK's link directory, a framework.
auto tHarvestLinkDirectories =
    test("Harvest/harvestConfig/linkDirectoriesWhole") = []
{
    const auto scratch = HarvestCache {};
    const auto result = scratch.harvest(
        "  INTERFACE_INCLUDE_DIRECTORIES \"@SRC@/include;@SRC@/lib\"\n"
        "  INTERFACE_LINK_DIRECTORIES \"@SRC@/lib\"\n"
        "  INTERFACE_LINK_LIBRARIES \"fmod;@SRC@/sdk/Foo.xcframework\"\n"
        "  INTERFACE_COMPILE_OPTIONS \"-I@SRC@/module\"\n");
    const auto& prefix = scratch.prefix;

    check(result.ok);
    check(eacp::Files::isFile(prefix / "include/src/lib/libfmod.a"));
    check(eacp::Files::isFile(prefix / "include/src/lib/fmod.dylib"));
    check(!eacp::Files::exists(prefix / "include/src/lib/.git"));
    check(
        eacp::Files::isFile(prefix / "include/src/sdk/Foo.xcframework/Info.plist"));
    check(eacp::Files::isFile(
        prefix / "include/src/sdk/Foo.xcframework/ios-arm64/libFoo.a"));

    // Include directories, by property or by option, keep only headers.
    check(eacp::Files::isFile(prefix / "include/src/include/pkg/Pkg.h"));
    check(!eacp::Files::exists(prefix / "include/src/include/pkg/impl.cpp"));
    check(!eacp::Files::exists(prefix / "include/src/include/CMakeLists.txt"));
    check(!eacp::Files::exists(prefix / "include/src/module/inner.cpp"));
};

// A source file the consumer compiles brings its directory's siblings, and
// the directories it lies in keep their sources whichever spelling names
// them.
auto tHarvestSourceSiblings = test("Harvest/harvestConfig/sourceSiblings") = []
{
    const auto scratch = HarvestCache {};
    const auto resolvedSource = resolvedPath(scratch.source).str();
    const auto result = scratch.harvest(
        "  INTERFACE_INCLUDE_DIRECTORIES \"" + resolvedSource
        + "/module\"\n"
          "  INTERFACE_SOURCES \"@SRC@/standalone/unit.cpp;@SRC@/module/module.cpp\"\n");
    const auto& prefix = scratch.prefix;
    const auto in = std::string {entryDirectory};

    check(result.ok);
    check(eacp::Files::isFile(prefix / "include/src/standalone/unit.cpp"));
    check(eacp::Files::isFile(prefix / "include/src/standalone/impl_detail.h"));
    check(eacp::Files::isFile(prefix / "include/src/standalone/helper.cpp"));
    check(eacp::Files::isFile(prefix / "include/src/module/inner.cpp"));
    check(scratch.exported()
          == "  INTERFACE_INCLUDE_DIRECTORIES \"" + in
                 + "/include/src/module\"\n  INTERFACE_SOURCES \"" + in
                 + "/include/src/standalone/unit.cpp;" + in
                 + "/include/src/module/module.cpp\"\n");
};

auto tHarvestOptions = test("Harvest/harvestConfig/optionsAndGenexes") = []
{
    const auto scratch = HarvestCache {};
    const auto result = scratch.harvest(
        "  INTERFACE_COMPILE_OPTIONS \"-I@SRC@/include;-isystem@CPM@/dep/1234/include;"
        "SHELL:-include @SRC@/include/pkg/Pkg.h -DX\"\n"
        "  INTERFACE_INCLUDE_DIRECTORIES \"$<IF:$<CONFIG:Debug>,@SRC@/include,"
        "@SRC@/Lib>\"\n"
        "  INTERFACE_LINK_OPTIONS \"-L@SRC@/lib;LINKER:-rpath,@SRC@/lib\"\n");
    const auto in = std::string {entryDirectory};

    check(result.ok);
    check(scratch.exported()
          == "  INTERFACE_COMPILE_OPTIONS \"-I" + in
                 + "/include/src/include;-isystem" + in
                 + "/include/cpm/dep/1234/include;SHELL:-include " + in
                 + "/include/src/include/pkg/Pkg.h -DX\"\n"
                   "  INTERFACE_INCLUDE_DIRECTORIES \"$<IF:$<CONFIG:Debug>,"
                 + in + "/include/src/include," + in
                 + "/include/src/Lib>\"\n  INTERFACE_LINK_OPTIONS \"-L" + in
                 + "/include/src/lib;LINKER:-rpath," + in + "/include/src/lib\"\n");
    check(eacp::Files::isFile(scratch.prefix / "include/src/lib/libfmod.a"));
};

// What a nested child build takes from its dependencies: Leaf's snapshot
// (${Leaf_SOURCE_DIR}/include) and Leaf's entry.
auto tHarvestDependencies = test("Harvest/harvestConfig/dependencyTrees") = []
{
    const auto scratch = HarvestCache {};
    const auto result = scratch.harvest(
        "  INTERFACE_INCLUDE_DIRECTORIES \"@CACHE@/src/Leaf/def/include\"\n"
        "  INTERFACE_LINK_LIBRARIES \"@CACHE@/bin/Leaf/macos-arm64/fp2/configs/"
        "Release/lib/libLeaf.a\"\n");
    const auto& prefix = scratch.prefix;
    const auto in = std::string {entryDirectory};

    check(result.ok);
    check(scratch.exported()
          == "  INTERFACE_INCLUDE_DIRECTORIES \"" + in
                 + "/include/dep/Leaf/include\"\n  INTERFACE_LINK_LIBRARIES \"" + in
                 + "/dep/Leaf/configs/Release/lib/libLeaf.a\"\n");
    check(eacp::Files::isFile(prefix / "include/dep/Leaf/include/Leaf.h"));
    check(!eacp::Files::exists(prefix / "include/dep/Leaf/CMakeLists.txt"));
    check(eacp::Files::isFile(prefix / "dep/Leaf/configs/Release/lib/libLeaf.a"));
    check(result.includedPaths.contains("include/dep/Leaf/include"));
};
