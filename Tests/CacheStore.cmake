# End-to-end self-test of cache stores with the real epm binary, over the
# fixtures of Tests/Relocate.cmake (Reloc, which takes RelocDep through CPM,
# committed into local git repositories):
#
#   built     a consumer of Reloc against <work>/cache-a builds Reloc's entry
#   pushed    `epm cache push <work>/store --cache cache-a` copies its Release
#             unit into the store; a second push copies nothing ("1 already
#             there"); a copy of the store with a corrupted archive is made
#             beside it, and the good store is made read-only (not on Windows)
#   pulled    a fresh consumer with an empty cache-b,
#             EPM_CACHE_STORES=<bad copy>;<store> and EPM_FETCH_SOURCES=OFF
#             configures, builds and runs without building Reloc or fetching
#             its source (no snapshot in cache-b's src/): the bad copy is
#             rejected with a note, the good
#             store's unit is copied into cache-b, `epm cache verify` passes
#             on it, the store is unchanged, and the step's input in
#             <build>/epm/steps/ names the stores, so a step run again by hand
#             sees them
#   env       the same with the EPM_CACHE_STORES and EPM_FETCH_SOURCES
#             environment variables and an empty cache-c
#   sources   without EPM_FETCH_SOURCES, the entry still comes from the store
#             and Reloc's snapshot is fetched for its SOURCE_DIR
#
#   cmake -DEPM_BINARY=<epm> [-DWORK_DIR=<dir>] [-DTEST_GENERATOR=<generator>]
#         [-DTEST_MAKE_PROGRAM=<path>] [-DTEST_CXX_COMPILER=<path>]
#         -P Tests/CacheStore.cmake

cmake_minimum_required(VERSION 3.25)

set(fixtures "${CMAKE_CURRENT_LIST_DIR}/Relocate")
get_filename_component(repo_dir "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

if (NOT EXISTS "${EPM_BINARY}")
    message(FATAL_ERROR "EPM_BINARY '${EPM_BINARY}' does not exist")
endif ()

find_program(git_executable git NO_CACHE)

if (NOT git_executable)
    message(STATUS "epm-cache-store: skipped: no git")
    return()
endif ()

if (NOT WORK_DIR)
    set(WORK_DIR "${CMAKE_CURRENT_BINARY_DIR}/epm-cache-store")
endif ()

set(scenario "setup")

function(fail text)
    message(FATAL_ERROR "epm-cache-store: ${scenario}: ${text}")
endfunction()

# A read-only store left by an earlier run must be writable to be removed.
function(make_writable path)
    if (NOT CMAKE_HOST_WIN32 AND EXISTS "${path}")
        execute_process(COMMAND chmod -R u+w "${path}")
    endif ()
endfunction()

make_writable("${WORK_DIR}")
file(REMOVE_RECURSE "${WORK_DIR}")

set(repos "${WORK_DIR}/repos")
set(consumer "${WORK_DIR}/Consumer")
set(common_args -DCMAKE_BUILD_TYPE=Release "-DEPM_EXECUTABLE=${EPM_BINARY}")

if (TEST_GENERATOR)
    list(APPEND common_args -G "${TEST_GENERATOR}")
endif ()

if (TEST_MAKE_PROGRAM)
    list(APPEND common_args "-DCMAKE_MAKE_PROGRAM=${TEST_MAKE_PROGRAM}")
endif ()

if (TEST_CXX_COMPILER)
    list(APPEND common_args "-DCMAKE_CXX_COMPILER=${TEST_CXX_COMPILER}")
endif ()

function(git directory)
    execute_process(
            COMMAND "${git_executable}" -c user.name=epm
            -c user.email=epm@example.invalid -c commit.gpgsign=false
            -c tag.gpgsign=false -c init.defaultBranch=main ${ARGN}
            WORKING_DIRECTORY "${directory}"
            RESULT_VARIABLE result
            OUTPUT_VARIABLE output
            ERROR_VARIABLE output)

    if (NOT result EQUAL 0)
        fail("git ${ARGN} in ${directory} failed:\n${output}")
    endif ()
endfunction()

function(make_repository name out_var)
    set(directory "${repos}/${name}")
    git("${directory}" init -q)
    git("${directory}" add -A)
    git("${directory}" commit -q -m "${name}")
    git("${directory}" tag v1)

    set(path "${directory}")

    if (NOT path MATCHES "^/")
        set(path "/${path}")
    endif ()

    set(${out_var} "file://${path}" PARENT_SCOPE)
endfunction()

file(COPY "${fixtures}/RelocDep/" DESTINATION "${repos}/RelocDep")
make_repository(RelocDep DEP_URL)

file(COPY "${fixtures}/Reloc/" DESTINATION "${repos}/Reloc" PATTERN "*.in" EXCLUDE)
file(COPY "${fixtures}/Reloc/RelocGenerated.h.in" DESTINATION "${repos}/Reloc")
file(COPY "${repo_dir}/CMake/CPM.cmake" DESTINATION "${repos}/Reloc/cmake")
configure_file("${fixtures}/Reloc/CMakeLists.txt.in"
        "${repos}/Reloc/CMakeLists.txt" @ONLY)
make_repository(Reloc RELOC_URL)

set(GET_EPM "${repo_dir}/CMake/get_epm.cmake")
file(COPY "${fixtures}/Consumer/Main.cpp" DESTINATION "${consumer}")
configure_file("${fixtures}/Consumer/CMakeLists.txt.in"
        "${consumer}/CMakeLists.txt" @ONLY)
configure_file("${fixtures}/Consumer/epm.cpp.in" "${consumer}/epm.cpp" @ONLY)

# Configures, builds and runs the consumer into ${WORK_DIR}/<name> with the
# cache `cache` and the extra arguments ARGN (before -S, so `cmake -E env`
# arguments may come first). Sets `configure_output`.
function(build_consumer name cache)
    set(build "${WORK_DIR}/${name}")
    execute_process(
            COMMAND ${ARGN} "${CMAKE_COMMAND}" -S "${consumer}" -B "${build}"
            ${common_args} "-DEPM_CACHE=${cache}" ${consumer_args}
            RESULT_VARIABLE result
            OUTPUT_VARIABLE output
            ERROR_VARIABLE output)

    if (NOT result EQUAL 0)
        fail("configure failed (${result}):\n${output}")
    endif ()

    set(configure_output "${output}" PARENT_SCOPE)

    execute_process(
            COMMAND "${CMAKE_COMMAND}" --build "${build}" --config Release
            RESULT_VARIABLE result
            OUTPUT_VARIABLE output
            ERROR_VARIABLE output)

    if (NOT result EQUAL 0)
        fail("build failed (${result}):\n${output}")
    endif ()

    set(app "")

    foreach (candidate IN ITEMS "${build}/app" "${build}/app.exe"
            "${build}/Release/app" "${build}/Release/app.exe")
        if (NOT app AND EXISTS "${candidate}" AND NOT IS_DIRECTORY "${candidate}")
            set(app "${candidate}")
        endif ()
    endforeach ()

    if (NOT app)
        fail("no app executable in ${build}")
    endif ()

    execute_process(COMMAND "${app}" RESULT_VARIABLE result
            OUTPUT_VARIABLE run_output ERROR_VARIABLE run_output)
    string(STRIP "${run_output}" run_output)

    if (NOT run_output STREQUAL "reloc ok 123")
        fail("app printed '${run_output}' (exit ${result}), expected 'reloc ok 123'")
    endif ()
endfunction()

# Runs `epm ARGN`, expecting exit code `expected`; sets `epm_output`.
function(run_epm expected)
    execute_process(COMMAND "${EPM_BINARY}" ${ARGN}
            RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE output)

    if (NOT result EQUAL expected)
        fail("epm ${ARGN} exited ${result}, expected ${expected}:\n${output}")
    endif ()

    set(epm_output "${output}" PARENT_SCOPE)
endfunction()

# The one entry of Reloc under `bin`, relative to it.
function(reloc_entry bin out_var)
    file(GLOB entries LIST_DIRECTORIES true RELATIVE "${bin}" "${bin}/Reloc/*/*")
    list(FILTER entries EXCLUDE REGEX "/\\.staging$")
    list(LENGTH entries count)

    if (NOT count EQUAL 1)
        fail("expected one entry of Reloc in ${bin}, found: ${entries}")
    endif ()

    set(${out_var} "${entries}" PARENT_SCOPE)
endfunction()

# Every path under `directory`, relative, sorted.
function(tree directory out_var)
    file(GLOB_RECURSE paths LIST_DIRECTORIES true RELATIVE "${directory}"
            "${directory}/*")
    list(SORT paths)
    set(${out_var} "${paths}" PARENT_SCOPE)
endfunction()

# Neither a snapshot of Reloc nor its marker in `cache`: its source was not
# fetched (the walk's checkout of its epm.cpp alone lives in src/Reloc/.recipes).
function(expect_no_snapshot cache)
    file(GLOB fetched LIST_DIRECTORIES true "${cache}/src/Reloc/*")
    list(FILTER fetched EXCLUDE REGEX "/\\.recipes$")

    if (fetched)
        fail("Reloc's source was fetched into ${cache}: ${fetched}")
    endif ()
endfunction()

function(expect_reused)
    if (configure_output MATCHES "building Reloc from source")
        fail("Reloc was built, not taken from the store:\n${configure_output}")
    endif ()

    if (NOT configure_output MATCHES "copied Release from [^\n]*store")
        fail("no copy from the store was reported:\n${configure_output}")
    endif ()
endfunction()

set(scenario "built")
set(cache_a "${WORK_DIR}/cache-a")
build_consumer(built "${cache_a}")
reloc_entry("${cache_a}/bin" entry)

set(scenario "pushed")
set(store "${WORK_DIR}/store")
run_epm(0 cache push "${store}" --cache "${cache_a}" Reloc)

if (NOT epm_output MATCHES "pushed 1 configs \\([^)]+\\) into 1 entries to ")
    fail("the first push:\n${epm_output}")
endif ()

reloc_entry("${store}/bin" store_entry)

if (NOT store_entry STREQUAL entry)
    fail("the store holds ${store_entry}, not ${entry}")
endif ()

run_epm(0 cache push "${store}" --cache "${cache_a}")

if (NOT epm_output MATCHES "pushed 0 configs \\(0 B\\) into 0 entries to [^;]+; 1 already there")
    fail("the second push was not a no-op:\n${epm_output}")
endif ()

run_epm(0 cache verify --cache "${store}")

set(bad_store "${WORK_DIR}/bad-store")
file(COPY "${store}/" DESTINATION "${bad_store}")
file(GLOB bad_archives "${bad_store}/bin/${entry}/configs/Release/lib/Release/*")

if (NOT bad_archives)
    fail("no archive in ${bad_store}/bin/${entry}")
endif ()

list(GET bad_archives 0 bad_archive)
file(APPEND "${bad_archive}" "corrupt")

tree("${store}" store_before)

if (NOT CMAKE_HOST_WIN32)
    execute_process(COMMAND chmod -R a-w "${store}" RESULT_VARIABLE result)

    if (NOT result EQUAL 0)
        fail("chmod -R a-w ${store} failed")
    endif ()
endif ()

set(scenario "pulled")
set(cache_b "${WORK_DIR}/cache-b")
set(consumer_args "-DEPM_CACHE_STORES=${bad_store}\;${store}"
        -DEPM_FETCH_SOURCES=OFF)
build_consumer(pulled "${cache_b}")
set(consumer_args "")
expect_reused()

if (NOT configure_output MATCHES "note: the cache store [^\n]*bad-store holds Release of Reloc")
    fail("the corrupt store was not reported:\n${configure_output}")
endif ()

reloc_entry("${cache_b}/bin" pulled_entry)

if (NOT pulled_entry STREQUAL entry)
    fail("cache-b holds ${pulled_entry}, not ${entry}")
endif ()

expect_no_snapshot("${cache_b}")
run_epm(0 cache verify --cache "${cache_b}")

if (NOT epm_output MATCHES "verified 1 configs in 1 entries: no problems")
    fail("epm cache verify of cache-b:\n${epm_output}")
endif ()

tree("${store}" store_after)

if (NOT store_after STREQUAL store_before)
    fail("the store changed:\nbefore: ${store_before}\nafter: ${store_after}")
endif ()

file(READ "${WORK_DIR}/pulled/epm/steps/Reloc.in.json" step_input)
file(REAL_PATH "${store}" store_real)

string(FIND "${step_input}" "${store_real}" store_at)

if (store_at EQUAL -1)
    fail("steps/Reloc.in.json does not name the store:\n${step_input}")
endif ()

set(scenario "env")
set(cache_c "${WORK_DIR}/cache-c")
build_consumer(env "${cache_c}" "${CMAKE_COMMAND}" -E env
        "EPM_CACHE_STORES=${store}" "EPM_FETCH_SOURCES=OFF")
expect_reused()
expect_no_snapshot("${cache_c}")

# By default a prebuilt package's snapshot is fetched as well, for its
# SOURCE_DIR: the entry still comes from the store, unbuilt.
set(scenario "sources")
set(cache_d "${WORK_DIR}/cache-d")
set(consumer_args "-DEPM_CACHE_STORES=${store}")
build_consumer(sources "${cache_d}")
set(consumer_args "")
expect_reused()
file(GLOB fetched LIST_DIRECTORIES true "${cache_d}/src/Reloc/*.json")

if (NOT fetched)
    fail("Reloc's source was not fetched into ${cache_d} for its SOURCE_DIR")
endif ()

make_writable("${store}")
message(STATUS "epm-cache-store: ok")
