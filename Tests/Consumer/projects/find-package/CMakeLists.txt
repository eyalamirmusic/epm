cmake_minimum_required(VERSION 3.25)
set(CMAKE_PROJECT_TOP_LEVEL_INCLUDES ${EPM_MODULE})
project(FindPackageSample LANGUAGES NONE)

if (ADD_LISTER)
    epm_add_package(NAME lister GIT_REPOSITORY "${FIXTURE_URL}/lister.git"
            GIT_TAG main MODE source)
endif ()

if (FIND_NAME STREQUAL "shared")
    find_package(shared REQUIRED)

    if (NOT shared_FOUND OR NOT EXISTS "${shared_SOURCE_DIR}/shared.txt")
        message(FATAL_ERROR "shared_SOURCE_DIR is not the checkout of shared")
    endif ()
elseif (FIND_NAME STREQUAL "nowhere")
    find_package(nowhere REQUIRED)
elseif (FIND_NAME STREQUAL "Git")
    find_package(Git REQUIRED)
endif ()
