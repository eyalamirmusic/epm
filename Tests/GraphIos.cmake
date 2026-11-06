# End-to-end self-test of prebuilt packages for an iOS simulator consumer, with
# the real epm binary. Commits Tests/Nested/Leaf and Tests/Nested/Mid into
# local git repositories, as Tests/GraphNested.cmake does, then configures and
# builds Tests/Nested/Consumer with CMAKE_SYSTEM_NAME=iOS and the simulator SDK
# and no toolchain file, once per generator:
#
#   xcode   -G Xcode, CMAKE_OSX_SYSROOT=iphonesimulator, no architectures:
#           env.json carries the ones Xcode builds (ARCHS_STANDARD)
#   ninja   -G Ninja, Release (only with a ninja, TEST_MAKE_PROGRAM or on PATH):
#           the compiler's default architecture
#
# Each checks that the child builds were told the target system (Leaf's own
# status line in the EPM_VERBOSE output, as epm deletes the child build tree),
# that graph.json's platform and the cache entries are ios-simulator-<arch>,
# that the harvested archives hold simulator objects for those architectures
# (lipo, otool's LC_BUILD_VERSION), that the consumer's app links against them
# for every architecture, and that epm_add_recipe skipped itself.
#
#   cmake -DEPM_BINARY=<epm> [-DWORK_DIR=<dir>] [-DTEST_MAKE_PROGRAM=<ninja>]
#         -P Tests/GraphIos.cmake

cmake_minimum_required(VERSION 3.25)

set(fixtures "${CMAKE_CURRENT_LIST_DIR}/Nested")
get_filename_component(repo_dir "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

if (NOT EXISTS "${EPM_BINARY}")
    message(FATAL_ERROR "EPM_BINARY '${EPM_BINARY}' does not exist")
endif ()

if (NOT CMAKE_HOST_APPLE)
    message(STATUS "epm-graph-ios: skipped: not on macOS")
    return()
endif ()

find_program(git_executable git NO_CACHE)

if (NOT git_executable)
    message(STATUS "epm-graph-ios: skipped: no git")
    return()
endif ()

execute_process(COMMAND xcrun --sdk iphonesimulator --show-sdk-path
        RESULT_VARIABLE result
        OUTPUT_VARIABLE simulator_sdk
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE)

if (NOT result EQUAL 0 OR NOT IS_DIRECTORY "${simulator_sdk}")
    message(STATUS "epm-graph-ios: skipped: no iphonesimulator SDK")
    return()
endif ()

if (NOT WORK_DIR)
    set(WORK_DIR "${CMAKE_CURRENT_BINARY_DIR}/epm-graph-ios")
endif ()

file(REMOVE_RECURSE "${WORK_DIR}")

set(repos "${WORK_DIR}/repos")
set(cache "${WORK_DIR}/cache")
set(consumer "${WORK_DIR}/Consumer")
set(scenario "setup")

function(fail text)
    message(FATAL_ERROR "epm-graph-ios: ${scenario}: ${text}")
endfunction()

function(expect_equal what actual expected)
    if (NOT actual STREQUAL expected)
        fail("${what} is '${actual}', expected '${expected}'")
    endif ()
endfunction()

function(git directory)
    execute_process(
            COMMAND "${git_executable}" -c user.name=epm
            -c user.email=epm@example.invalid -c commit.gpgsign=false
            -c tag.gpgsign=false -c init.defaultBranch=main ${ARGN}
            WORKING_DIRECTORY "${directory}"
            RESULT_VARIABLE result
            OUTPUT_VARIABLE output
            ERROR_VARIABLE output)

    if (NOT result EQUAL 0)
        fail("git ${ARGN} in ${directory} failed:\n${output}")
    endif ()
endfunction()

# Commits everything in ${repos}/<name> and tags it v1; sets <out_var> to its
# file:// url.
function(make_repository name out_var)
    set(directory "${repos}/${name}")
    git("${directory}" init -q)
    git("${directory}" add -A)
    git("${directory}" commit -q -m "${name}")
    git("${directory}" tag v1)
    set(${out_var} "file://${directory}" PARENT_SCOPE)
endfunction()

file(COPY "${fixtures}/Leaf/" DESTINATION "${repos}/Leaf")
make_repository(Leaf LEAF_URL)

set(LEAF_MODE "")
file(COPY "${fixtures}/Mid/" DESTINATION "${repos}/Mid" PATTERN "*.in" EXCLUDE)
file(COPY "${repo_dir}/CMake/get_epm.cmake" DESTINATION "${repos}/Mid/cmake")
configure_file("${fixtures}/Mid/epm.cpp.in" "${repos}/Mid/epm.cpp" @ONLY)
make_repository(Mid MID_URL)

set(GET_EPM "${repo_dir}/CMake/get_epm.cmake")
set(ROOT_CACHES_LEAF OFF)
file(COPY "${fixtures}/Consumer/Main.cpp" DESTINATION "${consumer}")
configure_file("${fixtures}/Consumer/CMakeLists.txt.in"
        "${consumer}/CMakeLists.txt" @ONLY)
configure_file("${fixtures}/Consumer/epm.cpp.in" "${consumer}/epm.cpp" @ONLY)

set(common_args -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0
        -DEPM_VERBOSE=ON "-DEPM_EXECUTABLE=${EPM_BINARY}" "-DEPM_CACHE=${cache}")

# The platform name of the architectures in `architectures` (a list).
function(simulator_platform architectures out_var)
    if ("arm64" IN_LIST architectures AND "x86_64" IN_LIST architectures)
        set(name universal)
    elseif (architectures STREQUAL "x86_64")
        set(name x64)
    else ()
        set(name "${architectures}")
    endif ()

    set(${out_var} "ios-simulator-${name}" PARENT_SCOPE)
endfunction()

# The architectures of a Mach-O file or archive, sorted.
function(file_architectures file out_var)
    execute_process(COMMAND lipo -archs "${file}"
            RESULT_VARIABLE result
            OUTPUT_VARIABLE archs
            ERROR_VARIABLE archs
            OUTPUT_STRIP_TRAILING_WHITESPACE)

    if (NOT result EQUAL 0)
        fail("lipo -archs ${file} failed (${result}): ${archs}")
    endif ()

    separate_arguments(archs UNIX_COMMAND "${archs}")
    list(SORT archs)
    set(${out_var} "${archs}" PARENT_SCOPE)
endfunction()

# Every object in the archive is an iOS simulator one (LC_BUILD_VERSION
# platform 7, PLATFORM_IOSSIMULATOR) with a minimum OS of 15.0.
function(expect_simulator_objects archive)
    execute_process(COMMAND otool -l "${archive}"
            RESULT_VARIABLE result
            OUTPUT_VARIABLE load_commands
            ERROR_VARIABLE load_commands)

    if (NOT result EQUAL 0)
        fail("otool -l ${archive} failed (${result}):\n${load_commands}")
    endif ()

    string(REGEX MATCHALL "platform [0-9A-Z]+" platforms "${load_commands}")
    string(REGEX MATCHALL "minos [0-9.]+" minimums "${load_commands}")
    list(REMOVE_DUPLICATES platforms)
    list(REMOVE_DUPLICATES minimums)
    expect_equal("${archive}'s platforms" "${platforms}" "platform 7")
    expect_equal("${archive}'s minimum OS" "${minimums}" "minos 15.0")
endfunction()

# The value at the JSON path ARGN of package `name` in graph.json.
function(graph_value name out_var)
    file(READ "${build}/epm/graph.json" graph)
    string(JSON count LENGTH "${graph}" packages)
    math(EXPR last "${count} - 1")

    foreach (index RANGE ${last})
        string(JSON package_name GET "${graph}" packages ${index} name)

        if (package_name STREQUAL name)
            string(JSON value GET "${graph}" packages ${index} ${ARGN})
            set(${out_var} "${value}" PARENT_SCOPE)
            return()
        endif ()
    endforeach ()

    fail("${name} is not in graph.json")
endfunction()

# Configures the consumer into ${WORK_DIR}/<name> with ARGN, builds it in
# Release and checks the packages and the app; `expected_archs` is the list of
# architectures env.json must carry.
function(check_consumer name expected_archs)
    set(scenario "${name}")
    set(build "${WORK_DIR}/${name}")
    string(TIMESTAMP start "%s")

    execute_process(
            COMMAND "${CMAKE_COMMAND}" -S "${consumer}" -B "${build}"
            ${common_args} ${ARGN}
            RESULT_VARIABLE result
            OUTPUT_VARIABLE output
            ERROR_VARIABLE output)

    if (NOT result EQUAL 0)
        fail("configure failed (${result}):\n${output}")
    endif ()

    file(READ "${build}/epm/env.json" env)
    string(JSON cross GET "${env}" crossCompiling)
    expect_equal("env.json's crossCompiling" "${cross}" "ON")
    string(JSON env_archs GET "${env}" osxArchitectures)
    list(SORT env_archs)

    # The consumer's own CMAKE_XCODE_ARCHS, which must include the host's.
    if (expected_archs STREQUAL "XCODE")
        file(GLOB compiler_file "${build}/CMakeFiles/*/CMakeCXXCompiler.cmake")
        include("${compiler_file}")

        if (NOT host_arch IN_LIST CMAKE_XCODE_ARCHS)
            fail("CMAKE_XCODE_ARCHS in ${compiler_file} is '${CMAKE_XCODE_ARCHS}', "
                    "expected one naming ${host_arch}")
        endif ()

        set(expected_archs "${CMAKE_XCODE_ARCHS}")
    endif ()

    list(SORT expected_archs)
    expect_equal("env.json's osxArchitectures" "${env_archs}" "${expected_archs}")
    simulator_platform("${env_archs}" platform)

    if (NOT output MATCHES "epm_add_recipe\\(ConsumerRecipe\\) does nothing when cross-compiling")
        fail("no note that epm_add_recipe skipped itself:\n${output}")
    endif ()

    # Leaf's child build configured for the simulator: its own status line.
    if (NOT output MATCHES "Leaf: system iOS, sysroot [^\n]*iPhoneSimulator[^\n]*\\.sdk")
        fail("Leaf's child build did not configure for the iOS simulator:\n${output}")
    endif ()

    foreach (package IN ITEMS Leaf Mid)
        graph_value(${package} mode mode)
        expect_equal("${package}'s mode" "${mode}" prebuilt)
        graph_value(${package} package_platform platform)
        expect_equal("${package}'s platform" "${package_platform}" "${platform}")
        graph_value(${package} prefix prefix)
        string(FIND "${prefix}" "${cache}/bin/${package}/${platform}/" at)

        if (NOT at EQUAL 0)
            fail("${package}'s entry ${prefix} is not under "
                    "${cache}/bin/${package}/${platform}/")
        endif ()

        set(archive "${prefix}/configs/Release/lib/Release/lib${package}.a")

        if (NOT EXISTS "${archive}")
            fail("no ${archive}")
        endif ()

        file_architectures("${archive}" archive_archs)
        expect_equal("${archive}'s architectures" "${archive_archs}"
                "${expected_archs}")
        expect_simulator_objects("${archive}")
    endforeach ()

    execute_process(
            COMMAND "${CMAKE_COMMAND}" --build "${build}" --config Release
            RESULT_VARIABLE result
            OUTPUT_VARIABLE build_output
            ERROR_VARIABLE build_output)

    if (NOT result EQUAL 0)
        fail("Release build failed (${result}):\n${build_output}")
    endif ()

    set(app "")

    foreach (candidate IN ITEMS "${build}/app.app/app"
            "${build}/Release-iphonesimulator/app.app/app")
        if (EXISTS "${candidate}")
            set(app "${candidate}")
        endif ()
    endforeach ()

    if (NOT app)
        fail("no app.app/app in ${build}")
    endif ()

    file_architectures("${app}" app_archs)
    expect_equal("${app}'s architectures" "${app_archs}" "${expected_archs}")

    string(TIMESTAMP end "%s")
    math(EXPR seconds "${end} - ${start}")
    message(STATUS "epm-graph-ios: ${name}: ${platform} passed in ${seconds}s")
endfunction()

execute_process(COMMAND uname -m OUTPUT_VARIABLE host_arch
        OUTPUT_STRIP_TRAILING_WHITESPACE)

# Xcode builds a simulator's ARCHS_STANDARD when CMAKE_OSX_ARCHITECTURES is
# empty, arm64;x86_64 as of Xcode 27: CMake records it as CMAKE_XCODE_ARCHS,
# which only the configure knows.
check_consumer(xcode XCODE -G Xcode -DCMAKE_OSX_SYSROOT=iphonesimulator
        -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO
        "-DCMAKE_XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY=")

# Ninja targets the host's architecture by default, and resolves the SDK name
# to its path, which env.json then carries.
if (TEST_MAKE_PROGRAM MATCHES "ninja")
    set(ninja "${TEST_MAKE_PROGRAM}")
else ()
    find_program(ninja ninja NO_CACHE)
endif ()

if (ninja)
    check_consumer(ninja "${host_arch}" -G Ninja "-DCMAKE_MAKE_PROGRAM=${ninja}"
            -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_SYSROOT=iphonesimulator)
else ()
    message(STATUS "epm-graph-ios: ninja: not run, no ninja")
endif ()

message(STATUS "All iOS graph checks passed")
