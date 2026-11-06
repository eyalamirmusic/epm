cmake_minimum_required(VERSION 3.31)

project(epm VERSION 0.1.3 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# The configuration names the install's lib/<config>/ and the libraries epm's
# own recipes link, so a single-configuration build of epm always has one.
get_property(EPM_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)

if (PROJECT_IS_TOP_LEVEL AND NOT EPM_MULTI_CONFIG AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "The build configuration" FORCE)
endif ()

if (MSVC)
    add_compile_options(/EHsc)
endif ()

# The static runtime library on the MSVC ABI (cl, clang-cl): epm's default for
# itself and its recipe libraries, and, through epm.cmake, for every project
# using it and that project's dependencies (Docs/Graph.md §2). The policy
# default carries the variable into a dependency whose cmake_minimum_required
# predates it (CMake 3.15), which would otherwise take the DLL runtime.
if (NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif ()

set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)

option(EPM_VERBOSE_CONFIGURE
        "Print the third-party fetch and probe detail hidden by default" OFF)

# Off by default: a unity build collapses the per-file entries of
# compile_commands.json that LSP tooling reads. get_epm.cmake turns it on.
option(EPM_UNITY_BUILD "Compile epm's own targets as CMake unity builds" OFF)
option(EPM_UNITY_BUILD_DEPENDENCIES
        "Compile epm's dependencies as CMake unity builds" OFF)
option(EPM_CI_BUILD "Mirror the exact flag set used by CI builds" OFF)

# CI's flag set, as eacp's EACP_CI_BUILD.
if (EPM_CI_BUILD)
    set(EPM_UNITY_BUILD ON CACHE BOOL
            "Compile epm's own targets as CMake unity builds" FORCE)
    set(EPM_UNITY_BUILD_DEPENDENCIES ON CACHE BOOL
            "Compile epm's dependencies as CMake unity builds" FORCE)
endif ()

# Every dependency with sources to batch: eacp and Miro through their own
# switches, set before their option() calls read them and forced, so turning
# this on in an existing build directory overrides the OFF those calls cached;
# ResEmbed below. fmt, miniz and NanoTest's libraries are one source each, and
# ea_data_structures is header-only.
if (EPM_UNITY_BUILD_DEPENDENCIES)
    set(EACP_UNITY_BUILD ON CACHE BOOL
            "Compile eacp libraries as CMake unity builds" FORCE)
    set(MIRO_UNITY_BUILD ON CACHE BOOL
            "Enable CMake unity build for the Miro library" FORCE)
endif ()

# GCC exempts the main file's own types from -Wsubobject-linkage, and in a
# unity batch every source is an include, so each anonymous-namespace member
# warns. eacp adds the same only in its own directory.
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options(
            "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<BOOL:$<TARGET_PROPERTY:UNITY_BUILD>>>:-Wno-subobject-linkage>")
endif ()

# Same knobs as eacp's ConfigureLog.cmake: keep a clean configure down to CPM's
# one line per package.
if (EPM_VERBOSE_CONFIGURE)
    set(CMAKE_MESSAGE_LOG_LEVEL VERBOSE)
else ()
    set(CMAKE_REQUIRED_QUIET ON)
    set(Threads_FIND_QUIETLY TRUE)
endif ()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMake")

include(CPM)

find_package(eacp REQUIRED)
find_package(Miro REQUIRED)

# eacp adds its helper directory to CMAKE_MODULE_PATH only in its own scope.
list(APPEND CMAKE_MODULE_PATH "${eacp_SOURCE_DIR}/CMake")

add_subdirectory(Lib)
add_subdirectory(Apps)

# The whole install: the binary (Apps/epm), the recipe SDK's headers and
# per-configuration libraries (Lib/epm), and the consumer module, with the
# provider it includes from its own directory. An installed prefix is what
# get_epm.cmake uses: an epm on PATH (a release archive unpacked into a prefix
# is one), else a tool entry of its own, built from source and installed there
# (Debug and Release).
install(FILES CMake/epm.cmake CMake/epm-provider.cmake DESTINATION share/epm/cmake)

option(EPM_ENABLE_TESTS "Enable Tests for epm" ${PROJECT_IS_TOP_LEVEL})

if (EPM_ENABLE_TESTS)
    enable_testing()
    add_subdirectory(Tests)
endif ()

# epm's own targets only: its dependencies are EPM_UNITY_BUILD_DEPENDENCIES'.
if (EPM_UNITY_BUILD)
    set(epm_unity_targets epm-recipe epm)

    if (TARGET epmTests)
        list(APPEND epm_unity_targets epmTests)
    endif ()

    set_target_properties(${epm_unity_targets} PROPERTIES UNITY_BUILD ON)
endif ()

# ResEmbed (through eacp-core and Miro) has no unity switch of its own.
if (EPM_UNITY_BUILD_DEPENDENCIES)
    set_target_properties(ResEmbed PROPERTIES UNITY_BUILD ON)
endif ()
