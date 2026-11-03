cmake_minimum_required(VERSION 3.31)

project(epm VERSION 0.1.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

if (MSVC)
    add_compile_options(/EHsc)
endif ()

option(EPM_VERBOSE_CONFIGURE
        "Print the third-party fetch and probe detail hidden by default" OFF)

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

# The whole install: the binary and the consumer module. eacp and Miro are
# static and linked in. A tool entry of get_epm.cmake is exactly this install,
# whether unpacked from a release archive or built from source.
install(FILES CMake/epm.cmake DESTINATION share/epm/cmake)

option(EPM_ENABLE_TESTS "Enable Tests for epm" ${PROJECT_IS_TOP_LEVEL})

if (EPM_ENABLE_TESTS)
    enable_testing()
    add_subdirectory(Tests)
endif ()
