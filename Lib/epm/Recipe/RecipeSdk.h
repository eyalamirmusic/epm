#pragma once

#include <eacp/Core/Utils/Containers.h>
#include <eacp/Core/Utils/FilePath.h>

#include <map>
#include <optional>
#include <string>

namespace epm
{
// How the toolchain that built this binary compiles and links a recipe
// executable against the epm-recipe library (Recipe/Recipe.h). Unset values
// are empty.
struct RecipeSdk
{
    // CMAKE_CXX_COMPILER and CMAKE_CXX_COMPILER_ID.
    std::string compiler;
    std::string compilerId;

    // CMAKE_CXX_STANDARD, and whether the GNU dialect (-std=gnu++NN) is used.
    std::string cxxStandard;
    bool cxxExtensions = true;

    // The configuration this binary was built in: epm compiles its recipes
    // with its flags and links them against its libraries.
    std::string config;

    // CMAKE_CXX_FLAGS followed by `config`'s flags.
    eacp::Vector<std::string> flags;

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

// installedRecipeSdk for the prefix this executable runs from, else the build
// tree's. Located once per process.
const RecipeSdk& recipeSdk();
} // namespace epm
