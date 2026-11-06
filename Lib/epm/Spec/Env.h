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

    // CMAKE_CXX_COMPILER_FRONTEND_VARIANT: MSVC for cl and for clang-cl, which
    // takes cl's command line and targets the MSVC ABI (msvcAbi).
    std::string cxxCompilerFrontend;
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

    // Android only (epm_android_setting): CMAKE_ANDROID_ARCH_ABI, the API level
    // (CMAKE_SYSTEM_VERSION), CMAKE_ANDROID_NDK and CMAKE_ANDROID_STL_TYPE, or
    // the NDK legacy toolchain file's ANDROID_ABI, ANDROID_PLATFORM_LEVEL,
    // ANDROID_NDK and ANDROID_STL.
    std::string androidAbi;
    std::string androidApi;
    std::string androidNdk;
    std::string androidStl;
    std::string cxxStandard;
    std::string toolchainFile;

    // CMAKE_MSVC_RUNTIME_LIBRARY as the consumer's targets get it, on the MSVC
    // ABI only (epm.cmake defaults it to the static runtime); empty elsewhere.
    // Every child build is given it (msvcRuntimeOf), and it is part of the
    // fingerprint: archives of one runtime do not link with objects of another.
    std::string msvcRuntime;

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
                 cxxCompilerFrontend,
                 systemName,
                 systemProcessor,
                 crossCompiling,
                 osxArchitectures,
                 osxDeploymentTarget,
                 osxSysroot,
                 androidAbi,
                 androidApi,
                 androidNdk,
                 androidStl,
                 cxxStandard,
                 toolchainFile,
                 msvcRuntime,
                 profile,
                 epmExecutable)
};
} // namespace epm
