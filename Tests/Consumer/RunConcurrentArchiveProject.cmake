# Configures one project in four build directories at once, sharing an empty
# cache, so both fetch the same archive together; both must succeed. Expects
# PROJECT_DIR, WORK_DIR, EPM_EXECUTABLE and EPM_MODULE.

file(REMOVE_RECURSE "${WORK_DIR}")

foreach (copy first second third fourth)
    list(APPEND configures COMMAND "${CMAKE_COMMAND}" -S "${PROJECT_DIR}"
            -B "${WORK_DIR}/build-${copy}" "-DEPM_EXECUTABLE=${EPM_EXECUTABLE}"
            "-DEPM_MODULE=${EPM_MODULE}" "-DEPM_CACHE=${WORK_DIR}/cache")
endforeach ()

# execute_process runs its COMMANDs at the same time, as a pipeline.
execute_process(${configures} RESULTS_VARIABLE exit_codes
        OUTPUT_VARIABLE output ERROR_VARIABLE output)
message("${output}")

if (NOT exit_codes STREQUAL "0;0;0;0")
    message(FATAL_ERROR "the concurrent configures exited with ${exit_codes}")
endif ()
