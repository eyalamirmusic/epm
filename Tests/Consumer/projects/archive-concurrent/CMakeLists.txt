cmake_minimum_required(VERSION 3.25)
set(EPM_LOCK_FILE "${CMAKE_BINARY_DIR}/epm.lock")
set(CMAKE_PROJECT_TOP_LEVEL_INCLUDES ${EPM_MODULE})
project(ArchiveConcurrentSample LANGUAGES NONE)

epm_add_package(NAME adder DOWNLOAD_ONLY
        URL https://github.com/cpm-cmake/testpack-adder/archive/refs/tags/v1.0.1.tar.gz)
