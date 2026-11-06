# End-to-end self-test of prebuilt packages for an Android consumer, with the
# real epm binary and the NDK. Commits Tests/Nested/Leaf and Tests/Nested/Mid
# into local git repositories, as Tests/GraphIos.cmake does, then configures
# and builds Tests/Nested/Consumer under Ninja, Release, once per way of
# targeting Android:
#
#   cmake      CMake's own Android support: CMAKE_SYSTEM_NAME=Android,
#              CMAKE_ANDROID_NDK, arm64-v8a, API level 26, c++_static
#   toolchain  the NDK's android.toolchain.cmake: ANDROID_ABI=x86_64,
#              ANDROID_PLATFORM=android-28, ANDROID_STL=c++_shared, none of
#              which is the toolchain file's default
#
# Each checks env.json's Android settings, that the child builds were
# configured for that ABI, API level and STL (Leaf's own status line in the
# EPM_VERBOSE output, as epm deletes the child build tree), that graph.json's
# platform and the cache entries are android-<abi>, that the harvested
# archives hold ELF objects for that ABI's machine, that the consumer's app
# links against them (an ELF executable for the same machine: nothing runs on
# a device) and that epm_add_recipe skipped itself.
#
# The NDK: ANDROID_NDK_HOME, else ANDROID_NDK_ROOT, else the newest ndk/* under
# ANDROID_HOME, ANDROID_SDK_ROOT or Android Studio's default SDK directory.
# Skipped without one, without git or ninja, and on Windows hosts.
#
#   cmake -DEPM_BINARY=<epm> [-DWORK_DIR=<dir>] [-DTEST_MAKE_PROGRAM=<ninja>]
#         -P Tests/GraphAndroid.cmake

cmake_minimum_required(VERSION 3.25)

set(fixtures "${CMAKE_CURRENT_LIST_DIR}/Nested")
get_filename_component(repo_dir "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

if (NOT EXISTS "${EPM_BINARY}")
    message(FATAL_ERROR "EPM_BINARY '${EPM_BINARY}' does not exist")
endif ()

if (CMAKE_HOST_WIN32)
    message(STATUS "epm-graph-android: skipped: not run on Windows hosts")
    return()
endif ()

find_program(git_executable git NO_CACHE)

if (NOT git_executable)
    message(STATUS "epm-graph-android: skipped: no git")
    return()
endif ()

if (TEST_MAKE_PROGRAM MATCHES "ninja")
    set(ninja "${TEST_MAKE_PROGRAM}")
else ()
    find_program(ninja ninja NO_CACHE)
endif ()

if (NOT ninja)
    message(STATUS "epm-graph-android: skipped: no ninja")
    return()
endif ()

# The newest NDK (by version) under the SDK directory `sdk`, if any.
function(newest_ndk_in sdk out_var)
    file(GLOB candidates LIST_DIRECTORIES true "${sdk}/ndk/*")
    list(FILTER candidates INCLUDE REGEX "/[0-9][0-9.]*$")
    list(SORT candidates COMPARE NATURAL ORDER DESCENDING)

    foreach (candidate IN LISTS candidates)
        if (EXISTS "${candidate}/build/cmake/android.toolchain.cmake")
            set(${out_var} "${candidate}" PARENT_SCOPE)
            return()
        endif ()
    endforeach ()
endfunction()

set(ndk "")

foreach (variable IN ITEMS ANDROID_NDK_HOME ANDROID_NDK_ROOT)
    if (NOT ndk AND IS_DIRECTORY "$ENV{${variable}}")
        set(ndk "$ENV{${variable}}")
    endif ()
endforeach ()

if (CMAKE_HOST_APPLE)
    set(studio_sdk "$ENV{HOME}/Library/Android/sdk")
else ()
    set(studio_sdk "$ENV{HOME}/Android/Sdk")
endif ()

foreach (sdk IN ITEMS "$ENV{ANDROID_HOME}" "$ENV{ANDROID_SDK_ROOT}" "${studio_sdk}")
    if (NOT ndk AND IS_DIRECTORY "${sdk}")
        newest_ndk_in("${sdk}" ndk)
    endif ()
endforeach ()

if (NOT ndk OR NOT EXISTS "${ndk}/build/cmake/android.toolchain.cmake")
    message(STATUS "epm-graph-android: skipped: no Android NDK (set "
            "ANDROID_NDK_HOME, ANDROID_NDK_ROOT or ANDROID_HOME)")
    return()
endif ()

file(REAL_PATH "${ndk}" ndk)
file(GLOB readelf "${ndk}/toolchains/llvm/prebuilt/*/bin/llvm-readelf")

if (NOT readelf)
    message(FATAL_ERROR "epm-graph-android: no llvm-readelf in the NDK at ${ndk}")
endif ()

list(GET readelf 0 readelf)
message(STATUS "epm-graph-android: NDK ${ndk}")

# What env.json's sdkVersion, and so the fingerprint, records of the NDK in
# place of its directory.
file(STRINGS "${ndk}/source.properties" ndk_revision
        REGEX "^[ \t]*Pkg\\.Revision[ \t]*=")
string(REGEX REPLACE "^[^=]*=[ \t]*" "" ndk_revision "${ndk_revision}")

if (NOT WORK_DIR)
    set(WORK_DIR "${CMAKE_CURRENT_BINARY_DIR}/epm-graph-android")
endif ()

file(REMOVE_RECURSE "${WORK_DIR}")

set(repos "${WORK_DIR}/repos")
set(cache "${WORK_DIR}/cache")
set(consumer "${WORK_DIR}/Consumer")
set(scenario "setup")

function(fail text)
    message(FATAL_ERROR "epm-graph-android: ${scenario}: ${text}")
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

set(common_args -G Ninja "-DCMAKE_MAKE_PROGRAM=${ninja}" -DCMAKE_BUILD_TYPE=Release
        -DEPM_VERBOSE=ON "-DEPM_EXECUTABLE=${EPM_BINARY}" "-DEPM_CACHE=${cache}")

# The ELF machine llvm-readelf names for each Android ABI.
set(machine_arm64-v8a "AArch64")
set(machine_x86_64 "Advanced Micro Devices X86-64")

# Every object in `file` (an archive or an executable) is an ELF file for
# `machine`.
function(expect_machine file machine)
    execute_process(COMMAND "${readelf}" --file-header "${file}"
            RESULT_VARIABLE result
            OUTPUT_VARIABLE headers
            ERROR_VARIABLE headers)

    if (NOT result EQUAL 0)
        fail("llvm-readelf --file-header ${file} failed (${result}):\n${headers}")
    endif ()

    string(REGEX MATCHALL "Machine: +[^\n]+" machines "${headers}")
    list(REMOVE_DUPLICATES machines)
    string(REGEX REPLACE "Machine: +" "" machines "${machines}")
    expect_equal("${file}'s machines" "${machines}" "${machine}")
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
# Release and checks the packages and the app, for an `abi`, `api` level and
# `stl` the consumer asked for in ARGN; `triple` is the compiler target
# without the API level.
function(check_consumer name abi api stl triple)
    set(scenario "${name}")
    set(build "${WORK_DIR}/${name}")
    set(platform "android-${abi}")
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

    foreach (pair IN ITEMS "systemName=Android" "crossCompiling=ON"
            "androidAbi=${abi}" "androidApi=${api}" "androidStl=${stl}"
            "androidNdk=${ndk}" "sdkVersion=${ndk_revision}")
        string(FIND "${pair}" "=" equals)
        string(SUBSTRING "${pair}" 0 ${equals} key)
        math(EXPR equals "${equals} + 1")
        string(SUBSTRING "${pair}" ${equals} -1 expected)
        string(JSON value GET "${env}" ${key})

        if (key STREQUAL "androidNdk")
            file(REAL_PATH "${value}" value)
        endif ()

        expect_equal("env.json's ${key}" "${value}" "${expected}")
    endforeach ()

    if (NOT output MATCHES "epm_add_recipe\\(ConsumerRecipe\\) does nothing when cross-compiling")
        fail("no note that epm_add_recipe skipped itself:\n${output}")
    endif ()

    # Leaf's child build configured for the consumer's ABI, API level and STL:
    # its own status line.
    string(REGEX MATCH "Leaf: android target [^\n]*" leaf_line "${output}")
    expect_equal("Leaf's child build" "${leaf_line}"
            "Leaf: android target ${triple}${api}, stl ${stl}")

    if (NOT output MATCHES "Leaf: system Android,")
        fail("Leaf's child build did not configure for Android:\n${output}")
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

        expect_machine("${archive}" "${machine_${abi}}")
    endforeach ()

    execute_process(
            COMMAND "${CMAKE_COMMAND}" --build "${build}"
            RESULT_VARIABLE result
            OUTPUT_VARIABLE build_output
            ERROR_VARIABLE build_output)

    if (NOT result EQUAL 0)
        fail("build failed (${result}):\n${build_output}")
    endif ()

    if (NOT EXISTS "${build}/app")
        fail("no app in ${build}")
    endif ()

    expect_machine("${build}/app" "${machine_${abi}}")

    string(TIMESTAMP end "%s")
    math(EXPR seconds "${end} - ${start}")
    message(STATUS "epm-graph-android: ${name}: ${platform} passed in ${seconds}s")
endfunction()

check_consumer(cmake arm64-v8a 26 c++_static aarch64-none-linux-android
        -DCMAKE_SYSTEM_NAME=Android "-DCMAKE_ANDROID_NDK=${ndk}"
        -DCMAKE_ANDROID_ARCH_ABI=arm64-v8a -DCMAKE_SYSTEM_VERSION=26
        -DCMAKE_ANDROID_STL_TYPE=c++_static)

check_consumer(toolchain x86_64 28 c++_shared x86_64-none-linux-android
        "-DCMAKE_TOOLCHAIN_FILE=${ndk}/build/cmake/android.toolchain.cmake"
        -DANDROID_ABI=x86_64 -DANDROID_PLATFORM=android-28 -DANDROID_STL=c++_shared)

message(STATUS "All Android graph checks passed")
