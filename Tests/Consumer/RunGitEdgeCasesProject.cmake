# Locks and re-fetches three local repositories: one with an empty
# .gitmodules, one whose .gitmodules names a path with no gitlink, and one
# storing a file in Git LFS, plus one patched with a patch whose context is a
# line short, which `patch -p1` applies (as CPM does) and `git apply` refuses. Expects WORK_DIR, LOCKSTEP_EXECUTABLE,
# LOCKSTEP_SHIM and PROJECT_DIR.

set(lfs_payload "content that lives in lfs, not a pointer\n")

function(git)
    execute_process(COMMAND git -c user.name=lockstep -c user.email=lockstep@example.com ${ARGN}
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
    set(work "${work}" PARENT_SCOPE)
endfunction()

function(commit_repository work)
    git(-C "${work}" add -A)
    git(-C "${work}" commit --quiet -m fixture)
endfunction()

function(configure expect_success)
    execute_process(COMMAND "${CMAKE_COMMAND}" -S "${PROJECT_DIR}" -B "${WORK_DIR}/build-${ARGV1}"
            "-DLOCKSTEP_SHIM=${LOCKSTEP_SHIM}" "-DLOCKSTEP_EXECUTABLE=${LOCKSTEP_EXECUTABLE}"
            "-DLOCKSTEP_CACHE=${WORK_DIR}/cache" "-DLOCKSTEP_LOCK_FILE=${WORK_DIR}/lockstep.lock"
            "-DFIXTURE_URL=${fixture_url}" "-DLFS_PAYLOAD=${lfs_payload}" ${ARGN}
            RESULT_VARIABLE exit_code OUTPUT_VARIABLE output ERROR_VARIABLE output)
    message("${output}")
    if (expect_success AND NOT exit_code EQUAL 0)
        message(FATAL_ERROR "configure failed")
    endif ()
endfunction()

file(REMOVE_RECURSE "${WORK_DIR}")

create_repository(emptyModules)
file(WRITE "${work}/.gitmodules" "")
commit_repository("${work}")

create_repository(danglingModule)
file(WRITE "${work}/.gitmodules" "[submodule \"ghost\"]\n\tpath = ghost\n\turl = ../ghost.git\n")
commit_repository("${work}")

create_repository(lfs)
git(-C "${work}" lfs install --local --skip-repo)
git(-C "${work}" lfs track payload.bin)
file(WRITE "${work}/payload.bin" "${lfs_payload}")
commit_repository("${work}")

create_repository(staleContext)
file(WRITE "${work}/source.h"
        "#pragma once\n\n#include <bit>\n#include <execution>\n#include <new>\n\n#ifndef NDEBUG\n#include <iostream>\n#endif\n")
commit_repository("${work}")

set(fixture_url "file://${WORK_DIR}/fixtures")
if (CMAKE_HOST_WIN32)
    set(fixture_url "file:///${WORK_DIR}/fixtures")
endif ()

configure(TRUE update -DLOCKSTEP_MODE=update)

file(READ "${WORK_DIR}/lockstep.lock" lock)
string(JSON package_count LENGTH "${lock}" packages)
math(EXPR last_package "${package_count} - 1")
foreach (index RANGE ${last_package})
    string(JSON name GET "${lock}" packages ${index} name)
    string(JSON uses_lfs GET "${lock}" packages ${index} usesGitLfs)
    string(JSON submodule_count LENGTH "${lock}" packages ${index} submodules)
    if (NOT submodule_count EQUAL 0)
        message(FATAL_ERROR "${name} locked submodules it does not have:\n${lock}")
    endif ()
    if (name STREQUAL "lfs" AND NOT uses_lfs)
        message(FATAL_ERROR "the lock does not record that lfs uses Git LFS:\n${lock}")
    elseif (NOT name STREQUAL "lfs" AND uses_lfs)
        message(FATAL_ERROR "the lock says ${name} uses Git LFS:\n${lock}")
    endif ()
endforeach ()

file(REMOVE_RECURSE "${WORK_DIR}/cache/src")
configure(TRUE locked)
