cmake_minimum_required(VERSION 3.24)
set(CMAKE_PROJECT_TOP_LEVEL_INCLUDES ${LOCKSTEP_SHIM})
project(ArchiveBadHashSample LANGUAGES NONE)

lockstep_add_package(NAME adder
        URL https://github.com/cpm-cmake/testpack-adder/archive/refs/tags/v1.0.1.tar.gz
        URL_HASH SHA256=0000000000000000000000000000000000000000000000000000000000000000)
