# Self-test of env.json's Apple architecture and deployment-target fallbacks in
# CMake/epm.cmake (epm_osx_architectures, epm_osx_deployment_target), of its SDK
# directory and version (epm_osx_sysroot_path, epm_sdk_version, the NDK's
# revision on Android), of its C compiler (epm_c_driver_beside, epm_c_compiler),
# of its generator platform (epm_generator_platform), of its MSVC runtime
# library (epm_msvc_runtime) and of its Android settings (epm_android_setting).
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

set(deployment_variables MACOSX_DEPLOYMENT_TARGET IPHONEOS_DEPLOYMENT_TARGET)

foreach (variable IN LISTS deployment_variables)
    set(saved_${variable} "$ENV{${variable}}")
    unset(ENV{${variable}})
endforeach ()

expect(epm_osx_deployment_target "15.0" CMAKE_SYSTEM_NAME=iOS
        CMAKE_OSX_DEPLOYMENT_TARGET=15.0
        CMAKE_XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET=14.0)
expect(epm_osx_deployment_target "14.0" CMAKE_SYSTEM_NAME=iOS
        CMAKE_XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET=14.0)
expect(epm_osx_deployment_target "2.0" CMAKE_SYSTEM_NAME=visionOS
        CMAKE_XCODE_ATTRIBUTE_XROS_DEPLOYMENT_TARGET=2.0)
expect(epm_osx_deployment_target "" CMAKE_SYSTEM_NAME=Darwin
        CMAKE_XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET=14.0)

# Else the system's variable from the environment, which clang would read: the
# key and the child builds get the target the compiler actually uses.
set(ENV{MACOSX_DEPLOYMENT_TARGET} 12.3)
set(ENV{IPHONEOS_DEPLOYMENT_TARGET} 16.4)
expect(epm_osx_deployment_target "12.3" CMAKE_SYSTEM_NAME=Darwin)
expect(epm_osx_deployment_target "11.0" CMAKE_SYSTEM_NAME=Darwin
        CMAKE_OSX_DEPLOYMENT_TARGET=11.0)
expect(epm_osx_deployment_target "16.4" CMAKE_SYSTEM_NAME=iOS)
expect(epm_osx_deployment_target "14.0" CMAKE_SYSTEM_NAME=iOS
        CMAKE_XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET=14.0)
expect(epm_osx_deployment_target "" CMAKE_SYSTEM_NAME=tvOS)
expect(epm_osx_deployment_target "" CMAKE_SYSTEM_NAME=Linux)

foreach (variable IN LISTS deployment_variables)
    if (saved_${variable} STREQUAL "")
        unset(ENV{${variable}})
    else ()
        set(ENV{${variable}} "${saved_${variable}}")
    endif ()
endforeach ()

# The SDK version: an Apple SDK's SDKSettings.json, found where CMake resolved
# it, else at CMAKE_OSX_SYSROOT as a path.
set(fake_sdk "${CMAKE_CURRENT_BINARY_DIR}/epm-env-fake/MacOSX15.2.sdk")
set(no_settings_sdk "${CMAKE_CURRENT_BINARY_DIR}/epm-env-fake/Empty.sdk")
file(REMOVE_RECURSE "${CMAKE_CURRENT_BINARY_DIR}/epm-env-fake")
file(WRITE "${fake_sdk}/SDKSettings.json"
        "{\"CanonicalName\": \"macosx15.2\", \"Version\": \"15.2\"}")
file(MAKE_DIRECTORY "${no_settings_sdk}")

expect(epm_sdk_version "15.2" CMAKE_SYSTEM_NAME=Darwin
        "_CMAKE_OSX_SYSROOT_PATH=${fake_sdk}" CMAKE_OSX_SYSROOT=macosx)
expect(epm_sdk_version "15.2" CMAKE_SYSTEM_NAME=iOS "CMAKE_OSX_SYSROOT=${fake_sdk}")
expect(epm_sdk_version "" CMAKE_SYSTEM_NAME=Darwin
        "CMAKE_OSX_SYSROOT=${no_settings_sdk}")
expect(epm_sdk_version "" CMAKE_SYSTEM_NAME=Linux "CMAKE_OSX_SYSROOT=${fake_sdk}")

# The SDK directory child builds get as their sysroot: the same one, only for
# an Apple system and only when it exists.
expect(epm_osx_sysroot_path "${fake_sdk}" CMAKE_SYSTEM_NAME=Darwin
        "_CMAKE_OSX_SYSROOT_PATH=${fake_sdk}")
expect(epm_osx_sysroot_path "${fake_sdk}" CMAKE_SYSTEM_NAME=iOS
        CMAKE_OSX_SYSROOT=iphonesimulator "_CMAKE_OSX_SYSROOT_PATH=${fake_sdk}")
expect(epm_osx_sysroot_path "${fake_sdk}" CMAKE_SYSTEM_NAME=Darwin
        "CMAKE_OSX_SYSROOT=${fake_sdk}")
expect(epm_osx_sysroot_path "" CMAKE_SYSTEM_NAME=Darwin
        "_CMAKE_OSX_SYSROOT_PATH=${fake_sdk}-missing")
expect(epm_osx_sysroot_path "" CMAKE_SYSTEM_NAME=Linux
        "_CMAKE_OSX_SYSROOT_PATH=${fake_sdk}")

# With neither, xcrun names the default SDK, when there is one.
if (CMAKE_HOST_APPLE)
    find_program(xcrun_program xcrun)

    if (xcrun_program)
        set(CMAKE_SYSTEM_NAME Darwin)
        epm_sdk_version(default_version)
        epm_osx_sysroot_path(default_sysroot)
        unset(CMAKE_SYSTEM_NAME)

        if (NOT default_version MATCHES "^[0-9]+(\\.[0-9]+)*$")
            message(SEND_ERROR "epm_sdk_version of the default SDK is "
                    "'${default_version}', expected a version")
            set(failures 1)
        endif ()

        if (NOT IS_DIRECTORY "${default_sysroot}")
            message(SEND_ERROR "epm_osx_sysroot_path of the default SDK is "
                    "'${default_sysroot}', expected a directory")
            set(failures 1)
        endif ()
    endif ()
endif ()

# The C compiler: the project's, else on the MSVC ABI the C++ compiler, else the
# C driver beside the C++ one.
set(fake_bin "${CMAKE_CURRENT_BINARY_DIR}/epm-env-fake/bin")
set(fake_alt "${CMAKE_CURRENT_BINARY_DIR}/epm-env-fake/alt")

foreach (tool IN ITEMS clang++ clang x86_64-linux-gnu-g++-13
        x86_64-linux-gnu-gcc-13 g++-13 gcc-13 cc icpx icx em++ emcc
        clang++.exe clang.exe lonely-c++ weird++)
    file(WRITE "${fake_bin}/${tool}" "")
endforeach ()

function(expect_driver cxx expected)
    epm_c_driver_beside("${cxx}" actual)

    if (NOT actual STREQUAL expected)
        message(SEND_ERROR "epm_c_driver_beside(${cxx}) is '${actual}', expected "
                "'${expected}'")
        set(failures 1 PARENT_SCOPE)
    endif ()
endfunction()

expect_driver("${fake_bin}/clang++" "${fake_bin}/clang")
expect_driver("${fake_bin}/clang++.exe" "${fake_bin}/clang.exe")
expect_driver("${fake_bin}/x86_64-linux-gnu-g++-13"
        "${fake_bin}/x86_64-linux-gnu-gcc-13")
expect_driver("${fake_bin}/icpx" "${fake_bin}/icx")
expect_driver("${fake_bin}/em++" "${fake_bin}/emcc")
expect_driver("${fake_bin}/lonely-c++" "")
expect_driver("${fake_bin}/weird++" "")
expect_driver("" "")

# A c++ that links (via an alternatives directory) to g++-13 gets gcc-13, not
# the cc beside the link; skipped where symlinks cannot be made.
file(MAKE_DIRECTORY "${fake_alt}")
file(CREATE_LINK "${fake_bin}/g++-13" "${fake_alt}/c++" RESULT alt_link SYMBOLIC)
file(CREATE_LINK "../alt/c++" "${fake_bin}/c++" RESULT bin_link SYMBOLIC)

if (alt_link EQUAL 0 AND bin_link EQUAL 0)
    expect_driver("${fake_bin}/c++" "${fake_bin}/gcc-13")
endif ()

expect(epm_c_compiler "/opt/cc" CMAKE_C_COMPILER=/opt/cc
        "CMAKE_CXX_COMPILER=${fake_bin}/clang++" CMAKE_CXX_COMPILER_ID=Clang)
expect(epm_c_compiler "${fake_bin}/clang" "CMAKE_CXX_COMPILER=${fake_bin}/clang++"
        CMAKE_CXX_COMPILER_ID=Clang)
expect(epm_c_compiler "C:/VS/bin/cl.exe" CMAKE_CXX_COMPILER=C:/VS/bin/cl.exe
        CMAKE_CXX_COMPILER_ID=MSVC)
expect(epm_c_compiler "C:/LLVM/bin/clang-cl.exe"
        CMAKE_CXX_COMPILER=C:/LLVM/bin/clang-cl.exe CMAKE_CXX_COMPILER_ID=Clang
        CMAKE_CXX_COMPILER_FRONTEND_VARIANT=MSVC)
expect(epm_c_compiler "" "CMAKE_CXX_COMPILER=${fake_bin}/weird++"
        CMAKE_CXX_COMPILER_ID=GNU)
expect(epm_c_compiler "")

# A Visual Studio generator's platform, as given or as it picked by default.
expect(epm_generator_platform "ARM64" "CMAKE_GENERATOR=Visual Studio 17 2022"
        CMAKE_GENERATOR_PLATFORM=ARM64 CMAKE_VS_PLATFORM_NAME=ARM64)
expect(epm_generator_platform "x64" "CMAKE_GENERATOR=Visual Studio 17 2022"
        CMAKE_VS_PLATFORM_NAME=x64)
expect(epm_generator_platform "" CMAKE_GENERATOR=Ninja)

file(REMOVE_RECURSE "${CMAKE_CURRENT_BINARY_DIR}/epm-env-fake")

# Windows: the Visual Studio generator's SDK, else a developer prompt's.
set(saved_sdk_version "$ENV{WindowsSDKVersion}")
set(ENV{WindowsSDKVersion} "10.0.19041.0\\")
expect(epm_sdk_version "10.0.22621.0" CMAKE_SYSTEM_NAME=Windows
        CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION=10.0.22621.0)
expect(epm_sdk_version "10.0.19041.0" CMAKE_SYSTEM_NAME=Windows)
set(ENV{WindowsSDKVersion} "")
expect(epm_sdk_version "" CMAKE_SYSTEM_NAME=Windows)
set(ENV{WindowsSDKVersion} "${saved_sdk_version}")

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

# The NDK counts by its revision, never by its directory: its source.properties
# Pkg.Revision, through either way of naming it.
set(fake_ndk "${CMAKE_CURRENT_BINARY_DIR}/epm-env-fake/ndk/27.0.12077973")
file(WRITE "${fake_ndk}/source.properties"
        "Pkg.Desc = Android NDK\nPkg.Revision = 27.0.12077973\n")
expect(epm_sdk_version "27.0.12077973" CMAKE_SYSTEM_NAME=Android
        "CMAKE_ANDROID_NDK=${fake_ndk}")
expect(epm_sdk_version "27.0.12077973" CMAKE_SYSTEM_NAME=Android
        "ANDROID_NDK=${fake_ndk}")
expect(epm_sdk_version "" CMAKE_SYSTEM_NAME=Android
        "CMAKE_ANDROID_NDK=${CMAKE_CURRENT_BINARY_DIR}/epm-env-fake/no-ndk")
expect(epm_sdk_version "" CMAKE_SYSTEM_NAME=Linux "ANDROID_NDK=${fake_ndk}")
file(REMOVE_RECURSE "${CMAKE_CURRENT_BINARY_DIR}/epm-env-fake")

if (failures)
    message(FATAL_ERROR "epm env checks failed")
endif ()

message(STATUS "All epm env checks passed")
