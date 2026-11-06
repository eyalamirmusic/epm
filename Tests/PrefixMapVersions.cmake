# Self-test of epm-export.cmake's -ffile-prefix-map guard: a child build gets
# the option only from a compiler that takes it (GCC 8, Clang 10, AppleClang
# 12, any IntelLLVM). Configures a one-file C++ project with epm-export.cmake
# injected as epm injects it, then reads the compile command:
#
#   recent    the real compiler: the option is there (when it is GCC or Clang)
#   old       the same compiler with CMAKE_CXX_COMPILER_VERSION set to 1.0
#             after project(), as an old one reports itself: the option is
#             left out, so the build does not fail on it
#
#   cmake [-DWORK_DIR=<dir>] [-DTEST_GENERATOR=<generator>]
#         [-DTEST_MAKE_PROGRAM=<path>] [-DTEST_CXX_COMPILER=<path>]
#         -P Tests/PrefixMapVersions.cmake

cmake_minimum_required(VERSION 3.25)

get_filename_component(repo_dir "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

if (NOT WORK_DIR)
    set(WORK_DIR "${CMAKE_CURRENT_BINARY_DIR}/epm-prefix-map-versions")
endif ()

# compile_commands.json comes only from the Makefile and Ninja generators.
if (TEST_GENERATOR AND NOT TEST_GENERATOR MATCHES "^(Ninja|Unix Makefiles)$")
    message(STATUS "epm-prefix-map-versions: skipped: no compile commands from "
            "${TEST_GENERATOR}")
    return()
endif ()

file(REMOVE_RECURSE "${WORK_DIR}")
set(project "${WORK_DIR}/project")
set(scenario "setup")
set(prefix_map "-ffile-prefix-map=/epm-old=/epm-new")

function(fail text)
    message(FATAL_ERROR "epm-prefix-map-versions: ${scenario}: ${text}")
endfunction()

file(WRITE "${project}/CMakeLists.txt" [=[
cmake_minimum_required(VERSION 3.25)
project(PrefixMapVersions CXX)

# What an old compiler reports; generator expressions read it at generate time.
if (FAKE_CXX_VERSION)
    set(CMAKE_CXX_COMPILER_VERSION "${FAKE_CXX_VERSION}")
endif ()

add_library(prefix_map STATIC Lib.cpp)
]=])
file(WRITE "${project}/Lib.cpp" "int prefixMapValue()\n{\n    return 1;\n}\n")

set(common_args
        "-DCMAKE_PROJECT_TOP_LEVEL_INCLUDES=${repo_dir}/CMake/epm-export.cmake"
        "-DEPM_EXPORT_FILE=${WORK_DIR}/export.cmake"
        -DEPM_EXPORT_NAMESPACE=epm_test::
        "-DEPM_FILE_PREFIX_MAPS=/epm-old=/epm-new"
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON)

if (TEST_GENERATOR)
    list(APPEND common_args -G "${TEST_GENERATOR}")
endif ()

if (TEST_MAKE_PROGRAM)
    list(APPEND common_args "-DCMAKE_MAKE_PROGRAM=${TEST_MAKE_PROGRAM}")
endif ()

if (TEST_CXX_COMPILER)
    list(APPEND common_args "-DCMAKE_CXX_COMPILER=${TEST_CXX_COMPILER}")
endif ()

# Configures the project into ${WORK_DIR}/<name>; ARGN goes to cmake. Sets
# `compile_commands` and `compiler_id`.
function(configure name)
    set(scenario "${name}" PARENT_SCOPE)
    set(scenario "${name}")
    set(build "${WORK_DIR}/${name}")
    execute_process(
            COMMAND "${CMAKE_COMMAND}" -S "${project}" -B "${build}" ${common_args}
            ${ARGN}
            RESULT_VARIABLE result
            OUTPUT_VARIABLE output
            ERROR_VARIABLE output)

    if (NOT result EQUAL 0)
        fail("configure failed (${result}):\n${output}")
    endif ()

    file(READ "${build}/compile_commands.json" commands)
    file(STRINGS "${build}/CMakeCache.txt" id REGEX "^CMAKE_CXX_COMPILER_ID:")
    string(REGEX REPLACE "^[^=]*=" "" id "${id}")

    # The cache does not keep the compiler id; the compiler file does.
    if (NOT id)
        file(GLOB compiler_files "${build}/CMakeFiles/*/CMakeCXXCompiler.cmake")
        list(GET compiler_files 0 compiler_file)
        file(STRINGS "${compiler_file}" id REGEX "^set\\(CMAKE_CXX_COMPILER_ID ")
        string(REGEX REPLACE "^set\\(CMAKE_CXX_COMPILER_ID \"([^\"]*)\"\\)$" "\\1"
                id "${id}")
    endif ()

    set(compile_commands "${commands}" PARENT_SCOPE)
    set(compiler_id "${id}" PARENT_SCOPE)
endfunction()

configure(recent)

if (NOT compiler_id MATCHES "^(GNU|Clang|AppleClang|IntelLLVM)$")
    message(STATUS "epm-prefix-map-versions: skipped: the compiler is "
            "'${compiler_id}', which gets no prefix maps")
    return()
endif ()

string(FIND "${compile_commands}" "${prefix_map}" at)

if (at EQUAL -1)
    fail("${compiler_id}'s compile command lacks ${prefix_map}:\n${compile_commands}")
endif ()

configure(old -DFAKE_CXX_VERSION=1.0)
string(FIND "${compile_commands}" "-ffile-prefix-map" at)

if (NOT at EQUAL -1)
    fail("${compiler_id} 1.0 still gets -ffile-prefix-map:\n${compile_commands}")
endif ()

message(STATUS "All prefix map version checks passed")
