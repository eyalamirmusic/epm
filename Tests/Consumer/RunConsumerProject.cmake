cmake_minimum_required(VERSION 3.25)
# Configures a sample project twice, first in update mode to write its lock and
# then in locked mode from a fresh build directory, then builds and runs it.
# Expects PROJECT_DIR, WORK_DIR, EPM_EXECUTABLE, EPM_MODULE and
# EXPECTED_PACKAGES.

function(run_step)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE exit_code
            OUTPUT_VARIABLE output ERROR_VARIABLE output)
    message("${output}")
    if (NOT exit_code EQUAL 0)
        message(FATAL_ERROR "step failed: ${ARGN}")
    endif ()
    set(step_output "${output}" PARENT_SCOPE)
endfunction()

function(expect_failure expected_text)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE exit_code
            OUTPUT_VARIABLE output ERROR_VARIABLE output)
    if (exit_code EQUAL 0)
        message(FATAL_ERROR "expected a failure, but it passed: ${ARGN}")
    endif ()
    string(REGEX REPLACE "[ \n]+" " " flattened "${output}")
    if (NOT flattened MATCHES "${expected_text}")
        message(FATAL_ERROR "expected '${expected_text}' in:\n${output}")
    endif ()
endfunction()

file(REMOVE_RECURSE "${WORK_DIR}")
file(COPY "${PROJECT_DIR}/" DESTINATION "${WORK_DIR}/source")
file(COPY "${PROJECT_DIR}/../github/main.cpp" DESTINATION "${WORK_DIR}/github")

set(source "${WORK_DIR}/source")
set(common_arguments
        -S "${source}"
        "-DEPM_MODULE=${EPM_MODULE}"
        "-DEPM_EXECUTABLE=${EPM_EXECUTABLE}"
        "-DEPM_CACHE=${WORK_DIR}/cache"
        -DCMAKE_BUILD_TYPE=Release)

expect_failure("[^ ]+\\.(txt|cmake):[0-9]+ asks for .*, and there is no epm-lock.json yet; offline"
        "${CMAKE_COMMAND}" ${common_arguments} -B "${WORK_DIR}/build-offline-no-lock"
        -DEPM_LOCK_MODE=offline)

# Locked mode with no lock writes one, as npm install writes package-lock.json;
# a second locked configure then reads it and changes no byte.
run_step("${CMAKE_COMMAND}" ${common_arguments} -B "${WORK_DIR}/build-first")
if (NOT step_output MATCHES "epm: no epm-lock.json, so wrote one with ${EXPECTED_PACKAGES} packages; commit it.")
    message(FATAL_ERROR "the first configure did not report writing the lock")
endif ()
file(READ "${source}/epm-lock.json" first_lock)
run_step("${CMAKE_COMMAND}" ${common_arguments} -B "${WORK_DIR}/build-second")
file(READ "${source}/epm-lock.json" second_lock)
if (NOT first_lock STREQUAL second_lock OR step_output MATCHES "wrote one")
    message(FATAL_ERROR "the second locked configure rewrote the lock")
endif ()

run_step("${CMAKE_COMMAND}" ${common_arguments} -B "${WORK_DIR}/build-update"
        -DEPM_LOCK_MODE=update)

if (step_output MATCHES "locked [^ ]+ at  ")
    message(FATAL_ERROR "a 'locked' line names an empty version")
endif ()

file(READ "${source}/epm-lock.json" lock)
string(JSON package_count LENGTH "${lock}" packages)
if (NOT package_count EQUAL EXPECTED_PACKAGES)
    message(FATAL_ERROR "the lock holds ${package_count} packages, not ${EXPECTED_PACKAGES}:\n${lock}")
endif ()

file(REMOVE_RECURSE "${WORK_DIR}/cache/src")
run_step("${CMAKE_COMMAND}" ${common_arguments} -B "${WORK_DIR}/build-locked")
string(JSON root GET "${lock}" root)
if (NOT step_output MATCHES "epm: root ${root}")
    message(FATAL_ERROR "configure did not print 'epm: root ${root}'")
endif ()

run_step("${CMAKE_COMMAND}" --build "${WORK_DIR}/build-locked" --config Release)

find_program(sample NAMES sample PATHS "${WORK_DIR}/build-locked"
        "${WORK_DIR}/build-locked/Release" NO_DEFAULT_PATH REQUIRED)
run_step("${sample}")

run_step("${EPM_EXECUTABLE}" lock verify --full --lock "${source}/epm-lock.json"
        --cache "${WORK_DIR}/cache")

run_step("${CMAKE_COMMAND}" ${common_arguments} -B "${WORK_DIR}/build-offline"
        -DEPM_LOCK_MODE=offline)

string(JSON overridden_name GET "${lock}" packages 0 name)
expect_failure("EPM_CI forbids"
        "${CMAKE_COMMAND}" ${common_arguments} -B "${WORK_DIR}/build-ci"
        -DEPM_CI=ON "-DEPM_${overridden_name}_SOURCE=${WORK_DIR}/source")

expect_failure("CPM_${overridden_name}_SOURCE=.* EPM_CI forbids"
        "${CMAKE_COMMAND}" ${common_arguments} -B "${WORK_DIR}/build-ci-cpm"
        -DEPM_CI=ON "-DCPM_${overridden_name}_SOURCE=${WORK_DIR}/source")

# The lock must not depend on where the project is checked out: write it again
# from a second checkout at another path, with the bootstrap inside each copy,
# and compare the bytes.
foreach (checkout first second-checkout)
    set(copy "${WORK_DIR}/${checkout}/project")
    file(REMOVE_RECURSE "${WORK_DIR}/${checkout}")
    file(COPY "${PROJECT_DIR}/" DESTINATION "${copy}")
    file(COPY "${PROJECT_DIR}/../github/main.cpp" DESTINATION "${WORK_DIR}/${checkout}/github")
    get_filename_component(module_directory "${EPM_MODULE}" DIRECTORY)
    file(COPY "${EPM_MODULE}" "${module_directory}/epm-provider.cmake"
            DESTINATION "${copy}/cmake")
    run_step("${CMAKE_COMMAND}" -S "${copy}" -B "${copy}/build" -DEPM_LOCK_MODE=update
            "-DEPM_MODULE=${copy}/cmake/epm.cmake"
            "-DEPM_EXECUTABLE=${EPM_EXECUTABLE}"
            "-DEPM_CACHE=${WORK_DIR}/cache")
    file(READ "${copy}/epm-lock.json" lock_${checkout})
endforeach ()

if (NOT lock_first STREQUAL lock_second-checkout)
    message(FATAL_ERROR "the lock differs between checkouts:\n${lock_first}\n---\n${lock_second-checkout}")
endif ()
if (lock_first MATCHES "${WORK_DIR}")
    message(FATAL_ERROR "the lock holds a path from this machine:\n${lock_first}")
endif ()

# The same lock through a symlinked checkout, with the lock named by its real
# path: paths are compared with their symlinks resolved, so nothing changes.
file(CREATE_LINK "${WORK_DIR}/first" "${WORK_DIR}/linked" SYMBOLIC RESULT link_result)
if (link_result STREQUAL "0")
    file(REAL_PATH "${WORK_DIR}/first/project" real_copy)
    set(linked_copy "${WORK_DIR}/linked/project")
    run_step("${CMAKE_COMMAND}" -S "${linked_copy}" -B "${linked_copy}/build-linked"
            -DEPM_LOCK_MODE=update "-DEPM_LOCK_FILE=${real_copy}/epm-lock.json"
            "-DEPM_MODULE=${linked_copy}/cmake/epm.cmake"
            "-DEPM_EXECUTABLE=${EPM_EXECUTABLE}"
            "-DEPM_CACHE=${WORK_DIR}/cache")
    file(READ "${real_copy}/epm-lock.json" lock_linked)
    if (NOT lock_first STREQUAL lock_linked)
        message(FATAL_ERROR "the lock differs through a symlink:\n${lock_first}\n---\n${lock_linked}")
    endif ()
else ()
    message(STATUS "symlinked checkout: skipped, this machine cannot make a symlink")
endif ()
