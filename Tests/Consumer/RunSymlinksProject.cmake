cmake_minimum_required(VERSION 3.25)
# Symlinks in an archive and in a git repository hash the same on every
# platform: Windows checks them out as plain files holding the link text, and
# the Merkle hash still counts them as symlinks. The git fixture's symlink is
# made with git plumbing, so it needs no symlink privilege to create. Expects
# PROJECT_DIR, WORK_DIR, EPM_EXECUTABLE and EPM_MODULE.

# The hashes macOS and Linux give these trees, with real symlinks.
set(expected_shine_hash 52f6aa2a093980a807d8ddc599c10c0eaed9ba917d309313c5fb4b73fcf82b6b)
set(expected_linked_hash dea633c61049212cfa833d0a8ac23c1ed5693421d319b1c68816227f449bcc5d)

function(git)
    execute_process(COMMAND git -c user.name=epm -c user.email=epm@example.com ${ARGN}
            RESULT_VARIABLE exit_code OUTPUT_VARIABLE output ERROR_VARIABLE output
            OUTPUT_STRIP_TRAILING_WHITESPACE)
    if (NOT exit_code EQUAL 0)
        message(FATAL_ERROR "git ${ARGN} failed:\n${output}")
    endif ()
    set(git_output "${output}" PARENT_SCOPE)
endfunction()

file(REMOVE_RECURSE "${WORK_DIR}")
set(linked "${WORK_DIR}/fixtures/linked.git")
file(MAKE_DIRECTORY "${linked}/docs")
# LF on every host, so the tree, and its hash, is the same everywhere.
file(CONFIGURE OUTPUT "${linked}/docs/target.txt" CONTENT "the target\n"
        NEWLINE_STYLE UNIX)
git(-C "${linked}" init --quiet -b main)
git(-C "${linked}" add -A)
file(WRITE "${WORK_DIR}/link-text" "docs/target.txt")
git(-C "${linked}" hash-object -w "${WORK_DIR}/link-text")
git(-C "${linked}" update-index --add --cacheinfo "120000,${git_output},readme-link")
git(-C "${linked}" commit --quiet -m fixture)

set(fixture_url "file://${WORK_DIR}/fixtures")
if (CMAKE_HOST_WIN32)
    set(fixture_url "file:///${WORK_DIR}/fixtures")
endif ()

execute_process(COMMAND "${CMAKE_COMMAND}" -S "${PROJECT_DIR}" -B "${WORK_DIR}/build"
        "-DEPM_EXECUTABLE=${EPM_EXECUTABLE}" "-DEPM_MODULE=${EPM_MODULE}"
        "-DEPM_CACHE=${WORK_DIR}/cache" "-DEPM_LOCK_FILE=${WORK_DIR}/epm-lock.json"
        "-DFIXTURE_URL=${fixture_url}"
        RESULT_VARIABLE exit_code OUTPUT_VARIABLE output ERROR_VARIABLE output)
message("${output}")
if (NOT exit_code EQUAL 0)
    message(FATAL_ERROR "the configure failed")
endif ()

file(READ "${WORK_DIR}/epm-lock.json" lock)
foreach (index 0 1)
    string(JSON name GET "${lock}" packages ${index} name)
    string(JSON hash GET "${lock}" packages ${index} hash)
    if (NOT hash STREQUAL expected_${name}_hash)
        message(FATAL_ERROR "${name} hashes to ${hash}, not ${expected_${name}_hash}")
    endif ()
endforeach ()

# A full rehash of the cached trees agrees too.
execute_process(COMMAND "${EPM_EXECUTABLE}" lock verify --full "--lock=${WORK_DIR}/epm-lock.json"
        "--cache=${WORK_DIR}/cache" RESULT_VARIABLE exit_code
        OUTPUT_VARIABLE output ERROR_VARIABLE output)
message("${output}")
if (NOT exit_code EQUAL 0)
    message(FATAL_ERROR "lock verify --full failed")
endif ()
