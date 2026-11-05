#pragma once

#include <eacp/Core/Utils/Containers.h>
#include <eacp/Core/Utils/FilePath.h>

#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace epm
{
// How the toolchain that built this binary compiles and links a recipe
// executable against the epm-recipe library (Recipe/Recipe.h). Unset values
// are empty.
struct RecipeSdk
{
    // CMAKE_CXX_COMPILER, CMAKE_CXX_COMPILER_ID and
    // CMAKE_CXX_COMPILER_FRONTEND_VARIANT: the command line the compiler
    // takes, "MSVC" for cl and clang-cl, "GNU" for the rest (clCommandLine).
    std::string compiler;
    std::string compilerId;
    std::string compilerFrontend;

    // CMAKE_CXX_STANDARD, and whether the GNU dialect (-std=gnu++NN) is used.
    std::string cxxStandard;
    bool cxxExtensions = true;

    // The configuration this binary was built in: epm compiles its recipes
    // with its flags and links them against its libraries.
    std::string config;

    // CMAKE_CXX_FLAGS followed by `config`'s flags.
    eacp::Vector<std::string> flags;

    // The MSVC runtime library the archives of `config` were compiled against
    // (MSVC_RUNTIME_LIBRARY: MultiThreadedDLL, MultiThreadedDebugDLL, ...),
    // which a recipe must match; empty off the MSVC ABI.
    std::string msvcRuntime;

    std::string osxSysroot;
    eacp::Vector<std::string> osxArchitectures;
    std::string osxDeploymentTarget;

    // epm-recipe's, including what it inherits from eacp-core, eacp-network
    // and Miro.
    eacp::Vector<std::string> includeDirectories;
    eacp::Vector<std::string> compileDefinitions;
    eacp::Vector<std::string> compileOptions;

    // Per configuration, the static archives a recipe links, absolute, in
    // link order: epm-recipe first, each before the archives it depends on.
    std::map<std::string, eacp::Vector<std::string>> libraries;

    // What a recipe links besides those archives: frameworks ("-framework
    // Foundation"), system libraries ("pthread", "-lobjc"), linker flags.
    eacp::Vector<std::string> linkOptions;

    // What a recipe build depends on (all of the above but the other
    // configurations' libraries), one per line: the SDK's part of a recipe's
    // cache key.
    std::string identity;
};

// The configurations `sdk.libraries` holds, in name order.
eacp::Vector<std::string> recipeConfigs(const RecipeSdk& sdk);

// The archives a recipe built by this epm links: those of `sdk.config`.
eacp::Vector<std::string> recipeLibraries(const RecipeSdk& sdk);

// Whether the compiler takes cl's command line (/std:, /Fe, .lib names): cl
// itself, and clang-cl, whose id is Clang with an MSVC frontend.
bool clCommandLine(const RecipeSdk& sdk);

// The MSVC runtime library the archives of `config` were compiled against:
// `sdk.msvcRuntime` with CMake's default rule for the other configurations
// (the Debug runtime for Debug, the plain one for the rest), as installs of
// several configurations share the same choice. Empty off the MSVC ABI.
std::string msvcRuntimeFor(const RecipeSdk& sdk, std::string_view config);

// The build tree's SDK, baked in at build time (RecipeSdk.cpp.in), and where
// `cmake --install` puts the same headers and archives, relative to the
// install prefix.
struct BakedRecipeSdk
{
    RecipeSdk sdk;

    // include/, and lib/, holding one directory per installed configuration.
    std::string includeDestination;
    std::string libraryDestination;

    // The archive file names, one per sdk.libraries entry, as installed.
    eacp::Vector<std::string> installedLibraries;
};

const BakedRecipeSdk& bakedRecipeSdk();

// The SDK of the install prefix `prefix` (<prefix>/bin/epm): its include
// directory, and the archives of every configuration installed under its
// library directory. nullopt when `prefix` holds no install with the headers
// and the libraries of `baked.sdk.config`.
std::optional<RecipeSdk> installedRecipeSdk(const BakedRecipeSdk& baked,
                                            const eacp::FilePath& prefix);

// installedRecipeSdk for the prefix of the epm binary `epmExecutable`
// (<prefix>/bin/epm, through any symlink to it), else the build tree's. What
// code linking epm-recipe outside epm's own process (a recipe executable)
// finds the SDK with, from Env::epmExecutable.
RecipeSdk recipeSdkFor(const eacp::FilePath& epmExecutable);

// recipeSdkFor this executable. Located once per process.
const RecipeSdk& recipeSdk();
} // namespace epm
