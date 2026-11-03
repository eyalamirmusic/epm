cmake_minimum_required(VERSION 3.25)
# A lock holding a package this configure does not ask for, as a lock written
# on another platform does: verify fetches it at its locked version and checks
# its hash, and fails, naming it, when it cannot fetch it. Expects PROJECT_DIR,
# WORK_DIR, EPM_EXECUTABLE and EPM_MODULE.

function(run expect_success)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE exit_code
            OUTPUT_VARIABLE output ERROR_VARIABLE output)
    message("${output}")

    if (expect_success AND NOT exit_code EQUAL 0)
        message(FATAL_ERROR "failed: ${ARGN}")
    elseif (NOT expect_success AND exit_code EQUAL 0)
        message(FATAL_ERROR "passed but should have failed: ${ARGN}")
    endif ()

    set(run_output "${output}" PARENT_SCOPE)
endfunction()

file(REMOVE_RECURSE "${WORK_DIR}")
set(lock "${WORK_DIR}/epm.lock")
set(common -S "${PROJECT_DIR}" "-DEPM_EXECUTABLE=${EPM_EXECUTABLE}"
        "-DEPM_MODULE=${EPM_MODULE}" "-DEPM_LOCK_FILE=${lock}")

# "Another platform" writes the lock with both packages.
run(TRUE "${CMAKE_COMMAND}" ${common} -B "${WORK_DIR}/build-other"
        "-DEPM_CACHE=${WORK_DIR}/cache-other" -DASK_FOR_FIBONACCI=ON)

# This platform asks for adder alone, from a cache of its own.
set(cache "${WORK_DIR}/cache")
run(TRUE "${CMAKE_COMMAND}" ${common} -B "${WORK_DIR}/build" "-DEPM_CACHE=${cache}")

# Offline, verify cannot fetch fibonacci, which this cache never had.
run(FALSE "${EPM_EXECUTABLE}" lock verify --full "--lock=${lock}" "--cache=${cache}"
        --mode=offline)

if (NOT run_output MATCHES "FAILED fibonacci: .*offline mode forbids")
    message(FATAL_ERROR "verify did not name fibonacci and why it could not fetch it")
endif ()

# Online, verify fetches fibonacci and checks it against the lock.
run(TRUE "${EPM_EXECUTABLE}" lock verify --full "--lock=${lock}" "--cache=${cache}")

if (NOT run_output MATCHES "ok fibonacci [0-9a-f]+ \\(fetched\\)")
    message(FATAL_ERROR "verify did not fetch and check fibonacci")
endif ()
