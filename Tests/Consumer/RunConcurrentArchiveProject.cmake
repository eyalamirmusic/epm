# Configures one project in four build directories at once, sharing an empty
# cache, so all of them fetch the same archive together; all must succeed.
# Expects PROJECT_DIR, WORK_DIR, EPM_EXECUTABLE and EPM_MODULE. With COPY set,
# this script is one of those configures, writing its output to a log rather
# than to the pipe execute_process joins its commands with.

set(cache "${WORK_DIR}/cache")

if (COPY)
    execute_process(COMMAND "${CMAKE_COMMAND}" -S "${PROJECT_DIR}"
            -B "${WORK_DIR}/build-${COPY}" "-DEPM_EXECUTABLE=${EPM_EXECUTABLE}"
            "-DEPM_MODULE=${EPM_MODULE}" "-DEPM_CACHE=${cache}"
            RESULT_VARIABLE exit_code
            OUTPUT_FILE "${WORK_DIR}/${COPY}.log" ERROR_FILE "${WORK_DIR}/${COPY}.log")

    if (NOT exit_code EQUAL 0)
        message(FATAL_ERROR "configure ${COPY} failed")
    endif ()

    return()
endif ()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")
set(copies first second third fourth)

foreach (copy IN LISTS copies)
    list(APPEND configures COMMAND "${CMAKE_COMMAND}" "-DCOPY=${copy}"
            "-DPROJECT_DIR=${PROJECT_DIR}" "-DWORK_DIR=${WORK_DIR}"
            "-DEPM_EXECUTABLE=${EPM_EXECUTABLE}" "-DEPM_MODULE=${EPM_MODULE}"
            -P "${CMAKE_CURRENT_LIST_FILE}")
endforeach ()

# execute_process runs its COMMANDs at the same time, as a pipeline.
execute_process(${configures} RESULTS_VARIABLE exit_codes)

foreach (copy IN LISTS copies)
    file(READ "${WORK_DIR}/${copy}.log" log)
    message("--- ${copy}\n${log}")
endforeach ()

if (NOT exit_codes STREQUAL "0;0;0;0")
    message(FATAL_ERROR "the concurrent configures exited with ${exit_codes}")
endif ()
