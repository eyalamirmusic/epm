cmake_minimum_required(VERSION 3.25)
set(CMAKE_PROJECT_TOP_LEVEL_INCLUDES ${EPM_MODULE})
project(LockInputSample LANGUAGES NONE)

epm_add_package(NAME adder GITHUB_REPOSITORY cpm-cmake/testpack-adder GIT_TAG v1.0.1
        DOWNLOAD_ONLY)
