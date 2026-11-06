# End-to-end self-test of relocatable binary entries with the real epm binary.
# Commits Tests/Relocate/RelocDep and Tests/Relocate/Reloc (which takes
# RelocDep through CPM) into local git repositories, then:
#
#   built     a consumer of Reloc configures, builds and runs against
#             <work>/cache-a, which builds Reloc's entry; no CMake file of the
#             entry names the cache, the headers it needs are in its include/
#             (an extensionless one included, sources and build files left
#             out), and with GCC or Clang its archive names no path of the
#             cache, only the prefix maps' /epm/... ones
#   moved     cache-a is renamed to cache-b, so nothing is left at the old
#             path; a fresh consumer against cache-b reuses the entry without
#             building, links and runs, and the moved entry's CMake files
#             name neither cache; `epm cache verify` passes on it, and fails
#             once its archive is corrupted
#
#   cmake -DEPM_BINARY=<epm> [-DWORK_DIR=<dir>] [-DTEST_GENERATOR=<generator>]
#         [-DTEST_MAKE_PROGRAM=<path>] [-DTEST_CXX_COMPILER=<path>]
#         -P Tests/Relocate.cmake

cmake_minimum_required(VERSION 3.25)

set(fixtures "${CMAKE_CURRENT_LIST_DIR}/Relocate")
get_filename_component(repo_dir "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

if (NOT EXISTS "${EPM_BINARY}")
    message(FATAL_ERROR "EPM_BINARY '${EPM_BINARY}' does not exist")
endif ()

find_program(git_executable git NO_CACHE)

if (NOT git_executable)
    message(STATUS "epm-relocate: skipped: no git")
    return()
endif ()

if (NOT WORK_DIR)
    set(WORK_DIR "${CMAKE_CURRENT_BINARY_DIR}/epm-relocate")
endif ()

file(REMOVE_RECURSE "${WORK_DIR}")

set(repos "${WORK_DIR}/repos")
set(consumer "${WORK_DIR}/Consumer")
set(scenario "setup")

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

function(fail text)
    message(FATAL_ERROR "epm-relocate: ${scenario}: ${text}")
endfunction()

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

# Commits everything in ${repos}/<name>, tags it v1 and sets <out_var> to its
# url.
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

# Configures, builds and runs the consumer into ${WORK_DIR}/<name> against the
# cache `cache`. Sets `configure_output`.
function(build_consumer name cache)
    set(build "${WORK_DIR}/${name}")
    execute_process(
            COMMAND "${CMAKE_COMMAND}" -S "${consumer}" -B "${build}"
            ${common_args} "-DEPM_CACHE=${cache}"
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

# The one entry of Reloc in `cache`, relative to it.
function(reloc_entry cache out_var)
    file(GLOB entries LIST_DIRECTORIES true RELATIVE "${cache}"
            "${cache}/bin/Reloc/*/*")

    list(LENGTH entries count)

    if (NOT count EQUAL 1)
        fail("expected one entry of Reloc in ${cache}, found: ${entries}")
    endif ()

    set(${out_var} "${entries}" PARENT_SCOPE)
endfunction()

# No CMake file of the entry at `prefix` names any of ARGN.
function(expect_cmake_files_name_none prefix)
    file(GLOB_RECURSE cmake_files "${prefix}/*.cmake")

    if (NOT cmake_files)
        fail("no CMake files in ${prefix}")
    endif ()

    foreach (file IN LISTS cmake_files)
        file(READ "${file}" text)

        foreach (path IN LISTS ARGN)
            string(FIND "${text}" "${path}" at)

            if (NOT at EQUAL -1)
                fail("${file} names ${path}:\n${text}")
            endif ()
        endforeach ()
    endforeach ()
endfunction()

function(expect_exists path)
    if (NOT EXISTS "${path}")
        fail("${path} does not exist")
    endif ()
endfunction()

function(expect_missing path)
    if (EXISTS "${path}")
        fail("${path} exists")
    endif ()
endfunction()

set(scenario "built")
set(cache_a "${WORK_DIR}/cache-a")
file(MAKE_DIRECTORY "${cache_a}")
file(REAL_PATH "${cache_a}" cache_a_real)
build_consumer(built "${cache_a}")
reloc_entry("${cache_a}" entry)
set(prefix_a "${cache_a}/${entry}")

expect_cmake_files_name_none("${prefix_a}" "${cache_a}" "${cache_a_real}")

# Release's unit: everything of the config, its headers included.
set(unit_a "${prefix_a}/configs/Release")
file(READ "${unit_a}/export-Release.cmake" export_text)

string(FIND "${export_text}" "_DIR}/include/cpm/" cpm_at)

if (cpm_at EQUAL -1)
    fail("export-Release.cmake takes nothing from include/cpm:\n${export_text}")
endif ()

expect_exists("${unit_a}/epm-config.json")
expect_exists("${unit_a}/include/src/include/reloc/RelocVersion")
expect_exists("${unit_a}/include/src/include/reloc/Reloc.h")
expect_exists("${unit_a}/gen/Release/generated/RelocGenerated.h")
expect_missing("${unit_a}/include/src/Reloc.cpp")
expect_missing("${unit_a}/include/src/CMakeLists.txt")
file(GLOB_RECURSE dep_headers "${unit_a}/include/cpm/*/RelocDep.h")

if (NOT dep_headers)
    fail("RelocDep.h is not under ${unit_a}/include/cpm")
endif ()

# GCC and Clang (an archive named lib*.a) got the prefix maps: the archive
# names the stable source path, not the cache's.
file(GLOB archives "${unit_a}/lib/Release/libReloc.a")

if (archives)
    file(STRINGS "${archives}" archive_strings REGEX "Reloc\\.cpp")

    foreach (path IN ITEMS "${cache_a}" "${cache_a_real}")
        string(FIND "${archive_strings}" "${path}" at)

        if (NOT at EQUAL -1)
            fail("libReloc.a names ${path}: ${archive_strings}")
        endif ()
    endforeach ()

    if (NOT archive_strings MATCHES "/epm/src/Reloc/Reloc\\.cpp")
        fail("libReloc.a does not name /epm/src/Reloc/Reloc.cpp: ${archive_strings}")
    endif ()
endif ()

set(scenario "moved")
set(cache_b "${WORK_DIR}/cache-b")
file(RENAME "${cache_a}" "${cache_b}")
expect_missing("${cache_a}")
build_consumer(moved "${cache_b}")

if (configure_output MATCHES "building Reloc from source")
    fail("the moved entry was not reused:\n${configure_output}")
endif ()

reloc_entry("${cache_b}" moved_entry)

if (NOT moved_entry STREQUAL entry)
    fail("the consumer used ${moved_entry}, not the moved ${entry}")
endif ()

file(REAL_PATH "${cache_b}" cache_b_real)
expect_cmake_files_name_none("${cache_b}/${entry}" "${cache_a}" "${cache_a_real}"
        "${cache_b}" "${cache_b_real}")

# The moved entry still hashes as it was published; a corrupted archive does
# not, and `epm cache verify` says so with a non-zero exit.
function(cache_verify cache expected_result out_var)
    execute_process(COMMAND "${EPM_BINARY}" cache verify --cache "${cache}"
            RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE output)

    if (NOT result EQUAL expected_result)
        fail("epm cache verify --cache ${cache} exited ${result}, expected "
                "${expected_result}:\n${output}")
    endif ()

    set(${out_var} "${output}" PARENT_SCOPE)
endfunction()

cache_verify("${cache_b}" 0 verified)

if (NOT verified MATCHES "verified 1 configs in [0-9]+ entries: no problems")
    fail("epm cache verify of the moved cache:\n${verified}")
endif ()

file(GLOB moved_archives "${cache_b}/${entry}/configs/Release/lib/Release/*")
list(GET moved_archives 0 moved_archive)
file(APPEND "${moved_archive}" "corrupt")
cache_verify("${cache_b}" 1 verified)

if (NOT verified MATCHES "Release: lib/Release/[^:]+: size [0-9]+, recorded")
    fail("epm cache verify did not report the corrupted archive:\n${verified}")
endif ()

message(STATUS "epm-relocate: ok")
