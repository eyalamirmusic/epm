# End-to-end self-test of nested child builds with the real
# epm binary. Commits Tests/Nested/Leaf and Tests/Nested/Mid (whose epm.cpp
# declares Leaf) into local git repositories, then configures, builds and runs
# Tests/Nested/Consumer (whose epm.cpp declares only Mid) once per scenario.
# The binary compiles both recipes with the compiler that built it:
#
#   default       both prebuilt; Mid's child build loads Leaf through the
#                 injected provider; Mid's entry records Leaf's fingerprint in
#                 deps and does not re-export Leaf
#   cached        a fresh build directory reuses both entries
#   source        EPM_MODE=source: both added as subdirectories
#   leaf-source   EPM_Leaf_MODE=source forces Mid into source
#   leaf-checkout EPM_Leaf_SOURCE=<checkout>: Leaf's repository is hidden, so
#                 the checkout is used without a fetch
#
# Each scenario also builds the consumer's epm_add_recipe module and checks
# that Mid's epm_add_recipe built nothing; the default one runs its describe
# test.
#
#   cmake -DEPM_BINARY=<epm> [-DWORK_DIR=<dir>] [-DTEST_GENERATOR=<generator>]
#         [-DTEST_MAKE_PROGRAM=<path>] [-DTEST_CXX_COMPILER=<path>]
#         -P Tests/GraphNested.cmake

cmake_minimum_required(VERSION 3.25)

set(fixtures "${CMAKE_CURRENT_LIST_DIR}/Nested")
get_filename_component(repo_dir "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

if (NOT EXISTS "${EPM_BINARY}")
    message(FATAL_ERROR "EPM_BINARY '${EPM_BINARY}' does not exist")
endif ()

if (CMAKE_HOST_WIN32)
    message(STATUS "epm-graph-nested: skipped: no epm.cpp recipes on Windows yet")
    return()
endif ()

find_program(git_executable git NO_CACHE)

if (NOT git_executable)
    message(STATUS "epm-graph-nested: skipped: no git")
    return()
endif ()

if (NOT WORK_DIR)
    set(WORK_DIR "${CMAKE_CURRENT_BINARY_DIR}/epm-graph-nested")
endif ()

file(REMOVE_RECURSE "${WORK_DIR}")

set(repos "${WORK_DIR}/repos")
set(cache "${WORK_DIR}/cache")
set(consumer "${WORK_DIR}/Consumer")
set(leaf_checkout "${WORK_DIR}/LeafCheckout")

set(consumer_args -DCMAKE_BUILD_TYPE=Release "-DEPM_EXECUTABLE=${EPM_BINARY}"
        "-DEPM_CACHE=${cache}")

if (TEST_GENERATOR)
    list(APPEND consumer_args -G "${TEST_GENERATOR}")
endif ()

if (TEST_MAKE_PROGRAM)
    list(APPEND consumer_args "-DCMAKE_MAKE_PROGRAM=${TEST_MAKE_PROGRAM}")
endif ()

if (TEST_CXX_COMPILER)
    list(APPEND consumer_args "-DCMAKE_CXX_COMPILER=${TEST_CXX_COMPILER}")
endif ()

set(scenario "setup")

function(fail text)
    message(FATAL_ERROR "epm-graph-nested: ${scenario}: ${text}")
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

# Commits everything in ${repos}/<name> and tags it v1.
function(make_repository name)
    set(directory "${repos}/${name}")
    git("${directory}" init -q)
    git("${directory}" add -A)
    git("${directory}" commit -q -m "${name}")
    git("${directory}" tag v1)
endfunction()

function(file_url path out_var)
    if (NOT path MATCHES "^/")
        set(path "/${path}")
    endif ()

    set(${out_var} "file://${path}" PARENT_SCOPE)
endfunction()

file(COPY "${fixtures}/Leaf/" DESTINATION "${repos}/Leaf")
make_repository(Leaf)
file_url("${repos}/Leaf" LEAF_URL)

file(COPY "${fixtures}/Mid/" DESTINATION "${repos}/Mid" PATTERN "*.in" EXCLUDE)
file(COPY "${repo_dir}/CMake/get_epm.cmake" DESTINATION "${repos}/Mid/cmake")
configure_file("${fixtures}/Mid/epm.cpp.in" "${repos}/Mid/epm.cpp" @ONLY)
make_repository(Mid)
file_url("${repos}/Mid" MID_URL)

file(COPY "${fixtures}/Leaf/" DESTINATION "${leaf_checkout}")
file(WRITE "${leaf_checkout}/Leaf.cpp"
        "#include <Leaf.h>\n\nint leafValue()\n{\n    return 2;\n}\n")

set(GET_EPM "${repo_dir}/CMake/get_epm.cmake")
file(COPY "${fixtures}/Consumer/Main.cpp" DESTINATION "${consumer}")
configure_file("${fixtures}/Consumer/CMakeLists.txt.in"
        "${consumer}/CMakeLists.txt" @ONLY)
configure_file("${fixtures}/Consumer/epm.cpp.in" "${consumer}/epm.cpp" @ONLY)

# Configures the consumer into ${WORK_DIR}/<name>; ARGN goes to cmake. Sets
# `build` and `output`.
macro(configure_consumer name)
    set(scenario "${name}")
    set(build "${WORK_DIR}/${name}")
    execute_process(
            COMMAND "${CMAKE_COMMAND}" -S "${consumer}" -B "${build}"
            ${consumer_args} ${ARGN}
            RESULT_VARIABLE result
            OUTPUT_VARIABLE output
            ERROR_VARIABLE output)

    if (NOT result EQUAL 0)
        fail("configure failed (${result}):\n${output}")
    endif ()
endmacro()

# Both packages were loaded in `mode`, Leaf first.
function(expect_loaded mode)
    string(FIND "${output}" "-- epm: Leaf (${mode}, " leaf_at)
    string(FIND "${output}" "-- epm: Mid (${mode}, " mid_at)

    if (leaf_at EQUAL -1 OR mid_at EQUAL -1 OR NOT leaf_at LESS mid_at)
        fail("expected Leaf, then Mid, loaded ${mode}:\n${output}")
    endif ()
endfunction()

# The value at the JSON path ARGN of package `name` in the scenario's
# graph.json.
function(graph_value name out_var)
    file(READ "${build}/epm/graph.json" graph)
    string(JSON count LENGTH "${graph}" packages)
    math(EXPR last "${count} - 1")

    foreach (index RANGE ${last})
        string(JSON package_name GET "${graph}" packages ${index} name)

        if (package_name STREQUAL name)
            string(JSON value GET "${graph}" packages ${index} ${ARGN})
            set(${out_var} "${value}" PARENT_SCOPE)
            return()
        endif ()
    endforeach ()

    fail("${name} is not in graph.json")
endfunction()

function(expect_equal what actual expected)
    if (NOT actual STREQUAL expected)
        fail("${what} is '${actual}', expected '${expected}'")
    endif ()
endfunction()

# Builds the consumer and runs app, which prints Mid's and Leaf's values.
function(build_and_run expected)
    execute_process(
            COMMAND "${CMAKE_COMMAND}" --build "${build}" --config Release
            RESULT_VARIABLE result
            OUTPUT_VARIABLE build_output
            ERROR_VARIABLE build_output)

    if (NOT result EQUAL 0)
        fail("build failed (${result}):\n${build_output}")
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
    expect_equal("app's output (exit ${result})" "${run_output}"
            "nested ok ${expected}")

    expect_recipe_targets()
endfunction()

# The consumer's epm_add_recipe built its epm.cpp as a module, and Mid's, from
# a child build or a subdirectory, built nothing.
function(expect_recipe_targets)
    file(GLOB_RECURSE consumer_modules "${build}/*ConsumerRecipe.epm-recipe.*")
    file(GLOB_RECURSE mid_modules "${build}/*MidRecipe*" "${cache}/bin/*MidRecipe*")

    if (NOT consumer_modules)
        fail("epm_add_recipe(ConsumerRecipe) built no module in ${build}")
    endif ()

    if (mid_modules)
        fail("Mid's epm_add_recipe built something: ${mid_modules}")
    endif ()
endfunction()

# ConsumerRecipe's test, and the ConsumerRecipe-describe target, run
# `epm describe --module` over the module the build made.
function(expect_recipe_test)
    execute_process(
            COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${build}" -C Release
            -R "^ConsumerRecipe$" --output-on-failure
            RESULT_VARIABLE result
            OUTPUT_VARIABLE test_output
            ERROR_VARIABLE test_output)

    if (NOT result EQUAL 0 OR NOT test_output MATCHES "100% tests passed(, 0 tests failed)? out of 1")
        fail("ctest of ConsumerRecipe failed (${result}):\n${test_output}")
    endif ()

    execute_process(
            COMMAND "${CMAKE_COMMAND}" --build "${build}" --config Release
            --target ConsumerRecipe-describe
            RESULT_VARIABLE result
            OUTPUT_VARIABLE describe_output
            ERROR_VARIABLE describe_output)

    if (NOT result EQUAL 0 OR NOT describe_output MATCHES "\"name\": \"Mid\"")
        fail("ConsumerRecipe-describe failed (${result}):\n${describe_output}")
    endif ()
endfunction()

# Every config marker in the cache, with its contents: unchanged when nothing
# was built.
function(cache_markers out_var)
    file(GLOB markers "${cache}/bin/*/*/*/*.json")
    list(SORT markers)
    set(text "")

    foreach (marker IN LISTS markers)
        file(READ "${marker}" contents)
        string(APPEND text "${marker}\n${contents}\n")
    endforeach ()

    set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

# 1. Default mode: both prebuilt. Mid's child build found Leaf only through
# the provider the parent injected (Leaf has no package config).
configure_consumer(default)
expect_loaded(prebuilt)

foreach (name IN ITEMS Leaf Mid)
    graph_value(${name} mode mode)
    expect_equal("${name}'s mode" "${mode}" prebuilt)
    graph_value(${name} prefix prefix)

    foreach (file IN ITEMS manifest.json Release.json epm-targets.cmake)
        if (NOT EXISTS "${prefix}/${file}")
            fail("${name}'s entry ${prefix} has no ${file}")
        endif ()
    endforeach ()

    set(${name}_prefix "${prefix}")
endforeach ()

graph_value(Leaf leaf_fingerprint fingerprint)
graph_value(Mid graph_deps request deps Leaf)
expect_equal("Mid's request deps.Leaf in graph.json" "${graph_deps}"
        "${leaf_fingerprint}")

file(READ "${Mid_prefix}/manifest.json" mid_manifest)
string(JSON entry_deps GET "${mid_manifest}" fingerprint deps Leaf)
expect_equal("Mid's entry fingerprint deps.Leaf" "${entry_deps}"
        "${leaf_fingerprint}")

# Mid's entry exports Mid alone, links Leaf by its facade name and defines no
# Leaf target of its own.
file(READ "${Mid_prefix}/export-Release.cmake.names" names)
expect_equal("Mid's exported names" "${names}" "Mid\n")
file(READ "${Mid_prefix}/export-Release.cmake.aliases" aliases)
expect_equal("Mid's exported aliases" "${aliases}" "Mid::Mid Mid\n")

foreach (file IN ITEMS epm-targets.cmake export-Release.cmake)
    file(READ "${Mid_prefix}/${file}" text)

    if (text MATCHES "add_library\\(\"?(epm_release::)?Leaf[\" ]")
        fail("Mid's ${file} defines a Leaf target:\n${text}")
    endif ()
endforeach ()

file(READ "${Mid_prefix}/export-Release.cmake" mid_export)

if (NOT mid_export MATCHES "INTERFACE_LINK_LIBRARIES \"[^\"]*Leaf")
    fail("Mid's export-Release.cmake does not link Leaf:\n${mid_export}")
endif ()

build_and_run("11 1")
expect_recipe_test()
cache_markers(markers_before)

# 2. A fresh build directory: both entries are reused, nothing is built.
configure_consumer(cached)
expect_loaded(prebuilt)
cache_markers(markers_after)

if (NOT markers_before STREQUAL markers_after)
    fail("the cache changed:\n${markers_before}\nbecame\n${markers_after}")
endif ()

foreach (name IN ITEMS Leaf Mid)
    graph_value(${name} prefix prefix)
    expect_equal("${name}'s prefix" "${prefix}" "${${name}_prefix}")
endforeach ()

build_and_run("11 1")

# 3. Every package from source.
configure_consumer(source -DEPM_MODE=source)
expect_loaded(source)

foreach (name IN ITEMS Leaf Mid)
    if (NOT IS_DIRECTORY "${build}/_deps/${name}-build")
        fail("${name} was not added as a subdirectory")
    endif ()
endforeach ()

build_and_run("11 1")

# 4. Leaf from source forces Mid, which links it, into source too.
configure_consumer(leaf-source -DEPM_Leaf_MODE=source)
expect_loaded(source)
graph_value(Mid why provenance mode)
expect_equal("Mid's mode provenance" "${why}" "source dependency Leaf")
build_and_run("11 1")

# 5. A local Leaf checkout. Leaf's repository is moved away meanwhile, so any
# attempt to fetch it would fail the configure.
file(RENAME "${repos}/Leaf" "${repos}/Leaf-hidden")
configure_consumer(leaf-checkout "-DEPM_Leaf_SOURCE=${leaf_checkout}")
file(RENAME "${repos}/Leaf-hidden" "${repos}/Leaf")
expect_loaded(source)
graph_value(Leaf source_dir sourceDir)
expect_equal("Leaf's sourceDir" "${source_dir}" "${leaf_checkout}")
graph_value(Leaf commit commit)
expect_equal("Leaf's commit" "${commit}" "")
graph_value(Mid why provenance mode)
expect_equal("Mid's mode provenance" "${why}" "source dependency Leaf")
build_and_run("12 2")

message(STATUS "All nested graph checks passed")
