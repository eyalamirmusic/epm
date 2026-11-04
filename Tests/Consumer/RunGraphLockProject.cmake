cmake_minimum_required(VERSION 3.25)
# The epm.cpp graph goes through the lock: every package it resolves is
# locked by commit and Merkle hash, a dependency's epm.cpp declaration takes
# part in minimal version selection with the root's, the root's overrides
# replace them, and the Merkle hash covers the dependency's epm.cpp, so the
# lock pins the recipe code that runs. Builds `shared` tagged v1.0.0 and
# v1.1.0 and `depender`, whose epm.cpp asks for shared v1.1.0, then configures
# a root whose epm.cpp asks for depender and shared v1.0.0. Expects PROJECT_DIR, WORK_DIR, EPM_EXECUTABLE
# and EPM_MODULE.

include("${CMAKE_CURRENT_LIST_DIR}/WriteRecipe.cmake")

function(git)
    execute_process(COMMAND git -c user.name=epm -c user.email=epm@example.com ${ARGN}
            RESULT_VARIABLE exit_code OUTPUT_VARIABLE output ERROR_VARIABLE output
            OUTPUT_STRIP_TRAILING_WHITESPACE)
    if (NOT exit_code EQUAL 0)
        message(FATAL_ERROR "git ${ARGN} failed:\n${output}")
    endif ()
    set(git_output "${output}" PARENT_SCOPE)
endfunction()

function(commit_repository work)
    git(-C "${work}" add -A)
    git(-C "${work}" commit --quiet -m fixture)
endfunction()

function(configure expect_success)
    execute_process(COMMAND "${CMAKE_COMMAND}" -S "${source}" -B "${source}/build-${ARGV1}"
            "-DEPM_EXECUTABLE=${EPM_EXECUTABLE}" "-DEPM_MODULE=${EPM_MODULE}"
            "-DEPM_CACHE=${WORK_DIR}/cache" ${ARGN}
            RESULT_VARIABLE exit_code OUTPUT_VARIABLE output ERROR_VARIABLE output)
    message("${output}")
    if (expect_success AND NOT exit_code EQUAL 0)
        message(FATAL_ERROR "${ARGV1}: the configure failed")
    elseif (NOT expect_success AND exit_code EQUAL 0)
        message(FATAL_ERROR "${ARGV1}: the configure passed but should have failed")
    endif ()
    string(REGEX REPLACE "[ \n]+" " " flattened "${output}")
    set(configure_output "${flattened}" PARENT_SCOPE)
endfunction()

function(read_locked_commit name)
    file(READ "${source}/epm-lock.json" lock)
    string(JSON count LENGTH "${lock}" packages)
    math(EXPR last "${count} - 1")
    foreach (index RANGE ${last})
        string(JSON package_name GET "${lock}" packages ${index} name)
        if (package_name STREQUAL name)
            string(JSON commit GET "${lock}" packages ${index} commit)
            string(JSON overridden GET "${lock}" packages ${index} isOverridden)
            set(locked_commit "${commit}" PARENT_SCOPE)
            set(locked_overridden "${overridden}" PARENT_SCOPE)
        endif ()
    endforeach ()
endfunction()

if (CMAKE_HOST_WIN32)
    message(STATUS "graph-lock: skipped: no epm.cpp recipes on Windows yet")
    return()
endif ()

file(REMOVE_RECURSE "${WORK_DIR}")
set(fixture_url "file://${WORK_DIR}/fixtures")
if (CMAKE_HOST_WIN32)
    set(fixture_url "file:///${WORK_DIR}/fixtures")
endif ()

set(shared_repository "${WORK_DIR}/fixtures/shared.git")
file(MAKE_DIRECTORY "${shared_repository}")
file(WRITE "${shared_repository}/CMakeLists.txt"
        "cmake_minimum_required(VERSION 3.25)\nproject(shared LANGUAGES NONE)\n")
file(WRITE "${shared_repository}/shared.txt" "one\n")
git(-C "${shared_repository}" init --quiet -b main)
commit_repository("${shared_repository}")
git(-C "${shared_repository}" tag v1.0.0)
git(-C "${shared_repository}" rev-parse HEAD)
set(first_commit "${git_output}")
file(WRITE "${shared_repository}/shared.txt" "two\n")
commit_repository("${shared_repository}")
git(-C "${shared_repository}" tag v1.1.0)
git(-C "${shared_repository}" rev-parse HEAD)
set(second_commit "${git_output}")

set(depender_repository "${WORK_DIR}/fixtures/depender.git")
file(MAKE_DIRECTORY "${depender_repository}")
file(WRITE "${depender_repository}/CMakeLists.txt"
        "cmake_minimum_required(VERSION 3.25)\nproject(depender LANGUAGES NONE)\n")
write_recipe("${depender_repository}/epm.cpp"
        "setMode(project.add(\"shared\", \"${fixture_url}/shared.git\", \"v1.1.0\"), \"source\");")
git(-C "${depender_repository}" init --quiet -b main)
commit_repository("${depender_repository}")

set(source "${WORK_DIR}/source")
file(COPY "${PROJECT_DIR}/" DESTINATION "${source}")
set(root_packages "setMode(project.add(\"depender\", \"${fixture_url}/depender.git\", \"main\"), \"source\");
        setMode(project.add(\"shared\", \"${fixture_url}/shared.git\", \"v1.0.0\"), \"source\");")
write_recipe("${source}/epm.cpp" "${root_packages}")

# The first configure writes the lock: shared at the higher of the two tags.
configure(TRUE first)
read_locked_commit(shared)
if (NOT locked_commit STREQUAL second_commit)
    message(FATAL_ERROR "shared is locked at ${locked_commit}, not v1.1.0's ${second_commit}")
endif ()
read_locked_commit(depender)
if (locked_commit STREQUAL "")
    message(FATAL_ERROR "depender is not in the lock")
endif ()

# A fresh locked configure reads the lock and changes no byte of it.
file(READ "${source}/epm-lock.json" written)
configure(TRUE locked)
file(READ "${source}/epm-lock.json" reread)
if (NOT written STREQUAL reread)
    message(FATAL_ERROR "the locked configure rewrote the lock")
endif ()

# The root's epm.cpp asks for another tag than the lock records: locked mode
# stops; update re-locks.
string(REPLACE "v1.0.0" "v0.9.0" changed_packages "${root_packages}")
write_recipe("${source}/epm.cpp" "${changed_packages}")
configure(FALSE disagrees)
if (NOT configure_output MATCHES "shared")
    message(FATAL_ERROR "the disagreeing configure did not name shared")
endif ()

# The root's overrides replace every requirement on shared.
write_recipe("${source}/epm.cpp" "${root_packages}
        project.overridePackage(\"shared\").commit = \"${first_commit}\";")
configure(TRUE override -DEPM_LOCK_MODE=update)
read_locked_commit(shared)
if (NOT locked_commit STREQUAL first_commit OR NOT locked_overridden)
    message(FATAL_ERROR "the override did not lock shared at ${first_commit}: ${locked_commit}")
endif ()

# EPM_CI refuses a package the lock does not hold, here one the root recipe
# adds after the lock was written.
write_recipe("${source}/epm.cpp" "${root_packages}
        project.overridePackage(\"shared\").commit = \"${first_commit}\";
        setMode(project.add(\"late\", \"${fixture_url}/shared.git\", \"v1.0.0\"), \"source\");")
configure(FALSE ciRefusesAnUnlockedPackage -DEPM_CI=ON)
if (NOT configure_output MATCHES "late")
    message(FATAL_ERROR "EPM_CI did not refuse the unlocked package late")
endif ()

# depender's epm.cpp is part of the tree its Merkle hash covers: a recipe
# changed in the cache no longer matches the lock.
file(READ "${source}/epm-lock.json" lock)
string(JSON count LENGTH "${lock}" packages)
math(EXPR last "${count} - 1")
foreach (index RANGE ${last})
    string(JSON package_name GET "${lock}" packages ${index} name)
    if (package_name STREQUAL "depender")
        string(JSON depender_hash GET "${lock}" packages ${index} hash)
    endif ()
endforeach ()
set(cached_recipe "${WORK_DIR}/cache/src/${depender_hash}/epm.cpp")
if (NOT EXISTS "${cached_recipe}")
    message(FATAL_ERROR "depender's epm.cpp is not in its cache entry ${cached_recipe}")
endif ()
file(APPEND "${cached_recipe}" "// changed after it was locked\n")
execute_process(COMMAND "${EPM_EXECUTABLE}" lock verify --full
        --lock "${source}/epm-lock.json" --cache "${WORK_DIR}/cache"
        RESULT_VARIABLE exit_code OUTPUT_VARIABLE output ERROR_VARIABLE output)
if (exit_code EQUAL 0 OR NOT output MATCHES "depender")
    message(FATAL_ERROR "lock verify --full did not catch depender's changed epm.cpp:\n${output}")
endif ()

# The cache trusts an entry whose files keep their sizes and modification
# times. A locked configure still re-hashes depender's epm.cpp before running
# it: a same-size edit with the time put back stops the configure, naming
# depender.
write_recipe("${source}/epm.cpp" "${root_packages}
        project.overridePackage(\"shared\").commit = \"${first_commit}\";")
configure(TRUE beforeTheEdit)
file(MAKE_DIRECTORY "${WORK_DIR}/original-time")
execute_process(COMMAND cp -p "${cached_recipe}" "${WORK_DIR}/original-time/epm.cpp"
        COMMAND_ERROR_IS_FATAL ANY)
file(READ "${cached_recipe}" recipe_text)
string(REPLACE "v1.1.0" "v1.0.0" recipe_text "${recipe_text}")
file(WRITE "${cached_recipe}" "${recipe_text}")
execute_process(COMMAND touch -r "${WORK_DIR}/original-time/epm.cpp" "${cached_recipe}"
        COMMAND_ERROR_IS_FATAL ANY)
configure(FALSE changedRecipe)
if (NOT configure_output MATCHES "depender's epm.cpp in .* is not the one .* records")
    message(FATAL_ERROR "the changed epm.cpp of depender was run:\n${configure_output}")
endif ()
