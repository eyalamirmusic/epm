# The consumer Tests/GraphProvider.cmake configures in each scenario. The
# provider comes from CMAKE_PROJECT_TOP_LEVEL_INCLUDES, given on the command
# line; EPM_TEST_SCENARIO says what to expect.
cmake_minimum_required(VERSION 3.25)
project(Consumer NONE)

get_filename_component(packages "${CMAKE_CURRENT_LIST_DIR}/../Packages" ABSOLUTE)

if (EPM_TEST_INCLUDE_GET_EPM)
    # The pre-manifest form, which epm.json does not allow.
    include("${EPM_TEST_INCLUDE_GET_EPM}")
endif ()

if (EPM_TEST_SCENARIO STREQUAL "nested")
    find_package(Leaf QUIET)

    if (Leaf_FOUND OR TARGET Leaf::leaf)
        message(SEND_ERROR "nested: Leaf was provided")
    endif ()

    message(STATUS "epm-graph-test: ok")
    return()
endif ()

# Set after project(): seen because the graph is resolved at the first
# find_package, not when project() ran.
set(CMAKE_CXX_STANDARD 20)
set(EPM_Fetched_CONFIGS Release)

add_subdirectory(Sub)

if (DEFINED LEAF_FLAG)
    message(SEND_ERROR "Leaf's options leaked into the consumer: ${LEAF_FLAG}")
endif ()

get_property(order GLOBAL PROPERTY EPM_TEST_ORDER)

if (NOT order STREQUAL "Pre;Leaf")
    message(SEND_ERROR "load order is '${order}', expected 'Pre;Leaf'")
endif ()

foreach (pair IN ITEMS "FLAG=ON" "LEVEL=high" "NAME=two words=2")
    string(FIND "${pair}" "=" equals)
    string(SUBSTRING "${pair}" 0 ${equals} key)
    math(EXPR equals "${equals} + 1")
    string(SUBSTRING "${pair}" ${equals} -1 expected)
    get_property(actual GLOBAL PROPERTY EPM_TEST_LEAF_${key})

    if (NOT actual STREQUAL expected)
        message(SEND_ERROR "Leaf saw LEAF_${key}='${actual}', expected '${expected}'")
    endif ()
endforeach ()

# Loaded from Sub, seen here: the imported targets were made GLOBAL.
foreach (target IN ITEMS Pre::pre pre epm_release::pre Leaf::leaf leaf)
    if (NOT TARGET ${target})
        message(SEND_ERROR "target ${target} is not visible from the top level")
    endif ()
endforeach ()

get_property(global TARGET epm_release::pre PROPERTY IMPORTED_GLOBAL)

if (NOT global)
    message(SEND_ERROR "epm_release::pre was not made GLOBAL")
endif ()

# Loaded once; version arguments are ignored.
find_package(Pre 99.0 REQUIRED)

if (NOT Pre_FOUND OR NOT Pre_MODE STREQUAL "prebuilt"
        OR NOT Pre_SOURCE_DIR STREQUAL "${packages}/Pre/src")
    message(SEND_ERROR "find_package(Pre): FOUND '${Pre_FOUND}', MODE "
            "'${Pre_MODE}', SOURCE_DIR '${Pre_SOURCE_DIR}'")
endif ()

get_property(order GLOBAL PROPERTY EPM_TEST_ORDER)

if (NOT order STREQUAL "Pre;Leaf")
    message(SEND_ERROR "Pre was loaded again: order '${order}'")
endif ()

# Names the graph marks "system" go to CMake's own search.
find_package(NotEpm CONFIG REQUIRED PATHS "${packages}/NotEpm" NO_DEFAULT_PATH)

if (NOT NotEpm_FROM_CONFIG OR DEFINED NotEpm_MODE)
    message(SEND_ERROR "find_package(NotEpm) did not reach NotEpmConfig.cmake")
endif ()

find_package(Missing QUIET)

if (Missing_FOUND)
    message(SEND_ERROR "find_package(Missing) was found")
endif ()

# FetchContent_MakeAvailable goes through the provider too; the URL would fail
# if FetchContent tried it.
include(FetchContent)
FetchContent_Declare(Fetched URL "file:///nonexistent/fetched.tar.gz")
FetchContent_MakeAvailable(Fetched)
FetchContent_GetProperties(Fetched)

if (NOT TARGET Fetched::fetched OR NOT fetched_POPULATED
        OR NOT fetched_SOURCE_DIR STREQUAL "${packages}/Fetched"
        OR NOT fetched_BINARY_DIR STREQUAL "${CMAKE_BINARY_DIR}/_deps/Fetched-build")
    message(SEND_ERROR "FetchContent_MakeAvailable(Fetched): POPULATED "
            "'${fetched_POPULATED}', SOURCE_DIR '${fetched_SOURCE_DIR}', "
            "BINARY_DIR '${fetched_BINARY_DIR}'")
endif ()

message(STATUS "epm-graph-test: ok")
