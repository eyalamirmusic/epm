#pragma once

#include <epm/Cache/CacheRoot.h>
#include <epm/Spec/Env.h>
#include <epm/Spec/Request.h>

#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace epm
{
struct Generator
{
    // Empty: leave -G out and take CMake's platform default.
    std::string name;

    // Empty: let CMake find the build tool.
    std::string makeProgram;

    // -A, -T and CMAKE_GENERATOR_INSTANCE, each when not empty: a Visual
    // Studio generator's.
    std::string platform {};
    std::string toolset {};
    std::string instance {};
};

// The consumer's generator when it is single-config, else Ninja when it is on
// PATH, else the ninja a Ninja Multi-Config consumer runs, else a Visual
// Studio consumer's generator with its platform, toolset and instance, else
// Unix Makefiles (the platform default on Windows). Always naming the
// generator keeps the environment's CMAKE_GENERATOR (and with it
// CMAKE_GENERATOR_PLATFORM, _TOOLSET and _INSTANCE) from choosing it.
Generator chooseGenerator(const Env& env,
                          const std::optional<eacp::FilePath>& ninja);

// One configuration of a dependency, built straight into `prefix`: the
// config's unit (configs/<Config>/) in a staged entry, which is published into
// the binary cache entry once complete (BinaryCache.h).
struct ChildBuild
{
    // The package's name, and its options as -D<KEY>=<VALUE>.
    Request request;

    // Raw -D<NAME>=<VALUE> definitions passed after the options, so one may
    // override what epm itself passes (CMAKE_CXX_STANDARD, ...). NAME may
    // carry a :TYPE.
    std::map<std::string, std::string> definitions;

    Env env;
    Generator generator;
    eacp::FilePath sourceDirectory;
    eacp::FilePath buildDirectory;
    eacp::FilePath prefix;
    eacp::FilePath cpmCache;

    // The cache the build runs in, which holds the dependencies' snapshots
    // and entries a nested child build reads; empty: not mapped.
    eacp::FilePath cacheRoot;

    std::string config;

    // graph.cmake (Docs/Graph.md §7) of the package's dependency closure, all
    // prebuilt, which the nested provider loads (§10). Empty for a package
    // without dependencies: no provider is injected.
    std::string dependencyGraph;

    // Handed to a child with dependencies as EPM_EXECUTABLE, so a get_epm.cmake
    // of its own finds the epm that runs the graph. It comes from the build
    // step's input (BuildInput::epmExecutable): inside a recipe executable,
    // this process is the recipe, not epm.
    eacp::FilePath epmExecutable;
};

// The `<old>=<new>` maps that keep the cache out of what the compiler writes
// (debug info, __FILE__): the cache root, the source (its snapshot's root,
// sourceRootOf), the build tree and the CPM cache, each as written and with
// its symlinks resolved, onto /epm/cache, /epm/src/<name>, /epm/build/<name>
// and /epm/cpm. The cache root comes first: GCC applies the last map that
// matches, Clang the last or the longest by version (AppleClang 21 the
// longest, checked), so either way a more specific map after it wins. None on
// the MSVC ABI, whose cl has
// no documented equivalent (/d1trimfile is undocumented and covers __FILE__
// alone).
eacp::Vector<std::string> filePrefixMaps(const ChildBuild& build);

// The `cmake -S -B` arguments of PoC §5. Toolchain values the consumer left
// empty are not passed at all; the target system and processor only when it
// cross-compiles without a toolchain file (crossCompilesWithoutToolchain).
// An Apple child build gets the consumer's resolved SDK directory as its
// sysroot (Env::osxSysrootPath), else, cross-compiling without a toolchain
// file, the consumer's CMAKE_OSX_SYSROOT. On the MSVC ABI the consumer's runtime
// library (msvcRuntimeOf), with the policy (CMP0091) that makes a project
// with an older cmake_minimum_required honour it, and the C++ compiler for
// C as well when the consumer enabled no C; elsewhere the filePrefixMaps as
// EPM_FILE_PREFIX_MAPS, which epm-export.cmake adds as compile options for
// GCC and Clang. The archives go to <prefix>/lib/<config> through
// CMAKE_ARCHIVE_OUTPUT_DIRECTORY and its per-config CMAKE_ARCHIVE_OUTPUT_
// DIRECTORY_<CONFIG>, which a multi-config generator (Visual Studio) takes as
// it is instead of adding a <config> subdirectory. With a dependencyGraph
// also EPM_NESTED, EPM_GRAPH, EPM_EXECUTABLE and epm-nested.cmake after
// epm-export.cmake in CMAKE_PROJECT_TOP_LEVEL_INCLUDES (Docs/Graph.md §10).
eacp::Vector<std::string> configureArguments(const ChildBuild& build);

// `--build <dir> --config <config> --parallel`: a multi-config generator
// (Visual Studio) would build Debug without --config, a single-config one
// ignores it.
eacp::Vector<std::string> buildArguments(const ChildBuild& build);

// epm-export.cmake as it was when this binary was built.
std::string_view embeddedExportScript();

// epm-nested.cmake: epm-provider.cmake as it was when this binary was built,
// plus a tail registering the provider over ${EPM_GRAPH}.
std::string_view embeddedNestedScript();

struct ChildBuildResult
{
    bool ok = false;
    std::string error;

    // The tail of the failing step's output; the whole of it is in a log
    // file in the build directory, which is kept.
    std::string details;
};

// Writes epm-export.cmake into the build directory (with dependencies also
// epm-nested.cmake and epm-graph.cmake), then runs the configure step. The
// output goes to epm-configure.log in the build directory.
ChildBuildResult configureChildBuild(const ChildBuild& build);

// `cmake --build` of a configured build directory, logged to epm-build.log.
ChildBuildResult buildChildBuild(const ChildBuild& build);
} // namespace epm
