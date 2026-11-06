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

    // A Visual Studio generator's CMAKE_GENERATOR_PLATFORM (else
    // CMAKE_VS_PLATFORM_NAME), CMAKE_GENERATOR_TOOLSET and
    // CMAKE_GENERATOR_INSTANCE, which a child build reusing that generator is
    // given (chooseGenerator); empty under other generators.
    std::string generatorPlatform;
    std::string generatorToolset;
    std::string generatorInstance;

    // CMAKE_MAKE_PROGRAM, so a child build that reuses the consumer's
    // generator also finds its build tool when that is not on PATH.
    std::string makeProgram;
    bool isMultiConfig = false;
    std::string buildType;
    eacp::Vector<std::string> configurationTypes;

    // The C compiler of every child build (epm_c_compiler): CMAKE_C_COMPILER,
    // else, for a project that enabled no C, the C++ compiler on the MSVC ABI
    // and the C driver beside it elsewhere (clang for clang++, gcc for g++).
    std::string cCompiler;

    // CMAKE_C_COMPILER_ID and _VERSION when the project enabled C, else empty.
    // The fingerprint records them only when they are not the C++ compiler's.
    std::string cCompilerId;
    std::string cCompilerVersion;
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

    // The effective deployment target (epm_osx_deployment_target):
    // CMAKE_OSX_DEPLOYMENT_TARGET, else the Xcode attribute a project may set
    // instead, else the MACOSX_DEPLOYMENT_TARGET (IPHONEOS_, ...) the compiler
    // would read from the environment.
    std::string osxDeploymentTarget;

    // CMAKE_OSX_SYSROOT: tells an iOS simulator build from a device build.
    std::string osxSysroot;

    // The directory of the Apple SDK the project compiles against
    // (epm_apple_sdk_path), whatever named it: CMAKE_OSX_SYSROOT, SDKROOT or
    // the compiler's default. Every Apple child build gets it as its
    // CMAKE_OSX_SYSROOT, so it compiles against the SDK whose sdkVersion the
    // fingerprint records. Empty off Apple systems; a path, so not part of
    // the fingerprint.
    std::string osxSysrootPath;

    // The version of the SDK the project compiles against (epm_sdk_version):
    // an Apple SDK's SDKSettings.json Version, the Windows SDK version, or
    // the Android NDK's revision; empty elsewhere or when not found. Part of
    // the fingerprint.
    std::string sdkVersion;

    // Android only (epm_android_setting): CMAKE_ANDROID_ARCH_ABI, the API level
    // (CMAKE_SYSTEM_VERSION), CMAKE_ANDROID_NDK and CMAKE_ANDROID_STL_TYPE, or
    // the NDK legacy toolchain file's ANDROID_ABI, ANDROID_PLATFORM_LEVEL,
    // ANDROID_NDK and ANDROID_STL. The fingerprint records the API level and
    // STL; androidNdk is a path, so it counts only through sdkVersion, the
    // NDK's revision.
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
                 generatorPlatform,
                 generatorToolset,
                 generatorInstance,
                 makeProgram,
                 isMultiConfig,
                 buildType,
                 configurationTypes,
                 cCompiler,
                 cCompilerId,
                 cCompilerVersion,
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
                 osxSysrootPath,
                 sdkVersion,
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
