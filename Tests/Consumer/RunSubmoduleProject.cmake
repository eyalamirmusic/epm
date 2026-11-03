# Builds three local repositories, top -> middle -> leaf, linked by relative
# submodule URLs, then locks and re-fetches a project that uses top.
# Expects WORK_DIR, LOCKSTEP_EXECUTABLE, LOCKSTEP_SHIM and PROJECT_DIR.

function(git)
    execute_process(COMMAND git -c user.name=lockstep -c user.email=lockstep@example.com
            -c protocol.file.allow=always ${ARGN}
            RESULT_VARIABLE exit_code OUTPUT_VARIABLE output ERROR_VARIABLE output)
    if (NOT exit_code EQUAL 0)
        message(FATAL_ERROR "git ${ARGN} failed:\n${output}")
    endif ()
endfunction()

function(create_repository name)
    set(work "${WORK_DIR}/fixtures/${name}.git")
    file(MAKE_DIRECTORY "${work}")
    file(WRITE "${work}/${name}.txt" "${name}\n")
    git(-C "${work}" init --quiet -b main)
    foreach (child IN LISTS ARGN)
        git(-C "${work}" submodule --quiet add "../${child}.git" "${child}")
    endforeach ()
    git(-C "${work}" add -A)
    git(-C "${work}" commit --quiet -m "${name}")
endfunction()

function(configure expect_success)
    execute_process(COMMAND "${CMAKE_COMMAND}" -S "${PROJECT_DIR}" -B "${WORK_DIR}/build-${ARGV1}"
            "-DLOCKSTEP_SHIM=${LOCKSTEP_SHIM}" "-DLOCKSTEP_EXECUTABLE=${LOCKSTEP_EXECUTABLE}"
            "-DLOCKSTEP_CACHE=${WORK_DIR}/cache" "-DLOCKSTEP_LOCK_FILE=${WORK_DIR}/lockstep.lock"
            "-DFIXTURE_URL=${fixture_url}" ${ARGN}
            WORKING_DIRECTORY "${configure_working_directory}"
            RESULT_VARIABLE exit_code OUTPUT_VARIABLE output ERROR_VARIABLE output)
    message("${output}")
    if (expect_success AND NOT exit_code EQUAL 0)
        message(FATAL_ERROR "configure failed")
    elseif (NOT expect_success AND exit_code EQUAL 0)
        message(FATAL_ERROR "configure passed but should have failed")
    endif ()
    set(configure_output "${output}" PARENT_SCOPE)
endfunction()

file(REMOVE_RECURSE "${WORK_DIR}")
set(configure_working_directory "${WORK_DIR}")
create_repository(leaf)
create_repository(middle leaf)
create_repository(top middle)

set(fixture_url "file://${WORK_DIR}/fixtures")
if (CMAKE_HOST_WIN32)
    set(fixture_url "file:///${WORK_DIR}/fixtures")
endif ()

configure(TRUE update -DLOCKSTEP_MODE=update)

file(READ "${WORK_DIR}/lockstep.lock" lock)
string(JSON submodule_count LENGTH "${lock}" packages 0 submodules)
string(JSON nested_path GET "${lock}" packages 0 submodules 1 path)
if (NOT submodule_count EQUAL 2 OR NOT nested_path STREQUAL "middle/leaf")
    message(FATAL_ERROR "the lock does not record both submodules:\n${lock}")
endif ()

file(REMOVE_RECURSE "${WORK_DIR}/cache/src")
configure(TRUE locked)

# A configure run from inside a worktree whose .git names a directory that does
# not exist, as in a container with only the worktree mounted.
set(broken_worktree "${WORK_DIR}/broken-worktree")
file(MAKE_DIRECTORY "${broken_worktree}")
file(WRITE "${broken_worktree}/.git" "gitdir: ${WORK_DIR}/missing/.git/worktrees/x\n")
file(REMOVE_RECURSE "${WORK_DIR}/cache/src")
set(configure_working_directory "${broken_worktree}")
configure(TRUE broken-worktree)
set(configure_working_directory "${WORK_DIR}")

string(JSON locked_commit GET "${lock}" packages 0 submodules 1 commit)
string(REPLACE "${locked_commit}" "0000000000000000000000000000000000000000" tampered "${lock}")
file(WRITE "${WORK_DIR}/lockstep.lock" "${tampered}")
file(REMOVE_RECURSE "${WORK_DIR}/cache/src")
configure(FALSE tampered)
string(REGEX REPLACE "[ \n]+" " " flattened "${configure_output}")
if (NOT flattened MATCHES "submodule middle/leaf is at")
    message(FATAL_ERROR "a changed submodule commit was not reported")
endif ()
