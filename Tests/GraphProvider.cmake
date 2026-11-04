# Self-test of the manifest flow's CMake side: CMake/epm-provider.cmake over a
# hand-written graph.cmake, and CMake/get_epm.cmake as a top-level include
# driving a fake epm binary. Configures Tests/Graph/Consumer once per scenario.
#
#   cmake [-DWORK_DIR=<dir>] [-DTEST_GENERATOR=<generator>]
#         [-DTEST_MAKE_PROGRAM=<path>] -P Tests/GraphProvider.cmake

cmake_minimum_required(VERSION 3.25)

set(graph_dir "${CMAKE_CURRENT_LIST_DIR}/Graph")
get_filename_component(repo_dir "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(PACKAGES "${graph_dir}/Packages")

if (NOT WORK_DIR)
    set(WORK_DIR "${CMAKE_CURRENT_BINARY_DIR}/epm-graph-provider")
endif ()

file(REMOVE_RECURSE "${WORK_DIR}")
configure_file("${graph_dir}/graph.cmake.in" "${WORK_DIR}/graph.cmake" @ONLY)

set(generator_args "")

if (TEST_GENERATOR)
    list(APPEND generator_args -G "${TEST_GENERATOR}")
endif ()

if (TEST_MAKE_PROGRAM)
    list(APPEND generator_args "-DCMAKE_MAKE_PROGRAM=${TEST_MAKE_PROGRAM}")
endif ()

set(failures 0)
set(env_prefix "")

macro(fail text)
    message(SEND_ERROR "${scenario}: ${text}")
    math(EXPR failures "${failures} + 1")
endmacro()

# Configures the consumer into ${WORK_DIR}/<scenario>; ARGN goes to cmake.
# Sets `result` and `output` (stdout and stderr).
macro(configure_consumer scenario_name)
    set(scenario "${scenario_name}")
    execute_process(
            COMMAND ${env_prefix} "${CMAKE_COMMAND}"
            -S "${graph_dir}/Consumer" -B "${WORK_DIR}/${scenario}"
            ${generator_args} "-DEPM_TEST_SCENARIO=${scenario}" ${ARGN}
            RESULT_VARIABLE result
            OUTPUT_VARIABLE output
            ERROR_VARIABLE output)
    set(env_prefix "")
endmacro()

macro(expect_success)
    if (NOT result EQUAL 0 OR NOT output MATCHES "epm-graph-test: ok")
        fail("configure failed (${result}):\n${output}")
    endif ()
endmacro()

macro(expect_failure text)
    if (result EQUAL 0)
        fail("configure succeeded, expected a failure:\n${output}")
    else ()
        # CMake wraps error messages: compare with whitespace runs collapsed.
        string(REGEX REPLACE "[ \t\r\n]+" " " flat_output "${output}")
        string(REGEX REPLACE "[ \t\r\n]+" " " flat_text "${text}")
        string(FIND "${flat_output}" "${flat_text}" at)

        if (at EQUAL -1)
            fail("the output lacks '${text}':\n${output}")
        endif ()
    endif ()
endmacro()

# `text` appears exactly `count` times in the output.
macro(expect_count text count)
    string(REPLACE "${text}" "" stripped "${output}")
    string(LENGTH "${output}" output_length)
    string(LENGTH "${stripped}" stripped_length)
    string(LENGTH "${text}" text_length)
    math(EXPR seen "(${output_length} - ${stripped_length}) / ${text_length}")

    if (NOT seen EQUAL ${count})
        fail("'${text}' appears ${seen} times, expected ${count}:\n${output}")
    endif ()
endmacro()

macro(expect_provided_packages)
    expect_count("-- epm: Pre (prebuilt, fake-platform, 0123456789ab)" 1)
    expect_count("-- epm: Leaf (source, Leaf)" 1)
    expect_count("-- epm: Fetched (source, Fetched)" 1)
    expect_count("-- epm: note: Leaf: Other declares 1.0, root declares 2.0" 1)
endmacro()

# 1. The provider alone, over a ready graph (what a nested build does).
configure_consumer(provider
        "-DCMAKE_PROJECT_TOP_LEVEL_INCLUDES=${graph_dir}/RegisterProvider.cmake"
        "-DEPM_TEST_GRAPH=${WORK_DIR}/graph.cmake")
expect_success()
expect_provided_packages()

# 2. get_epm.cmake with a fake epm install: bin/epm answers --version and
# `configure` (copying the graph above), share/epm/cmake holds the modules.
if (CMAKE_HOST_WIN32)
    message(STATUS "Skipping the get_epm.cmake scenarios: the fake epm is a "
            "shell script")
else ()
    set(fake_prefix "${WORK_DIR}/fake-epm")
    set(fake_epm "${fake_prefix}/bin/epm")
    file(COPY "${repo_dir}/CMake/epm.cmake" "${repo_dir}/CMake/epm-provider.cmake"
            DESTINATION "${fake_prefix}/share/epm/cmake")
    file(MAKE_DIRECTORY "${fake_prefix}/bin")
    file(WRITE "${fake_epm}" "#!/bin/sh
if [ \"$1\" = --version ]; then echo 0.2.0; exit 0; fi
if [ \"$1\" != configure ]; then echo \"fake epm: unknown command $1\" >&2; exit 2; fi
if [ -n \"$EPM_TEST_FAIL\" ]; then
    echo 'fake epm: resolving the graph'
    echo 'fake epm: epm.cpp is broken' >&2
    exit 3
fi
out=''
previous=''
for arg in \"$@\"; do
    if [ \"$previous\" = --out ]; then out=\"$arg\"; fi
    previous=\"$arg\"
done
printf '%s\\n' \"$@\" > \"$out/configure-args.txt\"
cp '${WORK_DIR}/graph.cmake' \"$out/graph.cmake\"
")
    file(CHMOD "${fake_epm}" FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
            GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

    set(get_epm_args
            "-DCMAKE_PROJECT_TOP_LEVEL_INCLUDES=${repo_dir}/CMake/get_epm.cmake"
            "-DEPM_EXECUTABLE=${fake_epm}")

    # Overrides as cache entries (-C), plus the normal variable the consumer
    # sets (EPM_Fetched_CONFIGS).
    file(WRITE "${WORK_DIR}/overrides-init.cmake" [=[
set(EPM_PROFILE ci CACHE STRING "")
set(EPM_CONFIGS "Debug;Release" CACHE STRING "")
set(EPM_STRICT ON CACHE BOOL "")
set(EPM_Pre_MODE source CACHE STRING "")
set(EPM_Leaf_OPTIONS "LEAF_X=1;LEAF_Y=a \"b\"" CACHE STRING "")
]=])

    configure_consumer(get_epm ${get_epm_args} -C "${WORK_DIR}/overrides-init.cmake")
    expect_success()
    expect_provided_packages()

    set(epm_dir "${WORK_DIR}/get_epm/epm")
    file(READ "${epm_dir}/configure-args.txt" args)
    string(REPLACE "\n" ";" args "${args}")
    set(expected_args configure
            --manifest "${graph_dir}/Consumer/epm.cpp"
            --env "${epm_dir}/env.json"
            --overrides "${epm_dir}/overrides.json"
            --out "${epm_dir}")

    if (NOT args STREQUAL "${expected_args};")
        fail("epm configure got '${args}', expected '${expected_args}'")
    endif ()

    file(READ "${epm_dir}/env.json" env)
    string(JSON cxx_standard GET "${env}" cxxStandard)

    if (NOT cxx_standard STREQUAL "20")
        fail("env.json has cxxStandard '${cxx_standard}', expected 20 (set after "
                "project())")
    endif ()

    # EPM_PROFILE reaches the recipe through env.json, not as an override.
    string(JSON profile GET "${env}" profile)

    if (NOT profile STREQUAL "ci")
        fail("env.json has profile '${profile}', expected ci")
    endif ()

    file(READ "${epm_dir}/overrides.json" overrides)
    set(expected_overrides [=[{
        "schema": 1, "mode": "", "configs": ["Debug", "Release"],
        "abiMatch": "", "strict": true,
        "packages": {
            "Fetched": {"mode": "", "source": "", "configs": ["Release"], "options": {}},
            "Leaf": {"mode": "", "source": "", "configs": [],
                     "options": {"LEAF_X": "1", "LEAF_Y": "a \"b\""}},
            "Pre": {"mode": "source", "source": "", "configs": [], "options": {}}
        }
    }]=])
    string(JSON same EQUAL "${overrides}" "${expected_overrides}")

    if (NOT same)
        fail("overrides.json is\n${overrides}\nexpected\n${expected_overrides}")
    endif ()

    # 3. EPM_NESTED: get_epm.cmake does nothing, not even look for a binary.
    configure_consumer(nested ${get_epm_args} -DEPM_NESTED=ON)
    expect_success()
    expect_count("epm: using epm" 0)

    if (EXISTS "${WORK_DIR}/nested/epm/overrides.json")
        fail("overrides.json was written")
    endif ()

    # 4. A failing `epm configure` fails the configure, with its output. The
    # env.json written before it takes EPM_PROFILE from the environment.
    set(env_prefix "${CMAKE_COMMAND}" -E env EPM_TEST_FAIL=1 EPM_PROFILE=nightly)
    configure_consumer(failing ${get_epm_args})
    expect_failure("epm: epm configure failed (exit 3)")
    expect_failure("fake epm: resolving the graph")
    expect_failure("fake epm: epm.cpp is broken")

    file(READ "${WORK_DIR}/failing/epm/env.json" env)
    string(JSON profile GET "${env}" profile)

    if (NOT profile STREQUAL "nightly")
        fail("env.json has profile '${profile}', expected nightly (environment)")
    endif ()

    # 5. include() after project() cannot register a provider: a clear error.
    configure_consumer(included "-DEPM_EXECUTABLE=${fake_epm}"
            "-DEPM_TEST_INCLUDE_GET_EPM=${repo_dir}/CMake/get_epm.cmake")
    expect_failure("listed in CMAKE_PROJECT_TOP_LEVEL_INCLUDES before project()")
endif ()

if (failures GREATER 0)
    message(FATAL_ERROR "${failures} graph provider check(s) failed")
endif ()

message(STATUS "All graph provider checks passed")
