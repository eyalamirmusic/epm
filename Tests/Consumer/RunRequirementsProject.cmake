cmake_minimum_required(VERSION 3.25)
# Minimal version selection across a dependency graph. Builds a package
# `shared` tagged v1.0.0 and v1.1.0, and dependencies that each ask for it
# differently, then configures a root project over some of them. Expects
# PROJECT_DIR, WORK_DIR, EPM_EXECUTABLE and EPM_MODULE.

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

# A dependency whose CMakeLists.txt asks for shared at `ref`, with any extra
# files given as name/content pairs.
function(create_dependency name ref)
    set(work "${WORK_DIR}/fixtures/${name}.git")
    file(MAKE_DIRECTORY "${work}")
    file(WRITE "${work}/CMakeLists.txt"
            "cmake_minimum_required(VERSION 3.25)\nproject(${name} LANGUAGES NONE)\n"
            "epm_add_package(NAME shared GIT_REPOSITORY \"${fixture_url}/shared.git\" "
            "GIT_TAG ${ref} DOWNLOAD_ONLY)\n")
    set(extra ${ARGN})
    while (extra)
        list(POP_FRONT extra file_name content)
        file(WRITE "${work}/${file_name}" "${content}")
    endwhile ()
    git(-C "${work}" init --quiet -b main)
    commit_repository("${work}")
endfunction()

# Configures the root over `dependencies` in a fresh copy named `scenario`,
# with `manifest` as its epm.json when not empty.
function(configure scenario expect_success dependencies manifest)
    set(source "${WORK_DIR}/${scenario}")
    file(REMOVE_RECURSE "${source}")
    file(COPY "${PROJECT_DIR}/" DESTINATION "${source}")

    if (NOT manifest STREQUAL "")
        file(WRITE "${source}/epm.json" "${manifest}")
    endif ()

    foreach (attempt 1 2)
        execute_process(COMMAND "${CMAKE_COMMAND}" -S "${source}" -B "${source}/build"
                "-DEPM_EXECUTABLE=${EPM_EXECUTABLE}" "-DEPM_MODULE=${EPM_MODULE}"
                "-DEPM_CACHE=${WORK_DIR}/cache" "-DFIXTURE_URL=${fixture_url}"
                "-DDEPENDENCIES=${dependencies}"
                RESULT_VARIABLE exit_code OUTPUT_VARIABLE output ERROR_VARIABLE output)
        message("${output}")
        string(REGEX REPLACE "[ \n]+" " " flattened "${output}")

        # A requirement raised after its package was added asks for one more.
        if (exit_code EQUAL 0 OR NOT flattened MATCHES "Configure again")
            break ()
        endif ()
    endforeach ()

    if (expect_success AND NOT exit_code EQUAL 0)
        message(FATAL_ERROR "${scenario}: the configure failed")
    elseif (NOT expect_success AND exit_code EQUAL 0)
        message(FATAL_ERROR "${scenario}: the configure passed but should have failed")
    endif ()

    set(configure_output "${flattened}" PARENT_SCOPE)

    if (exit_code EQUAL 0)
        file(READ "${source}/epm-lock.json" lock)
        set(lock "${lock}" PARENT_SCOPE)
    endif ()
endfunction()

# The lock's entry for shared: its commit and whether an override chose it.
function(read_shared lock)
    string(JSON count LENGTH "${lock}" packages)
    math(EXPR last "${count} - 1")
    foreach (index RANGE ${last})
        string(JSON name GET "${lock}" packages ${index} name)
        if (name STREQUAL "shared")
            string(JSON commit GET "${lock}" packages ${index} commit)
            string(JSON ref GET "${lock}" packages ${index} requestedRef)
            string(JSON overridden GET "${lock}" packages ${index} isOverridden)
            set(shared_commit "${commit}" PARENT_SCOPE)
            set(shared_ref "${ref}" PARENT_SCOPE)
            set(shared_overridden "${overridden}" PARENT_SCOPE)
        endif ()
    endforeach ()
endfunction()

file(REMOVE_RECURSE "${WORK_DIR}")
set(fixture_url "file://${WORK_DIR}/fixtures")
if (CMAKE_HOST_WIN32)
    set(fixture_url "file:///${WORK_DIR}/fixtures")
endif ()

set(shared_repository "${WORK_DIR}/fixtures/shared.git")
file(MAKE_DIRECTORY "${shared_repository}")
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

create_dependency(olderTag v1.0.0)
create_dependency(newerTag v1.1.0)
create_dependency(firstCommit ${first_commit})
create_dependency(secondCommit ${second_commit})
create_dependency(shipsLock main epm-lock.json
        "{\"packages\":[{\"name\":\"shared\",\"kind\":\"git\",\"url\":\"${fixture_url}/shared.git\",\"requestedRef\":\"main\",\"commit\":\"${first_commit}\",\"hash\":\"\"}],\"root\":\"\",\"version\":1}\n")
create_dependency(shipsManifest v1.0.0 epm.json
        "{\"overrides\":{\"shared\":{\"commit\":\"${second_commit}\"}}}\n")

# 1. Two compatible tags: one shared, at the higher.
configure(tags TRUE "olderTag;newerTag" "")
read_shared("${lock}")
if (NOT shared_ref STREQUAL "v1.1.0" OR NOT shared_commit STREQUAL second_commit)
    message(FATAL_ERROR "tags: shared is at '${shared_ref}' (${shared_commit}), not v1.1.0")
endif ()

# 2. Two commits: the configure stops and names both requesters.
configure(commits FALSE "firstCommit;secondCommit" "")
if (NOT configure_output MATCHES "disagree on shared: firstCommit asks for .* secondCommit asks for .*epm.json")
    message(FATAL_ERROR "commits: the conflict did not name both requesters")
endif ()

# 3. The same two commits settled by the root's epm.json.
configure(override TRUE "firstCommit;secondCommit"
        "{\"overrides\":{\"shared\":{\"commit\":\"${second_commit}\"}}}\n")
read_shared("${lock}")
if (NOT shared_commit STREQUAL second_commit OR NOT shared_overridden)
    message(FATAL_ERROR "override: shared is at ${shared_commit}, overridden '${shared_overridden}'")
endif ()

# 4. A dependency's own lock is its requirement: shipsLock asks for main, but
# its lock says the first commit.
configure(shippedLock TRUE "shipsLock" "")
read_shared("${lock}")
if (NOT shared_commit STREQUAL first_commit)
    message(FATAL_ERROR "shippedLock: shared is at ${shared_commit}, not the shipped ${first_commit}")
endif ()

# 5. A dependency's epm.json is ignored: shipsManifest overrides shared to the
# second commit, but only the root's overrides count.
configure(dependencyManifest TRUE "shipsManifest" "")
read_shared("${lock}")
if (NOT shared_commit STREQUAL first_commit OR shared_overridden)
    message(FATAL_ERROR "dependencyManifest: shared is at ${shared_commit}, overridden '${shared_overridden}'")
endif ()
