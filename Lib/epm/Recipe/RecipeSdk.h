#pragma once

#include <eacp/Core/Utils/Containers.h>

#include <string>

namespace epm
{
// How the toolchain that built this binary compiles C++, baked in at build
// time (RecipeSdk.cpp.in): a recipe compiled this way shares epm's ABI, so
// C++ types cross between it and epm. Unset values are empty.
struct RecipeSdk
{
    // CMAKE_CXX_COMPILER and CMAKE_CXX_COMPILER_ID.
    std::string compiler;
    std::string compilerId;

    // CMAKE_CXX_STANDARD, and whether the GNU dialect (-std=gnu++NN) is used.
    std::string cxxStandard;
    bool cxxExtensions = true;

    // CMAKE_CXX_FLAGS followed by the build configuration's flags.
    eacp::Vector<std::string> flags;

    std::string osxSysroot;
    eacp::Vector<std::string> osxArchitectures;
    std::string osxDeploymentTarget;

    // epm-core's, including what it inherits from eacp and Miro.
    eacp::Vector<std::string> includeDirectories;
    eacp::Vector<std::string> compileDefinitions;
    eacp::Vector<std::string> compileOptions;

    // All of the above, one per line: the SDK's part of a recipe's cache key.
    std::string identity;
};

const RecipeSdk& recipeSdk();
} // namespace epm
