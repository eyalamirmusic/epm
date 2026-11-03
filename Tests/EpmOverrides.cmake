# Self-test of the overrides.json writer in CMake/epm.cmake (Docs/Graph.md §3).
#
#   cmake -P Tests/EpmOverrides.cmake

cmake_minimum_required(VERSION 3.25)

set(EPM_FUNCTIONS_ONLY ON)
include("${CMAKE_CURRENT_LIST_DIR}/../CMake/epm.cmake")

set(failures 0)
set(work_dir "${CMAKE_CURRENT_BINARY_DIR}/epm-overrides-test")
file(REMOVE_RECURSE "${work_dir}")

# Writes overrides.json in a fresh function scope holding only ARGN's
# NAME=VALUE variables (and whatever this file set), and compares it with
# `expected` as JSON.
function(expect_overrides expected)
    foreach (assignment IN LISTS ARGN)
        string(FIND "${assignment}" "=" equals)
        string(SUBSTRING "${assignment}" 0 ${equals} name)
        math(EXPR equals "${equals} + 1")
        string(SUBSTRING "${assignment}" ${equals} -1 value)
        string(REPLACE "|" ";" value "${value}")
        set(${name} "${value}")
    endforeach ()

    epm_write_overrides("${work_dir}/overrides.json")
    file(READ "${work_dir}/overrides.json" actual)
    string(JSON same ERROR_VARIABLE error EQUAL "${actual}" "${expected}")

    if (error OR NOT same)
        message(SEND_ERROR "overrides.json is\n${actual}\nexpected\n${expected}"
                "\n${error}")
        set(failures 1 PARENT_SCOPE)
    endif ()
endfunction()

# Nothing set: every field empty.
expect_overrides([=[{"schema": 1, "profile": "", "mode": "", "configs": [],
    "abiMatch": "", "strict": false, "packages": {}}]=])

# Every field. `|` stands for `;` in the assignments. EPM_<name>_SOURCE is made
# absolute against CMAKE_SOURCE_DIR, with a leading ~ expanded (a shell leaves
# -DEPM_C_SOURCE=~/... alone); an empty value is left out; options split
# at the first `=`, a bare name meaning ON; variables that only look alike
# (EPM_BOOTSTRAP_SOURCE_DIR, epm_fmt_MODE) are not package overrides.
set(CMAKE_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/Graph")
set(b_source "${CMAKE_CURRENT_LIST_DIR}/B")
set(home "$ENV{HOME}")

if (home STREQUAL "" AND CMAKE_HOST_WIN32)
    set(home "$ENV{USERPROFILE}")
endif ()

file(TO_CMAKE_PATH "${home}/Code/C" c_source)
set(expected [=[{
    "schema": 1, "profile": "ci", "mode": "prebuilt", "configs": ["auto"],
    "abiMatch": "strict", "strict": true, "buildMissingBinaries": true,
    "packages": {
        "B": {"mode": "source", "source": "@B_SOURCE@", "configs": [], "options": {}},
        "C": {"mode": "", "source": "@C_SOURCE@", "configs": [], "options": {}},
        "fmt": {"mode": "", "source": "", "configs": ["Debug", "Release"],
                "options": {"FMT_TEST": "OFF", "FMT_NAME": "a \"q\" b=c",
                            "FMT_BARE": "ON"}},
        "my_lib": {"mode": "binary-only", "source": "", "configs": [], "options": {}}
    }
}]=])
string(REPLACE "@B_SOURCE@" "${b_source}" expected "${expected}")
string(REPLACE "@C_SOURCE@" "${c_source}" expected "${expected}")
expect_overrides("${expected}"
        "EPM_PROFILE=ci"
        "EPM_MODE=prebuilt"
        "EPM_CONFIGS=auto"
        "EPM_ABI_MATCH=strict"
        "EPM_STRICT=ON"
        "EPM_fmt_CONFIGS=Debug|Release"
        "EPM_fmt_OPTIONS=FMT_TEST=OFF|FMT_NAME=a \"q\" b=c|FMT_BARE"
        "EPM_B_SOURCE=../B"
        "EPM_B_MODE=source"
        "EPM_C_SOURCE=~/Code/C"
        "EPM_my_lib_MODE=binary-only"
        "EPM_empty_MODE="
        "EPM_BUILD_MISSING_BINARIES=ON"
        "EPM_BOOTSTRAP_SOURCE_DIR=/somewhere"
        "epm_fmt_MODE=prebuilt")

# EPM_STRICT and EPM_BUILD_MISSING_BINARIES off are false.
expect_overrides([=[{"schema": 1, "profile": "", "mode": "", "configs": [],
    "abiMatch": "", "strict": false, "buildMissingBinaries": false,
    "packages": {}}]=]
        "EPM_STRICT=OFF"
        "EPM_BUILD_MISSING_BINARIES=OFF")

if (failures GREATER 0)
    message(FATAL_ERROR "epm_write_overrides checks failed")
endif ()

message(STATUS "All epm_write_overrides checks passed")
