cmake_minimum_required(VERSION 3.25)
set(CMAKE_PROJECT_TOP_LEVEL_INCLUDES ${EPM_MODULE})
project(RequirementsSample LANGUAGES NONE)

foreach (dependency IN LISTS DEPENDENCIES)
    epm_add_package(NAME ${dependency} GIT_REPOSITORY "${FIXTURE_URL}/${dependency}.git"
            GIT_TAG main MODE source)
endforeach ()
