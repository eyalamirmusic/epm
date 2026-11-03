# A lock holding a package this configure does not ask for, as a lock written
# on another platform does: verify skips it when it is not in the cache, and
# still fails for a package the configure asked for that is missing. Expects
# PROJECT_DIR, WORK_DIR, EPM_EXECUTABLE and EPM_MODULE.

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
run(TRUE "${EPM_EXECUTABLE}" lock verify --full "--lock=${lock}"
        "--build-dir=${WORK_DIR}/build" "--cache=${cache}")

if (NOT run_output MATCHES "skipped fibonacci: not used on this platform")
    message(FATAL_ERROR "verify did not report fibonacci as skipped")
endif ()

# A package this configure asked for that is gone from the cache still fails.
file(GLOB trees LIST_DIRECTORIES TRUE "${cache}/src/*")
file(REMOVE_RECURSE ${trees})
run(FALSE "${EPM_EXECUTABLE}" lock verify --full "--lock=${lock}"
        "--build-dir=${WORK_DIR}/build" "--cache=${cache}")

if (NOT run_output MATCHES "FAILED adder")
    message(FATAL_ERROR "verify did not fail for adder, which the configure asked for")
endif ()
