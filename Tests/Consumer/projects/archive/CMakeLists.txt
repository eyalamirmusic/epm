cmake_minimum_required(VERSION 3.24)
set(CMAKE_PROJECT_TOP_LEVEL_INCLUDES ${LOCKSTEP_SHIM})
project(ArchiveSample LANGUAGES CXX)

lockstep_add_package(NAME adder
        URL https://github.com/cpm-cmake/testpack-adder/archive/refs/tags/v1.0.1.tar.gz)
lockstep_add_package(NAME fibonacci
        URL https://github.com/cpm-cmake/testpack-fibonacci/archive/refs/tags/v1.0.zip
        DOWNLOAD_ONLY)

if (NOT EXISTS "${fibonacci_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "the zip archive did not unpack into ${fibonacci_SOURCE_DIR}")
endif ()

add_executable(sample ../github/main.cpp)
target_link_libraries(sample PRIVATE adder)
