cmake_minimum_required(VERSION 3.25)
# The lock is an input of the configure: a build after the lock is deleted
# reconfigures and writes it again (or, with EPM_CI, fails saying to commit
# it), and a build after it is edited reconfigures and checks it. Expects
# PROJECT_DIR, WORK_DIR, EPM_EXECUTABLE and EPM_MODULE; GENERATOR optional.

function(run expect_success)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE exit_code
            OUTPUT_VARIABLE output ERROR_VARIABLE output)
    message("${output}")

    if (expect_success AND NOT exit_code EQUAL 0)
        message(FATAL_ERROR "failed: ${ARGN}")
    elseif (NOT expect_success AND exit_code EQUAL 0)
        message(FATAL_ERROR "passed but should have failed: ${ARGN}")
    endif ()

    string(REGEX REPLACE "[ \n]+" " " flattened "${output}")
    set(run_output "${flattened}" PARENT_SCOPE)
endfunction()

file(REMOVE_RECURSE "${WORK_DIR}")
set(lock "${WORK_DIR}/epm-lock.json")
set(build "${WORK_DIR}/build")
set(generator_arguments)

if (GENERATOR)
    set(generator_arguments -G "${GENERATOR}")
endif ()

run(TRUE "${CMAKE_COMMAND}" -S "${PROJECT_DIR}" -B "${build}" ${generator_arguments}
        "-DEPM_EXECUTABLE=${EPM_EXECUTABLE}" "-DEPM_MODULE=${EPM_MODULE}"
        "-DEPM_CACHE=${WORK_DIR}/cache" "-DEPM_LOCK_FILE=${lock}")
run(TRUE "${CMAKE_COMMAND}" --build "${build}")
file(READ "${lock}" written)

# Deleted: the next build reconfigures and writes the same lock again.
file(REMOVE "${lock}")
run(TRUE "${CMAKE_COMMAND}" --build "${build}")
file(READ "${lock}" rewritten)

if (NOT rewritten STREQUAL written)
    message(FATAL_ERROR "the rewritten lock differs:\n${written}\n---\n${rewritten}")
endif ()

# Deleted under EPM_CI: the build fails and says to commit the lock.
run(TRUE "${CMAKE_COMMAND}" -DEPM_CI=ON "${build}")
file(REMOVE "${lock}")
run(FALSE "${CMAKE_COMMAND}" --build "${build}")

if (NOT run_output MATCHES "EPM_CI forbids writing it.*commit the lock")
    message(FATAL_ERROR "the EPM_CI build did not say to commit the lock")
endif ()

file(WRITE "${lock}" "${written}")
run(TRUE "${CMAKE_COMMAND}" -DEPM_CI=OFF "${build}")

# Edited: the next build reconfigures, and locked mode fetches the commit the
# lock now names, which does not exist upstream, so the configure fails.
string(JSON commit GET "${written}" packages 0 commit)
string(REPLACE "${commit}" "e0e1a2b3c4d5e6f708192a3b4c5d6e7f80910203" edited "${written}")
file(WRITE "${lock}" "${edited}")
file(REMOVE_RECURSE "${WORK_DIR}/cache/src")
run(FALSE "${CMAKE_COMMAND}" --build "${build}")

if (NOT run_output MATCHES "Could not download adder at commit e0e1a2b3")
    message(FATAL_ERROR "the build after the edit did not check the edited lock")
endif ()
