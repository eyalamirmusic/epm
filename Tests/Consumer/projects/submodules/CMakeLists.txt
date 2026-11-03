cmake_minimum_required(VERSION 3.25)
set(CMAKE_PROJECT_TOP_LEVEL_INCLUDES ${LOCKSTEP_SHIM})
project(SubmoduleSample LANGUAGES NONE)

lockstep_add_package(NAME top GIT_REPOSITORY "${FIXTURE_URL}/top.git" GIT_TAG main
        DOWNLOAD_ONLY)
lockstep_add_package(NAME topAlone GIT_REPOSITORY "${FIXTURE_URL}/top.git" GIT_TAG main
        GIT_SUBMODULES "" DOWNLOAD_ONLY)

if (NOT EXISTS "${top_SOURCE_DIR}/middle/leaf/leaf.txt")
    message(FATAL_ERROR "the nested submodule was not checked out")
endif ()
if (EXISTS "${topAlone_SOURCE_DIR}/middle/middle.txt")
    message(FATAL_ERROR "GIT_SUBMODULES \"\" still checked out a submodule")
endif ()
