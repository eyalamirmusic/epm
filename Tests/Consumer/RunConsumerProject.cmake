# Configures a sample project twice, first in update mode to write its lock and
# then in locked mode from a fresh build directory, then builds and runs it.
# Expects PROJECT_DIR, WORK_DIR, LOCKSTEP_EXECUTABLE, LOCKSTEP_SHIM and
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
        "-DLOCKSTEP_SHIM=${LOCKSTEP_SHIM}"
        "-DLOCKSTEP_EXECUTABLE=${LOCKSTEP_EXECUTABLE}"
        "-DLOCKSTEP_CACHE=${WORK_DIR}/cache"
        -DCMAKE_BUILD_TYPE=Release)

expect_failure("[^ ]+\\.(txt|cmake):[0-9]+ asks for .*, and there is no lockstep.lock yet; offline"
        "${CMAKE_COMMAND}" ${common_arguments} -B "${WORK_DIR}/build-offline-no-lock"
        -DLOCKSTEP_MODE=offline)

# Locked mode with no lock writes one, as npm install writes package-lock.json;
# a second locked configure then reads it and changes no byte.
run_step("${CMAKE_COMMAND}" ${common_arguments} -B "${WORK_DIR}/build-first")
if (NOT step_output MATCHES "lockstep: no lockstep.lock, so wrote one with ${EXPECTED_PACKAGES} packages; commit it.")
    message(FATAL_ERROR "the first configure did not report writing the lock")
endif ()
file(READ "${source}/lockstep.lock" first_lock)
run_step("${CMAKE_COMMAND}" ${common_arguments} -B "${WORK_DIR}/build-second")
file(READ "${source}/lockstep.lock" second_lock)
if (NOT first_lock STREQUAL second_lock OR step_output MATCHES "wrote one")
    message(FATAL_ERROR "the second locked configure rewrote the lock")
endif ()

run_step("${CMAKE_COMMAND}" ${common_arguments} -B "${WORK_DIR}/build-update"
        -DLOCKSTEP_MODE=update)

if (step_output MATCHES "locked [^ ]+ at  ")
    message(FATAL_ERROR "a 'locked' line names an empty version")
endif ()

file(READ "${source}/lockstep.lock" lock)
string(JSON package_count LENGTH "${lock}" packages)
if (NOT package_count EQUAL EXPECTED_PACKAGES)
    message(FATAL_ERROR "the lock holds ${package_count} packages, not ${EXPECTED_PACKAGES}:\n${lock}")
endif ()

file(REMOVE_RECURSE "${WORK_DIR}/cache/src")
run_step("${CMAKE_COMMAND}" ${common_arguments} -B "${WORK_DIR}/build-locked")
string(JSON root GET "${lock}" root)
if (NOT step_output MATCHES "lockstep: root ${root}")
    message(FATAL_ERROR "configure did not print 'lockstep: root ${root}'")
endif ()

run_step("${CMAKE_COMMAND}" --build "${WORK_DIR}/build-locked" --config Release)

find_program(sample NAMES sample PATHS "${WORK_DIR}/build-locked"
        "${WORK_DIR}/build-locked/Release" NO_DEFAULT_PATH REQUIRED)
run_step("${sample}")

run_step("${CMAKE_COMMAND}" -E env "LOCKSTEP_EXECUTABLE=${LOCKSTEP_EXECUTABLE}"
        "${LOCKSTEP_EXECUTABLE}" verify --full --lock "${source}/lockstep.lock"
        --cache "${WORK_DIR}/cache")

run_step("${CMAKE_COMMAND}" ${common_arguments} -B "${WORK_DIR}/build-offline"
        -DLOCKSTEP_MODE=offline)

string(JSON overridden_name GET "${lock}" packages 0 name)
expect_failure("LOCKSTEP_CI forbids"
        "${CMAKE_COMMAND}" ${common_arguments} -B "${WORK_DIR}/build-ci"
        -DLOCKSTEP_CI=ON "-DLOCKSTEP_${overridden_name}_SOURCE=${WORK_DIR}/source")

expect_failure("CPM_${overridden_name}_SOURCE=.* LOCKSTEP_CI forbids"
        "${CMAKE_COMMAND}" ${common_arguments} -B "${WORK_DIR}/build-ci-cpm"
        -DLOCKSTEP_CI=ON "-DCPM_${overridden_name}_SOURCE=${WORK_DIR}/source")

# A reconfigure takes LOCKSTEP_EXECUTABLE from the environment over its cache.
expect_failure("no such file or directory"
        "${CMAKE_COMMAND}" -E env "LOCKSTEP_EXECUTABLE=${WORK_DIR}/no-such-lockstep"
        "${CMAKE_COMMAND}" -S "${source}" -B "${WORK_DIR}/build-offline")

# The lock must not depend on where the project is checked out: write it again
# from a second checkout at another path, with the bootstrap inside each copy,
# and compare the bytes.
foreach (checkout first second-checkout)
    set(copy "${WORK_DIR}/${checkout}/project")
    file(REMOVE_RECURSE "${WORK_DIR}/${checkout}")
    file(COPY "${PROJECT_DIR}/" DESTINATION "${copy}")
    file(COPY "${PROJECT_DIR}/../github/main.cpp" DESTINATION "${WORK_DIR}/${checkout}/github")
    file(COPY "${LOCKSTEP_SHIM}" DESTINATION "${copy}/cmake")
    run_step("${CMAKE_COMMAND}" -S "${copy}" -B "${copy}/build" -DLOCKSTEP_MODE=update
            "-DLOCKSTEP_SHIM=${copy}/cmake/lockstep.cmake"
            "-DLOCKSTEP_EXECUTABLE=${LOCKSTEP_EXECUTABLE}"
            "-DLOCKSTEP_CACHE=${WORK_DIR}/cache")
    file(READ "${copy}/lockstep.lock" lock_${checkout})
endforeach ()

if (NOT lock_first STREQUAL lock_second-checkout)
    message(FATAL_ERROR "the lock differs between checkouts:\n${lock_first}\n---\n${lock_second-checkout}")
endif ()
if (lock_first MATCHES "${WORK_DIR}")
    message(FATAL_ERROR "the lock holds a path from this machine:\n${lock_first}")
endif ()
