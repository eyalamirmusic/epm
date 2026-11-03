# Configures a project whose archive has a wrong URL_HASH, from an empty cache,
# and checks the configure ends with the hash mismatch rather than a generic
# download failure. Expects PROJECT_DIR, WORK_DIR, LOCKSTEP_EXECUTABLE and
# LOCKSTEP_SHIM.

file(REMOVE_RECURSE "${WORK_DIR}")
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${PROJECT_DIR}" -B "${WORK_DIR}/build"
        "-DLOCKSTEP_SHIM=${LOCKSTEP_SHIM}"
        "-DLOCKSTEP_EXECUTABLE=${LOCKSTEP_EXECUTABLE}"
        "-DLOCKSTEP_CACHE=${WORK_DIR}/cache"
        -DLOCKSTEP_MODE=update
        RESULT_VARIABLE exit_code OUTPUT_VARIABLE output ERROR_VARIABLE output)
message("${output}")

if (exit_code EQUAL 0)
    message(FATAL_ERROR "a wrong URL_HASH configured without an error")
endif ()

string(REGEX REPLACE "[ \n]+" " " flattened "${output}")
if (NOT flattened MATCHES "hashes to SHA256=[0-9a-f]+, not the expected SHA256=0+")
    message(FATAL_ERROR "the configure did not end with the hash mismatch")
endif ()
if (flattened MATCHES "Could not download")
    message(FATAL_ERROR "the hash mismatch was reported as a download failure")
endif ()
