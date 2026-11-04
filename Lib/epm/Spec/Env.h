#pragma once

#include <eacp/Core/Utils/Containers.h>

#include <Miro/Reflect.h>

#include <string>

namespace epm
{
// env.json: the consumer's toolchain, written once per configure by epm.cmake.
// Unset CMake variables arrive as empty strings / arrays.
struct Env
{
    int schema = 1;
    std::string cmake;
    std::string cmakeVersion;
    std::string generator;

    // CMAKE_MAKE_PROGRAM, so a child build that reuses the consumer's
    // generator also finds its build tool when that is not on PATH.
    std::string makeProgram;
    bool isMultiConfig = false;
    std::string buildType;
    eacp::Vector<std::string> configurationTypes;
    std::string cCompiler;
    std::string cxxCompiler;
    std::string cxxCompilerId;
    std::string cxxCompilerVersion;
    std::string systemName;
    std::string systemProcessor;

    // CMAKE_CROSSCOMPILING: the consumer named its target system itself.
    bool crossCompiling = false;

    // CMAKE_OSX_ARCHITECTURES, or, for an Apple device or simulator SDK left
    // without one, the architecture it builds for by default (epm.cmake).
    std::string osxArchitectures;
    std::string osxDeploymentTarget;

    // CMAKE_OSX_SYSROOT: tells an iOS simulator build from a device build.
    std::string osxSysroot;

    // CMAKE_ANDROID_ARCH_ABI (or the NDK toolchain's ANDROID_ABI).
    std::string androidAbi;
    std::string cxxStandard;
    std::string toolchainFile;

    // EPM_PROFILE (CMake variable, else environment): a name for epm.cpp
    // recipes to branch on. epm itself gives it no meaning, and it is not
    // part of any fingerprint.
    std::string profile;

    // EPM_EXECUTABLE: the epm binary running this configure, whose install
    // prefix holds the recipe SDK (RecipeSdk). Not part of any fingerprint;
    // empty when unknown, which means the running executable.
    std::string epmExecutable;

    MIRO_REFLECT(schema,
                 cmake,
                 cmakeVersion,
                 generator,
                 makeProgram,
                 isMultiConfig,
                 buildType,
                 configurationTypes,
                 cCompiler,
                 cxxCompiler,
                 cxxCompilerId,
                 cxxCompilerVersion,
                 systemName,
                 systemProcessor,
                 crossCompiling,
                 osxArchitectures,
                 osxDeploymentTarget,
                 osxSysroot,
                 androidAbi,
                 cxxStandard,
                 toolchainFile,
                 profile,
                 epmExecutable)
};
} // namespace epm
