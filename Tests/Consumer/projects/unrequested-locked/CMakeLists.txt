cmake_minimum_required(VERSION 3.25)
set(CMAKE_PROJECT_TOP_LEVEL_INCLUDES ${EPM_MODULE})
project(UnusedLockedSample LANGUAGES NONE)

epm_add_package(NAME adder DOWNLOAD_ONLY
        URL https://github.com/cpm-cmake/testpack-adder/archive/refs/tags/v1.0.1.tar.gz)

# Stands in for a package only another platform asks for.
if (ASK_FOR_FIBONACCI)
    epm_add_package(NAME fibonacci DOWNLOAD_ONLY
            URL https://github.com/cpm-cmake/testpack-fibonacci/archive/refs/tags/v1.0.zip)
endif ()
