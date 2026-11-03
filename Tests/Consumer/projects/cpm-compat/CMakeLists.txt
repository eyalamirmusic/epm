cmake_minimum_required(VERSION 3.24)
set(CMAKE_PROJECT_TOP_LEVEL_INCLUDES ${LOCKSTEP_SHIM})
project(CpmCompatSample LANGUAGES CXX)

# SOURCE_DIR beside a repository is where CPM clones it, and the package is locked.
set(adder_checkout "${CMAKE_BINARY_DIR}/checkouts/adder")
CPMAddPackage(NAME adder GITHUB_REPOSITORY cpm-cmake/testpack-adder VERSION 1.0.1
        SOURCE_DIR "${adder_checkout}" OPTIONS "ADDER_BUILD_TESTS OFF" "ADDER_BUILD_EXAMPLES"
        FIND_PACKAGE_ARGUMENTS "COMPONENTS first\;second")

if (NOT adder_SOURCE_DIR STREQUAL adder_checkout OR NOT EXISTS "${adder_checkout}/CMakeLists.txt")
    message(FATAL_ERROR "adder was not checked out into ${adder_checkout}: ${adder_SOURCE_DIR}")
endif ()

function(add_hashed_archive)
    CPMAddPackage(NAME hashed DOWNLOAD_ONLY
            URL https://github.com/cpm-cmake/testpack-adder/archive/refs/tags/v1.0.1.tar.gz
            URL_HASH SHA1=42853417f43007f46f5426295d4f1abe4df3178a)
endfunction()
add_hashed_archive()

add_executable(sample ../github/main.cpp)
target_link_libraries(sample PRIVATE adder)
