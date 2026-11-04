# Self-test of the EPM_SOURCE_DIR decisions in CMake/get_epm.cmake: which
# directories are epm checkouts, where the setting comes from (including the
# default to the checkout get_epm.cmake sits in, and the deprecated
# EPM_BOOTSTRAP_SOURCE_DIR), the tool entry a checkout maps to, the generator
# picked for its build and the consumer toolchain passed to it. Nothing is
# built.
#
#   cmake -DWORK_DIR=<scratch dir> -P Tests/GetEpmSourceDir.cmake

set(GET_EPM_FUNCTIONS_ONLY ON)
include("${CMAKE_CURRENT_LIST_DIR}/../CMake/get_epm.cmake")

if (NOT WORK_DIR)
    set(WORK_DIR "${CMAKE_CURRENT_BINARY_DIR}/get-epm-source-dir")
endif ()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")
file(REAL_PATH "${CMAKE_CURRENT_LIST_DIR}/.." repo)
set(failures 0)

macro(fail)
    string(JOIN "" text ${ARGN})
    message(SEND_ERROR "${text}")
    math(EXPR failures "${failures} + 1")
endmacro()

macro(expect_checkout dir expected_dir)
    get_epm_check_source_dir("${dir}")

    if (NOT source_error STREQUAL "" OR NOT source_dir STREQUAL "${expected_dir}")
        fail("'${dir}': source_dir '${source_dir}', error '${source_error}'; "
                "expected '${expected_dir}'")
    endif ()
endmacro()

macro(expect_rejected dir expected_error)
    get_epm_check_source_dir("${dir}")

    if (NOT source_error MATCHES "${expected_error}")
        fail("'${dir}': error '${source_error}', expected one matching "
                "'${expected_error}'")
    endif ()
endmacro()

function(write_lists name content)
    file(WRITE "${WORK_DIR}/${name}/CMakeLists.txt" "${content}")
endfunction()

# This repository is a checkout, however it is spelled.
expect_checkout("${repo}" "${repo}")
expect_checkout("${CMAKE_CURRENT_LIST_DIR}/.." "${repo}")
expect_checkout("${repo}/Tests/../" "${repo}")
file(CREATE_LINK "${repo}" "${WORK_DIR}/link" SYMBOLIC RESULT link_result)

if (link_result STREQUAL "0")
    expect_checkout("${WORK_DIR}/link" "${repo}")
endif ()

# project(epm in its usual spellings.
write_lists(plain "cmake_minimum_required(VERSION 3.31)\nproject(epm)\n")
write_lists(versioned "project(epm VERSION 0.2.0 LANGUAGES C CXX)\n")
write_lists(spaced "  PROJECT ( epm\n        VERSION 1.0)\n")
write_lists(split "project(epm\n    VERSION 1.0)\n")
expect_checkout("${WORK_DIR}/plain" "${WORK_DIR}/plain")
expect_checkout("${WORK_DIR}/versioned" "${WORK_DIR}/versioned")
expect_checkout("${WORK_DIR}/spaced" "${WORK_DIR}/spaced")
expect_checkout("${WORK_DIR}/split" "${WORK_DIR}/split")

# Anything else is rejected, with the reason.
write_lists(other "project(HelloGraph CXX)\n")
write_lists(prefix "project(epmx)\n")
write_lists(commented "# project(epm)\nproject(other)\n")
file(MAKE_DIRECTORY "${WORK_DIR}/empty")
file(WRITE "${WORK_DIR}/file.txt" "")
expect_rejected("${WORK_DIR}/missing" "is not a directory")
expect_rejected("${WORK_DIR}/file.txt" "is not a directory")
expect_rejected("${WORK_DIR}/empty" "no CMakeLists.txt")
expect_rejected("${WORK_DIR}/other" "has no project.epm")
expect_rejected("${WORK_DIR}/prefix" "has no project.epm")
expect_rejected("${WORK_DIR}/commented" "has no project.epm")

# A relative path is taken from the top-level source directory.
set(CMAKE_SOURCE_DIR "${repo}/Tests")
expect_checkout("../" "${repo}")
expect_rejected("Graph" "no CMakeLists.txt")

# The variable wins over the environment.
set(ENV{EPM_SOURCE_DIR} "/from/env")
unset(EPM_SOURCE_DIR)
get_epm_source_dir_setting(setting)

if (NOT setting STREQUAL "/from/env")
    fail("EPM_SOURCE_DIR from the environment is '${setting}'")
endif ()

set(EPM_SOURCE_DIR "/from/variable")
get_epm_source_dir_setting(setting)

if (NOT setting STREQUAL "/from/variable")
    fail("EPM_SOURCE_DIR from the variable is '${setting}'")
endif ()

unset(EPM_SOURCE_DIR)
set(ENV{EPM_SOURCE_DIR} "")
get_epm_source_dir_setting(setting)

# Unset, it defaults to the checkout this get_epm.cmake belongs to.
if (NOT setting STREQUAL "${repo}" OR NOT setting_origin STREQUAL "enclosing")
    fail("unset EPM_SOURCE_DIR is '${setting}' (${setting_origin}), expected "
            "'${repo}' (enclosing)")
endif ()

# The checkout a copy of get_epm.cmake belongs to: <dir> for <dir>/CMake/
# get_epm.cmake when <dir>/CMakeLists.txt has project(epm), else none. Each case
# runs a copy in a fresh `cmake -P`, with no EPM_SOURCE_DIR in the environment
# unless given.
file(WRITE "${WORK_DIR}/probe.cmake" [=[
set(GET_EPM_FUNCTIONS_ONLY ON)
include("${GET_EPM}")
get_epm_source_dir_setting(setting)
message("setting=[${setting}] origin=[${setting_origin}]")
]=])

macro(expect_setting name lists expected_setting expected_origin)
    set(dir "${WORK_DIR}/${name}")
    file(MAKE_DIRECTORY "${dir}/CMake")
    file(WRITE "${dir}/CMakeLists.txt" "${lists}")
    file(COPY "${CMAKE_CURRENT_LIST_DIR}/../CMake/get_epm.cmake"
            DESTINATION "${dir}/CMake")
    file(REAL_PATH "${dir}" real_dir)
    string(REPLACE "<dir>" "${real_dir}" expected "${expected_setting}")
    execute_process(
            COMMAND "${CMAKE_COMMAND}" -E env --unset=EPM_SOURCE_DIR ${env_args}
            "${CMAKE_COMMAND}" "-DGET_EPM=${dir}/CMake/get_epm.cmake" ${ARGN}
            -P "${WORK_DIR}/probe.cmake"
            RESULT_VARIABLE result
            OUTPUT_VARIABLE output
            ERROR_VARIABLE output)
    string(STRIP "${output}" output)
    set(expected "setting=[${expected}] origin=[${expected_origin}]")

    if (NOT result EQUAL 0 OR NOT output STREQUAL expected)
        fail("${name} ${ARGN}: '${output}' (${result}), expected '${expected}'")
    endif ()
endmacro()

set(epm_lists "cmake_minimum_required(VERSION 3.31)\nproject(epm VERSION 9.9.9)\n")
set(app_lists "cmake_minimum_required(VERSION 3.25)\nproject(App CXX)\n")
set(env_args "")

expect_setting(inside "${epm_lists}" "<dir>" enclosing)
expect_setting(vendored "${app_lists}" "" "")

# Anything that asks for an epm wins over the default.
expect_setting(inside "${epm_lists}" "/explicit" variable -DEPM_SOURCE_DIR=/explicit)
expect_setting(inside "${epm_lists}" "" "" -DEPM_EXECUTABLE=/user/epm)
expect_setting(inside "${epm_lists}" "" "" -DEPM_SOURCE_REF=main)
expect_setting(inside "${epm_lists}" "" "" -DEPM_SOURCE_URL=https://x/epm.tar.gz)
set(env_args EPM_SOURCE_DIR=/from/env)
expect_setting(inside "${epm_lists}" "/from/env" environment)
expect_setting(vendored "${app_lists}" "/from/env" environment)
set(env_args "")

# An EPM_EXECUTABLE get_epm picked on an earlier configure is not the user's.
expect_setting(inside "${epm_lists}" "<dir>" enclosing
        -DEPM_EXECUTABLE=/picked/epm -DEPM_SELECTED_EXECUTABLE=/picked/epm)
expect_setting(inside "${epm_lists}" "<dir>" enclosing
        -DEPM_EXECUTABLE=/old/epm -DEPM_BOOTSTRAPPED_EXECUTABLE=/old/epm)

# The deprecated EPM_BOOTSTRAP_SOURCE_DIR is EPM_SOURCE_DIR when that is unset.
expect_setting(vendored "${app_lists}" "/old/name" deprecated
        -DEPM_BOOTSTRAP_SOURCE_DIR=/old/name)
expect_setting(inside "${epm_lists}" "/old/name" deprecated
        -DEPM_BOOTSTRAP_SOURCE_DIR=/old/name)
expect_setting(vendored "${app_lists}" "/new/name" variable
        -DEPM_BOOTSTRAP_SOURCE_DIR=/old/name -DEPM_SOURCE_DIR=/new/name)
set(env_args EPM_SOURCE_DIR=/from/env)
expect_setting(vendored "${app_lists}" "/from/env" environment
        -DEPM_BOOTSTRAP_SOURCE_DIR=/old/name)
set(env_args "")

# One entry per checkout: local-<first 12 hex digits of sha256(real path)>.
string(SHA256 hash "${repo}")
string(SUBSTRING "${hash}" 0 12 hash)
get_epm_source_entry("/cache" "${repo}" entry)

if (NOT entry STREQUAL "/cache/tool/local-${hash}")
    fail("entry of ${repo} is '${entry}', expected /cache/tool/local-${hash}")
endif ()

get_epm_source_entry("/cache" "${WORK_DIR}/plain" other_entry)

if (other_entry STREQUAL entry OR NOT other_entry MATCHES "^/cache/tool/local-[0-9a-f]+$")
    fail("entry of ${WORK_DIR}/plain is '${other_entry}'")
endif ()

# The generator of the epm build: Ninja when there is one, else Unix
# Makefiles; on a Windows host without a ninja, or for a Visual Studio consumer
# outside a developer prompt there, the consumer's.
macro(expect_generator consumer windows msvc_env ninja expected)
    get_epm_pick_generator("${consumer}" "${windows}" ${msvc_env} "${ninja}")

    if (NOT tool_generator STREQUAL "${expected}")
        fail("generator for '${consumer}' (windows '${windows}', msvc env "
                "${msvc_env}, ninja '${ninja}') is '${tool_generator}', expected "
                "'${expected}'")
    endif ()
endmacro()

foreach (consumer IN ITEMS Xcode Ninja "Ninja Multi-Config" "Unix Makefiles"
        "Visual Studio 17 2022" "")
    expect_generator("${consumer}" "" FALSE /bin/ninja Ninja)
    expect_generator("${consumer}" "" FALSE "" "Unix Makefiles")
endforeach ()

expect_generator(Ninja 1 TRUE C:/ninja.exe Ninja)
expect_generator("Visual Studio 17 2022" 1 TRUE C:/ninja.exe Ninja)
expect_generator("Visual Studio 17 2022" 1 FALSE C:/ninja.exe "Visual Studio 17 2022")
expect_generator("Visual Studio 17 2022" 1 TRUE "" "Visual Studio 17 2022")
expect_generator("NMake Makefiles" 1 TRUE "" "NMake Makefiles")
expect_generator("" 1 FALSE "" "")

# The epm build's generator, with a build directory of its own; the consumer's
# compilers too, unless they may be a cross toolchain's. Without a native
# consumer, get_epm_run_step also drops CC, CXX and the flags from the
# environment.
macro(expect_toolchain expected_name)
    get_epm_consumer_toolchain()
    set(expected_args ${ARGN})

    if (NOT build_name STREQUAL "${expected_name}"
            OR NOT toolchain_args STREQUAL "${expected_args}")
        fail("consumer toolchain for '${CMAKE_GENERATOR}' is '${toolchain_args}' "
                "in '${build_name}', expected '${expected_args}' in "
                "'${expected_name}'")
    endif ()
endmacro()

# What this host picks for a consumer whose generator is not a Ninja one.
set(ENV{VCINSTALLDIR} "C:/VS/VC/")
find_program(host_ninja NAMES ninja ninja-build NO_CACHE NO_CMAKE_FIND_ROOT_PATH)
set(host_known TRUE)

if (host_ninja)
    set(host_name build-ninja)
    set(host -G Ninja "-DCMAKE_MAKE_PROGRAM=${host_ninja}")
elseif (NOT CMAKE_HOST_WIN32)
    set(host_name build-unix-makefiles)
    set(host -G "Unix Makefiles")
else ()
    set(host_known FALSE)
endif ()

if (host_known)
    expect_toolchain(${host_name} ${host})
    set(CMAKE_CXX_COMPILER /usr/bin/c++)
    expect_toolchain(${host_name} ${host} -DCMAKE_CXX_COMPILER=/usr/bin/c++)

    # Xcode and Visual Studio consumers: neither their generator nor their
    # make program (xcodebuild, MSBuild) nor their toolset reach the build.
    set(CMAKE_GENERATOR Xcode)
    set(CMAKE_MAKE_PROGRAM /usr/bin/xcodebuild)
    expect_toolchain(${host_name} ${host} -DCMAKE_CXX_COMPILER=/usr/bin/c++)
    set(CMAKE_SYSTEM_NAME iOS)
    expect_toolchain(${host_name} ${host})
    unset(CMAKE_SYSTEM_NAME)
    set(CMAKE_GENERATOR "Visual Studio 17 2022")
    set(CMAKE_GENERATOR_TOOLSET ClangCL)
    set(CMAKE_MAKE_PROGRAM "C:/VS/MSBuild.exe")
    expect_toolchain(${host_name} ${host} -DCMAKE_CXX_COMPILER=/usr/bin/c++)
    unset(CMAKE_GENERATOR_TOOLSET)
endif ()

# A Ninja consumer's own ninja is the one used, its build directory named as
# before; Ninja Multi-Config gives way to Ninja.
set(CMAKE_CXX_COMPILER /usr/bin/c++)
set(CMAKE_GENERATOR Ninja)
set(CMAKE_MAKE_PROGRAM /usr/bin/ninja)
set(native -G Ninja -DCMAKE_MAKE_PROGRAM=/usr/bin/ninja)
expect_toolchain(build-ninja ${native} -DCMAKE_CXX_COMPILER=/usr/bin/c++)
set(CMAKE_SYSTEM_NAME "${CMAKE_HOST_SYSTEM_NAME}")
expect_toolchain(build-ninja ${native} -DCMAKE_CXX_COMPILER=/usr/bin/c++)
set(CMAKE_SYSTEM_NAME iOS)
expect_toolchain(build-ninja ${native})
unset(CMAKE_SYSTEM_NAME)
set(CMAKE_TOOLCHAIN_FILE /some/toolchain.cmake)
expect_toolchain(build-ninja ${native})
unset(CMAKE_TOOLCHAIN_FILE)
set(CMAKE_CROSSCOMPILING TRUE)
expect_toolchain(build-ninja ${native})

# The linker type follows the compilers; a launcher (ccache) always reaches
# the build, unless it is a list.
set(CMAKE_LINKER_TYPE LLD)
set(CMAKE_CXX_COMPILER_LAUNCHER ccache)
expect_toolchain(build-ninja ${native} -DCMAKE_CXX_COMPILER_LAUNCHER=ccache)
unset(CMAKE_CROSSCOMPILING)
expect_toolchain(build-ninja ${native} -DCMAKE_CXX_COMPILER=/usr/bin/c++
        -DCMAKE_LINKER_TYPE=LLD -DCMAKE_CXX_COMPILER_LAUNCHER=ccache)
set(CMAKE_CXX_COMPILER_LAUNCHER "ccache;--some-option")
expect_toolchain(build-ninja ${native} -DCMAKE_CXX_COMPILER=/usr/bin/c++
        -DCMAKE_LINKER_TYPE=LLD)
unset(CMAKE_LINKER_TYPE)
unset(CMAKE_CXX_COMPILER_LAUNCHER)
set(CMAKE_GENERATOR "Ninja Multi-Config")
expect_toolchain(build-ninja ${native} -DCMAKE_CXX_COMPILER=/usr/bin/c++)
unset(CMAKE_MAKE_PROGRAM)

# The consumer's generator, toolset and make program are kept where it is the
# one picked: a Visual Studio consumer on Windows outside a developer prompt.
if (CMAKE_HOST_WIN32)
    set(ENV{VCINSTALLDIR} "")
    set(CMAKE_GENERATOR "Visual Studio 17 2022")
    set(CMAKE_GENERATOR_TOOLSET ClangCL)
    set(CMAKE_MAKE_PROGRAM "C:/VS/MSBuild.exe")
    expect_toolchain(build-visual-studio-17-2022 -G "Visual Studio 17 2022"
            -T ClangCL "-DCMAKE_MAKE_PROGRAM=C:/VS/MSBuild.exe"
            -DCMAKE_CXX_COMPILER=/usr/bin/c++)
    unset(CMAKE_GENERATOR_TOOLSET)
    unset(CMAKE_MAKE_PROGRAM)
endif ()

unset(ENV{VCINSTALLDIR})

# A cross consumer's compiler environment (a Yocto-style SDK) stays out of the
# build; a native consumer's reaches it.
macro(expect_build_env expected_cc)
    set(env_log "${WORK_DIR}/env.log")
    file(WRITE "${env_log}" "")
    get_epm_run_step(probe "${env_log}" "${CMAKE_COMMAND}" -E environment)
    file(STRINGS "${env_log}" cc_lines REGEX "^CC=")

    if (NOT "${cc_lines}" STREQUAL "${expected_cc}")
        fail("build environment has '${cc_lines}', expected '${expected_cc}'")
    endif ()
endmacro()

set(ENV{CC} /cross/bin/cc)
set(CMAKE_TOOLCHAIN_FILE /some/toolchain.cmake)
expect_build_env("")
unset(CMAKE_TOOLCHAIN_FILE)
expect_build_env("CC=/cross/bin/cc")
unset(ENV{CC})

# The SDK and deployment targets an Xcode build exports never reach it.
set(ENV{SDKROOT} /iPhoneOS.sdk)
set(ENV{MACOSX_DEPLOYMENT_TARGET} 10.9)
set(ENV{IPHONEOS_DEPLOYMENT_TARGET} 15.0)
set(env_log "${WORK_DIR}/env.log")
file(WRITE "${env_log}" "")
get_epm_run_step(probe "${env_log}" "${CMAKE_COMMAND}" -E environment)
file(STRINGS "${env_log}" xcode_lines
        REGEX "^(SDKROOT|MACOSX_DEPLOYMENT_TARGET|IPHONEOS_DEPLOYMENT_TARGET)=")

if (NOT "${xcode_lines}" STREQUAL "")
    fail("build environment has '${xcode_lines}'")
endif ()

unset(ENV{SDKROOT})
unset(ENV{MACOSX_DEPLOYMENT_TARGET})
unset(ENV{IPHONEOS_DEPLOYMENT_TARGET})

file(REMOVE_RECURSE "${WORK_DIR}")

if (failures GREATER 0)
    message(FATAL_ERROR "${failures} source dir check(s) failed")
endif ()

message(STATUS "all source dir checks passed")
