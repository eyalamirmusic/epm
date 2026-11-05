# Self-test of env.json's Apple architecture and deployment-target fallbacks in
# CMake/epm.cmake (epm_osx_architectures, epm_osx_deployment_target), and of
# its MSVC runtime library (epm_msvc_runtime) and Android settings
# (epm_android_setting).
#
#   cmake -P Tests/EpmEnv.cmake

cmake_minimum_required(VERSION 3.25)

set(EPM_FUNCTIONS_ONLY ON)
include("${CMAKE_CURRENT_LIST_DIR}/../CMake/epm.cmake")

set(failures 0)

# Calls `function` in a fresh function scope holding only ARGN's NAME=VALUE
# variables, and compares what it returns with `expected`. `|` stands for `;`.
function(expect function expected)
    foreach (assignment IN LISTS ARGN)
        string(FIND "${assignment}" "=" equals)
        string(SUBSTRING "${assignment}" 0 ${equals} name)
        math(EXPR equals "${equals} + 1")
        string(SUBSTRING "${assignment}" ${equals} -1 value)
        string(REPLACE "|" ";" value "${value}")
        set(${name} "${value}")
    endforeach ()

    cmake_language(CALL ${function} actual)
    string(REPLACE "|" ";" expected "${expected}")

    if (NOT actual STREQUAL expected)
        message(SEND_ERROR "${function} (${ARGN}) is '${actual}', expected "
                "'${expected}'")
        set(failures 1 PARENT_SCOPE)
    endif ()
endfunction()

# The consumer's architectures always win.
expect(epm_osx_architectures "arm64" CMAKE_SYSTEM_NAME=iOS
        CMAKE_OSX_ARCHITECTURES=arm64 "CMAKE_XCODE_ARCHS=arm64|x86_64")
expect(epm_osx_architectures "arm64|x86_64" CMAKE_SYSTEM_NAME=Darwin
        "CMAKE_OSX_ARCHITECTURES=arm64|x86_64")

# An iOS SDK without them: what Xcode builds, else the compiler's target.
expect(epm_osx_architectures "arm64|x86_64" CMAKE_SYSTEM_NAME=iOS
        "CMAKE_XCODE_ARCHS=arm64|x86_64" CMAKE_CXX_COMPILER_ARCHITECTURE_ID=arm64)
expect(epm_osx_architectures "arm64" CMAKE_SYSTEM_NAME=iOS
        CMAKE_CXX_COMPILER_ARCHITECTURE_ID=arm64)
expect(epm_osx_architectures "x86_64" CMAKE_SYSTEM_NAME=visionOS
        CMAKE_C_COMPILER_ARCHITECTURE_ID=x86_64)

# macOS and the rest keep an empty value: the platform name uses the processor.
expect(epm_osx_architectures "" CMAKE_SYSTEM_NAME=Darwin
        CMAKE_CXX_COMPILER_ARCHITECTURE_ID=arm64)
expect(epm_osx_architectures "" CMAKE_SYSTEM_NAME=Linux
        CMAKE_CXX_COMPILER_ARCHITECTURE_ID=x86_64)

expect(epm_osx_deployment_target "15.0" CMAKE_SYSTEM_NAME=iOS
        CMAKE_OSX_DEPLOYMENT_TARGET=15.0
        CMAKE_XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET=14.0)
expect(epm_osx_deployment_target "14.0" CMAKE_SYSTEM_NAME=iOS
        CMAKE_XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET=14.0)
expect(epm_osx_deployment_target "2.0" CMAKE_SYSTEM_NAME=visionOS
        CMAKE_XCODE_ATTRIBUTE_XROS_DEPLOYMENT_TARGET=2.0)
expect(epm_osx_deployment_target "" CMAKE_SYSTEM_NAME=Darwin
        CMAKE_XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET=14.0)

# The runtime library on the MSVC ABI only (cl, or clang-cl's MSVC frontend):
# the project's, or CMake's own default when the project left it empty.
expect(epm_msvc_runtime "MultiThreaded$<$<CONFIG:Debug>:Debug>"
        CMAKE_CXX_COMPILER_ID=MSVC
        "CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>")
expect(epm_msvc_runtime "MultiThreadedDLL" CMAKE_CXX_COMPILER_ID=Clang
        CMAKE_CXX_COMPILER_FRONTEND_VARIANT=MSVC
        CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL)
expect(epm_msvc_runtime "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
        CMAKE_CXX_COMPILER_ID=MSVC)
expect(epm_msvc_runtime "" CMAKE_CXX_COMPILER_ID=Clang
        CMAKE_CXX_COMPILER_FRONTEND_VARIANT=GNU
        CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded)
expect(epm_msvc_runtime "" CMAKE_CXX_COMPILER_ID=AppleClang)

foreach (case IN ITEMS "MultiThreaded=static" "MultiThreadedDebug=static"
        "MultiThreadedDLL=dynamic" "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL=dynamic")
    string(REPLACE "=" ";" case "${case}")
    list(GET case 0 runtime)
    list(GET case 1 expected)
    epm_msvc_runtime_kind("${runtime}" actual)

    if (NOT actual STREQUAL expected)
        message(SEND_ERROR "epm_msvc_runtime_kind(${runtime}) is '${actual}', "
                "expected '${expected}'")
        set(failures 1)
    endif ()
endforeach ()

# Android's settings, for `expect`, which passes only the output variable.
foreach (setting IN ITEMS abi api ndk stl)
    cmake_language(EVAL CODE "
        function(epm_android_${setting} out_var)
            epm_android_setting(${setting} value)
            set(\${out_var} \"\${value}\" PARENT_SCOPE)
        endfunction()")
endforeach ()

# CMake's own Android support.
set(cmake_android CMAKE_SYSTEM_NAME=Android CMAKE_ANDROID_ARCH_ABI=arm64-v8a
        CMAKE_SYSTEM_VERSION=26 CMAKE_ANDROID_NDK=/sdk/ndk/30
        CMAKE_ANDROID_STL_TYPE=c++_static)
expect(epm_android_abi "arm64-v8a" ${cmake_android})
expect(epm_android_api "26" ${cmake_android})
expect(epm_android_ndk "/sdk/ndk/30" ${cmake_android})
expect(epm_android_stl "c++_static" ${cmake_android})

# The NDK's legacy toolchain file, whose CMAKE_SYSTEM_VERSION is 1.
set(legacy_toolchain CMAKE_SYSTEM_NAME=Android ANDROID_ABI=x86_64
        CMAKE_SYSTEM_VERSION=1 ANDROID_PLATFORM_LEVEL=28 ANDROID_NDK=/sdk/ndk/30
        ANDROID_STL=c++_shared)
expect(epm_android_abi "x86_64" ${legacy_toolchain})
expect(epm_android_api "28" ${legacy_toolchain})
expect(epm_android_ndk "/sdk/ndk/30" ${legacy_toolchain})
expect(epm_android_stl "c++_shared" ${legacy_toolchain})

# Nothing off Android.
expect(epm_android_api "" CMAKE_SYSTEM_NAME=Darwin CMAKE_SYSTEM_VERSION=25.0.0)
expect(epm_android_abi "" CMAKE_SYSTEM_NAME=Linux ANDROID_ABI=arm64-v8a)

if (failures)
    message(FATAL_ERROR "epm env checks failed")
endif ()

message(STATUS "All epm env checks passed")
