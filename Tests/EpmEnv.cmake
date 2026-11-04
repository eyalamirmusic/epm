# Self-test of env.json's Apple architecture and deployment-target fallbacks in
# CMake/epm.cmake (epm_osx_architectures, epm_osx_deployment_target).
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

if (failures)
    message(FATAL_ERROR "epm env checks failed")
endif ()

message(STATUS "All epm env checks passed")
