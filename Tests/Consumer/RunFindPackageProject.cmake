cmake_minimum_required(VERSION 3.25)
# find_package of a name outside the graph: added from a lock in the graph,
# refused under EPM_CI and when nothing says where it comes from, and left to
# CMake's search only when the root epm.cpp adds it with addSystem. Expects PROJECT_DIR,
# WORK_DIR, EPM_EXECUTABLE and EPM_MODULE.

include("${CMAKE_CURRENT_LIST_DIR}/WriteRecipe.cmake")

function(git)
    execute_process(COMMAND git -c user.name=epm -c user.email=epm@example.com ${ARGN}
            RESULT_VARIABLE exit_code OUTPUT_VARIABLE output ERROR_VARIABLE output
            OUTPUT_STRIP_TRAILING_WHITESPACE)
    if (NOT exit_code EQUAL 0)
        message(FATAL_ERROR "git ${ARGN} failed:\n${output}")
    endif ()
    set(git_output "${output}" PARENT_SCOPE)
endfunction()

function(create_repository work)
    git(-C "${work}" init --quiet -b main)
    git(-C "${work}" add -A)
    git(-C "${work}" commit --quiet -m fixture)
endfunction()

# Configures a fresh copy named `scenario` with `recipe` as the describe() body
# of its epm.cpp when not empty; the rest of ARGN are -D arguments.
function(configure scenario expect_success recipe)
    set(source "${WORK_DIR}/${scenario}")
    file(REMOVE_RECURSE "${source}")
    file(COPY "${PROJECT_DIR}/" DESTINATION "${source}")

    if (NOT recipe STREQUAL "")
        write_recipe("${source}/epm.cpp" "${recipe}")
    endif ()

    execute_process(COMMAND "${CMAKE_COMMAND}" -S "${source}" -B "${source}/build"
            "-DEPM_EXECUTABLE=${EPM_EXECUTABLE}" "-DEPM_MODULE=${EPM_MODULE}"
            "-DEPM_CACHE=${WORK_DIR}/cache" "-DFIXTURE_URL=${fixture_url}" ${ARGN}
            RESULT_VARIABLE exit_code OUTPUT_VARIABLE output ERROR_VARIABLE output)
    message("${output}")
    string(REGEX REPLACE "[ \n]+" " " flattened "${output}")

    if (expect_success AND NOT exit_code EQUAL 0)
        message(FATAL_ERROR "${scenario}: the configure failed")
    elseif (NOT expect_success AND exit_code EQUAL 0)
        message(FATAL_ERROR "${scenario}: the configure passed but should have failed")
    endif ()

    set(configure_output "${flattened}" PARENT_SCOPE)
    set(scenario_source "${source}" PARENT_SCOPE)
endfunction()

file(REMOVE_RECURSE "${WORK_DIR}")
set(fixture_url "file://${WORK_DIR}/fixtures")
if (CMAKE_HOST_WIN32)
    set(fixture_url "file:///${WORK_DIR}/fixtures")
endif ()

set(shared_repository "${WORK_DIR}/fixtures/shared.git")
file(MAKE_DIRECTORY "${shared_repository}")
file(WRITE "${shared_repository}/shared.txt" "one\n")
file(WRITE "${shared_repository}/CMakeLists.txt"
        "cmake_minimum_required(VERSION 3.25)\nproject(shared LANGUAGES NONE)\n"
        "add_library(shared INTERFACE)\n")
create_repository("${shared_repository}")
git(-C "${shared_repository}" rev-parse HEAD)
set(shared_commit "${git_output}")

# lister ships a lock naming shared and never adds it.
set(lister_repository "${WORK_DIR}/fixtures/lister.git")
file(MAKE_DIRECTORY "${lister_repository}")
file(WRITE "${lister_repository}/CMakeLists.txt"
        "cmake_minimum_required(VERSION 3.25)\nproject(lister LANGUAGES NONE)\n")
file(WRITE "${lister_repository}/epm-lock.json"
        "{\"packages\":[{\"name\":\"shared\",\"kind\":\"git\",\"url\":\"${fixture_url}/shared.git\",\"requestedRef\":\"main\",\"commit\":\"${shared_commit}\",\"hash\":\"\"}],\"root\":\"\",\"version\":1}\n")
create_repository("${lister_repository}")

# 1. Locked mode with a lock that lacks shared: added from lister's lock, and
#    the lock now holds it.
configure(fromLock TRUE "" -DADD_LISTER=ON -DFIND_NAME=shared)
file(READ "${scenario_source}/epm-lock.json" lock)
if (NOT lock MATCHES "\"name\": \"shared\"")
    message(FATAL_ERROR "fromLock: the lock does not hold shared:\n${lock}")
endif ()

# 2. The same under EPM_CI, with a lock written before the find_package was
#    there: stopped, naming the find_package line.
configure(ciStops TRUE "" -DADD_LISTER=ON)
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${scenario_source}" -B "${scenario_source}/build-ci"
        "-DEPM_EXECUTABLE=${EPM_EXECUTABLE}" "-DEPM_MODULE=${EPM_MODULE}"
        "-DEPM_CACHE=${WORK_DIR}/cache" "-DFIXTURE_URL=${fixture_url}"
        -DADD_LISTER=ON -DFIND_NAME=shared -DEPM_CI=ON
        RESULT_VARIABLE exit_code OUTPUT_VARIABLE output ERROR_VARIABLE output)
string(REGEX REPLACE "[ \n]+" " " flattened "${output}")
if (exit_code EQUAL 0 OR NOT flattened MATCHES "CMakeLists.txt:11 asks for shared")
    message(FATAL_ERROR "ciStops: EPM_CI did not stop at CMakeLists.txt:11:\n${output}")
endif ()

# 3. A name nothing in the graph knows: stopped, with the lines to paste.
configure(unknown FALSE "" -DFIND_NAME=nowhere)
if (NOT configure_output MATCHES "CMakeLists.txt:17: find_package\\(nowhere\\) names a package that is not in the graph"
        OR NOT configure_output MATCHES "project.addSystem\\(\"nowhere\"\\)")
    message(FATAL_ERROR "unknown: the error does not name the call and the fix")
endif ()

if (CMAKE_HOST_WIN32)
    message(STATUS "find-package: addSystem skipped: no epm.cpp recipes on Windows yet")
    return()
endif ()

# 4. Added with addSystem in epm.cpp: CMake's own search finds it.
configure(system TRUE "project.addSystem(\"Git\");"
        -DFIND_NAME=Git)
