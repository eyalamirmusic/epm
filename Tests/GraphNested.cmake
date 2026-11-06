# End-to-end self-test of nested child builds (Docs/Graph.md §10) with the real
# epm binary. Commits Tests/Nested/Leaf and Tests/Nested/Mid (whose epm.cpp
# declares Leaf) into local git repositories, then configures, builds and runs
# Tests/Nested/Consumer (whose epm.cpp declares only Mid) once per scenario.
# The binary compiles both recipes with the compiler that built it:
#
#   default       both prebuilt; Mid's child build loads Leaf through the
#                 injected provider; Mid's entry records Leaf's fingerprint in
#                 deps and does not re-export Leaf, and carries a copy of the
#                 ${Leaf_SOURCE_DIR}/include it names, which in its child
#                 build is Leaf's snapshot in the cache; Leaf's build step, which
#                 Mid declares with add<LeafSteps>, runs in Mid's recipe
#                 executable, and again by hand from <build>/epm/steps/
#   cached        a fresh build directory reuses both entries
#   no-sources    EPM_FETCH_SOURCES=OFF: no SOURCE_DIR for a prebuilt Leaf,
#                 one for Mid through EPM_Mid_FETCH_SOURCE=ON
#   mid-source-leaf-prebuilt
#                 EPM_Mid_MODE=source over the prebuilt Leaf, with
#                 EPM_FETCH_SOURCES=OFF: Leaf's snapshot is fetched anyway,
#                 since Mid reads ${Leaf_SOURCE_DIR}/include
#   source        EPM_MODE=source: both added as subdirectories
#   leaf-source   EPM_Leaf_MODE=source forces Mid into source (§5 rule 6)
#   leaf-checkout EPM_Leaf_SOURCE=<checkout>: Leaf's repository is hidden, so
#                 the checkout is used without a fetch
#   lib-source    a consumer of mid-source, a Mid whose epm.cpp declares Leaf
#                 mode("source"): both from source
#   lib-source-root-prebuilt
#                 its root also declares Leaf prebuilt (Release, or any config
#                 on a multi-config generator): the root's mode wins, Mid's is
#                 a note (an error with EPM_STRICT), Leaf's entry is reused and
#                 Mid is prebuilt again
#   lib-source-root-debug
#                 the same root in a Debug build (single-config only): both
#                 from source again
#
# Each scenario also builds the consumer's epm_add_recipe executable and checks
# that Mid's epm_add_recipe built nothing; the default one runs its -configure
# target, a whole configure in that executable, and its describe command
# standalone.
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

# A <name>.command line is quoted for the host's shell.
if (CMAKE_HOST_WIN32)
    set(command_syntax WINDOWS_COMMAND)
else ()
    set(command_syntax UNIX_COMMAND)
endif ()

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

# Commits everything in ${repos}/<name> and tags it v1. It serves partial
# fetches and commits by hash, as GitHub does, so the walk's fetch of an
# epm.cpp alone (--filter=blob:none, then that one blob) runs as it would there.
function(make_repository name)
    set(directory "${repos}/${name}")
    git("${directory}" init -q)
    git("${directory}" config uploadpack.allowFilter true)
    git("${directory}" config uploadpack.allowAnySHA1InWant true)
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

# A repository ${repos}/<repository> of the package Mid, whose epm.cpp declares
# Leaf in LEAF_MODE (empty: unset); sets <out_var> to its url.
function(make_mid repository LEAF_MODE out_var)
    set(directory "${repos}/${repository}")
    file(COPY "${fixtures}/Mid/" DESTINATION "${directory}" PATTERN "*.in" EXCLUDE)
    file(COPY "${repo_dir}/CMake/get_epm.cmake" DESTINATION "${directory}/cmake")
    configure_file("${fixtures}/Mid/epm.cpp.in" "${directory}/epm.cpp" @ONLY)
    make_repository(${repository})
    file_url("${directory}" url)
    set(${out_var} "${url}" PARENT_SCOPE)
endfunction()

make_mid(Mid "" MID_URL)
make_mid(mid-source source MID_SOURCE_URL)

file(COPY "${fixtures}/Leaf/" DESTINATION "${leaf_checkout}")
file(WRITE "${leaf_checkout}/Leaf.cpp"
        "#include <Leaf.h>\n\nint leafValue()\n{\n    return 2;\n}\n")

# The consumer project in <directory>, declaring Mid from MID_URL; with
# ROOT_CACHES_LEAF ON its root also declares Leaf prebuilt (Release, or a
# multi-config generator).
function(make_consumer directory MID_URL ROOT_CACHES_LEAF)
    set(GET_EPM "${repo_dir}/CMake/get_epm.cmake")
    file(COPY "${fixtures}/Consumer/Main.cpp" DESTINATION "${directory}")
    configure_file("${fixtures}/Consumer/CMakeLists.txt.in"
            "${directory}/CMakeLists.txt" @ONLY)
    configure_file("${fixtures}/Consumer/epm.cpp.in" "${directory}/epm.cpp" @ONLY)
endfunction()

make_consumer("${consumer}" "${MID_URL}" OFF)
make_consumer("${WORK_DIR}/Lib" "${MID_SOURCE_URL}" OFF)
make_consumer("${WORK_DIR}/App" "${MID_SOURCE_URL}" ON)

if (TEST_GENERATOR MATCHES "^(Xcode|Ninja Multi-Config|Visual Studio)")
    set(multi_config ON)
else ()
    set(multi_config OFF)
endif ()

# Configures the consumer project in ${consumer} into ${WORK_DIR}/<name>; ARGN
# goes to cmake. Sets `build`, `output` and `result`.
macro(try_configure_consumer name)
    set(scenario "${name}")
    set(build "${WORK_DIR}/${name}")
    execute_process(
            COMMAND "${CMAKE_COMMAND}" -S "${consumer}" -B "${build}"
            ${consumer_args} ${ARGN}
            RESULT_VARIABLE result
            OUTPUT_VARIABLE output
            ERROR_VARIABLE output)
endmacro()

# try_configure_consumer, which must succeed.
macro(configure_consumer name)
    try_configure_consumer(${name} ${ARGN})

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

# Builds the consumer in config ARGV1 (default Release) and runs app, which
# prints Mid's and Leaf's values.
function(build_and_run expected)
    set(config Release)

    if (ARGC GREATER 1)
        set(config "${ARGV1}")
    endif ()

    execute_process(
            COMMAND "${CMAKE_COMMAND}" --build "${build}" --config ${config}
            RESULT_VARIABLE result
            OUTPUT_VARIABLE build_output
            ERROR_VARIABLE build_output)

    if (NOT result EQUAL 0)
        fail("${config} build failed (${result}):\n${build_output}")
    endif ()

    set(app "")

    foreach (candidate IN ITEMS "${build}/app" "${build}/app.exe"
            "${build}/${config}/app" "${build}/${config}/app.exe")
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

# The executable epm_add_recipe(ConsumerRecipe) built, empty for none.
function(consumer_recipe out_var)
    set(found "")

    foreach (candidate IN ITEMS "${build}/ConsumerRecipe" "${build}/ConsumerRecipe.exe"
            "${build}/Release/ConsumerRecipe" "${build}/Release/ConsumerRecipe.exe"
            "${build}/Debug/ConsumerRecipe" "${build}/Debug/ConsumerRecipe.exe")
        if (NOT found AND EXISTS "${candidate}" AND NOT IS_DIRECTORY "${candidate}")
            set(found "${candidate}")
        endif ()
    endforeach ()

    set(${out_var} "${found}" PARENT_SCOPE)
endfunction()

# The consumer's epm_add_recipe built its epm.cpp as an executable, and Mid's,
# from a child build or a subdirectory, built nothing.
function(expect_recipe_targets)
    consumer_recipe(recipe)
    file(GLOB_RECURSE mid_recipes "${build}/*MidRecipe*" "${cache}/bin/*MidRecipe*")

    if (NOT recipe)
        fail("epm_add_recipe(ConsumerRecipe) built no executable in ${build}")
    endif ()

    if (mid_recipes)
        fail("Mid's epm_add_recipe built something: ${mid_recipes}")
    endif ()
endfunction()

# The Project JSON a recipe prints names Mid.
set(names_mid "\"name\": *\"Mid\"")

# The ConsumerRecipe-configure target runs the whole configure in the
# executable the build made, with this build's env.json and overrides.json,
# into the build's own <build>/epm, finding both entries cached. Its describe
# command prints the project, and `epm describe --exe` runs it in place of one
# epm builds.
function(expect_recipe_configure)
    file(SHA256 "${build}/epm/graph.cmake" graph_before)

    # The target refreshes the build's own graph files, everything cached.
    file(REMOVE "${build}/epm/graph.cmake")
    execute_process(
            COMMAND "${CMAKE_COMMAND}" --build "${build}" --config Release
            --target ConsumerRecipe-configure
            RESULT_VARIABLE result
            OUTPUT_VARIABLE configure_output
            ERROR_VARIABLE configure_output)

    if (NOT result EQUAL 0 OR NOT EXISTS "${build}/epm/graph.cmake")
        fail("ConsumerRecipe-configure failed (${result}):\n${configure_output}")
    endif ()

    file(SHA256 "${build}/epm/graph.cmake" graph_after)
    expect_equal("graph.cmake from ConsumerRecipe-configure" "${graph_after}"
            "${graph_before}")

    if (configure_output MATCHES "building (Leaf|Mid) from source")
        fail("ConsumerRecipe-configure built again:\n${configure_output}")
    endif ()

    # The steps it ran, Mid's own and Leaf's in Mid's executable, are still
    # epm's cached recipe executables.
    foreach (name IN ITEMS Leaf Mid)
        file(STRINGS "${build}/epm/steps/${name}.command" command_lines)
        list(GET command_lines 1 command_line)
        separate_arguments(command_line ${command_syntax} "${command_line}")
        list(GET command_line 0 step_recipe)

        if (NOT IS_ABSOLUTE "${step_recipe}"
                OR NOT step_recipe MATCHES "/epm-recipe(\\.exe)?$")
            fail("${name}.command after ConsumerRecipe-configure: ${command_line}")
        endif ()
    endforeach ()

    consumer_recipe(recipe)
    set(env "${build}/epm/env.json")
    execute_process(COMMAND "${recipe}" describe --env "${env}"
            RESULT_VARIABLE result
            OUTPUT_VARIABLE run_output
            ERROR_VARIABLE run_error)

    if (NOT result EQUAL 0 OR NOT run_output MATCHES "${names_mid}")
        fail("${recipe} describe --env ${env} failed (${result}):\n${run_output}${run_error}")
    endif ()

    set(project_file "${build}/recipe-project.json")
    file(REMOVE "${project_file}")
    execute_process(
            COMMAND "${recipe}" describe --env "${env}" --out "${project_file}"
            RESULT_VARIABLE result
            OUTPUT_VARIABLE run_output
            ERROR_VARIABLE run_output)

    if (EXISTS "${project_file}")
        file(READ "${project_file}" project_json)
    else ()
        set(project_json "")
    endif ()

    if (NOT result EQUAL 0 OR NOT project_json MATCHES "${names_mid}")
        fail("${recipe} --out ${project_file} failed (${result}):\n${run_output}\n${project_file}: ${project_json}")
    endif ()

    execute_process(
            COMMAND "${recipe}" describe --env "${WORK_DIR}/no-such-env.json"
            RESULT_VARIABLE result
            OUTPUT_VARIABLE run_output
            ERROR_VARIABLE run_error)

    if (NOT result EQUAL 1 OR run_error STREQUAL "")
        fail("${recipe} describe with a missing env.json exited ${result} (expected 1, with a message), stderr '${run_error}'")
    endif ()

    # So does its configure, the default command.
    execute_process(
            COMMAND "${recipe}" --env "${WORK_DIR}/no-such-env.json"
            --overrides "${build}/epm/overrides.json" --out "${build}/recipe-out"
            WORKING_DIRECTORY "${consumer}"
            RESULT_VARIABLE result
            OUTPUT_VARIABLE run_output
            ERROR_VARIABLE run_error)

    if (NOT result EQUAL 1 OR NOT run_error MATCHES "cannot read the env file")
        fail("${recipe} configuring with a missing env.json exited ${result} (expected 1, with a message), stderr '${run_error}'")
    endif ()

    execute_process(
            COMMAND "${EPM_BINARY}" describe "${consumer}/epm.cpp" --exe "${recipe}"
            --env "${env}"
            RESULT_VARIABLE result
            OUTPUT_VARIABLE run_output
            ERROR_VARIABLE run_error)

    if (NOT result EQUAL 0 OR NOT run_output MATCHES "${names_mid}")
        fail("epm describe --exe ${recipe} failed (${result}):\n${run_output}${run_error}")
    endif ()
endfunction()

# Every config marker in the cache, with its contents: unchanged when nothing
# was built.
function(cache_markers out_var)
    file(GLOB markers "${cache}/bin/*/*/*/manifest.json"
            "${cache}/bin/*/*/*/configs/*/epm-config.json")
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

    foreach (file IN ITEMS manifest.json configs/Release/epm-config.json
            epm-targets.cmake)
        if (NOT EXISTS "${prefix}/${file}")
            fail("${name}'s entry ${prefix} has no ${file}")
        endif ()
    endforeach ()

    set(${name}_prefix "${prefix}")

    # Prebuilt, each still has its snapshot as SOURCE_DIR (EPM_FETCH_SOURCES
    # is on by default), for a consumer reading CMake modules from there.
    graph_value(${name} source_dir sourceDir)
    string(FIND "${source_dir}" "${cache}/src/${name}/" snapshot_at)

    if (NOT IS_DIRECTORY "${source_dir}" OR NOT snapshot_at EQUAL 0)
        fail("${name}'s sourceDir '${source_dir}' is not its snapshot in the cache")
    endif ()
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
set(mid_release "${Mid_prefix}/configs/Release")
file(READ "${mid_release}/export-Release.cmake.names" names)
expect_equal("Mid's exported names" "${names}" "Mid\n")
file(READ "${mid_release}/export-Release.cmake.aliases" aliases)
expect_equal("Mid's exported aliases" "${aliases}" "Mid::Mid Mid\n")

foreach (file IN ITEMS "${Mid_prefix}/epm-targets.cmake"
        "${mid_release}/export-Release.cmake")
    file(READ "${file}" text)

    if (text MATCHES "add_library\\(\"?(epm_release::)?Leaf[\" ]")
        fail("Mid's ${file} defines a Leaf target:\n${text}")
    endif ()
endforeach ()

# Mid records Leaf's entry per policy too: compatible compares that key, so a
# Mid built elsewhere against a compatible Leaf entry still matches.
graph_value(Mid graph_compatible request compatibleDeps Leaf)
string(JSON entry_compatible GET "${mid_manifest}" fingerprint compatibleDeps Leaf)
expect_equal("Mid's entry fingerprint compatibleDeps.Leaf" "${entry_compatible}"
        "${graph_compatible}")

file(READ "${mid_release}/export-Release.cmake" mid_export)

if (NOT mid_export MATCHES "INTERFACE_LINK_LIBRARIES \"[^\"]*Leaf")
    fail("Mid's export-Release.cmake does not link Leaf:\n${mid_export}")
endif ()

# Mid names ${Leaf_SOURCE_DIR}/include, Leaf's snapshot in the cache: the
# harvest copied it into Mid's entry and names the copy.
string(FIND "${mid_export}" "/include/dep/Leaf/include" leaf_include_at)

if (leaf_include_at EQUAL -1
        OR NOT EXISTS "${mid_release}/include/dep/Leaf/include/Leaf.h")
    fail("Mid's entry lacks its copy of \${Leaf_SOURCE_DIR}/include:\n${mid_export}")
endif ()

# Leaf's build step ran in Mid's recipe executable: the option its add<LeafSteps>
# configure hook sets is in Leaf's fingerprint. The steps report progress
# while the configure runs.
file(READ "${Leaf_prefix}/manifest.json" leaf_manifest)
string(JSON leaf_extra ERROR_VARIABLE leaf_extra_error
        GET "${leaf_manifest}" fingerprint options LEAF_EXTRA)
expect_equal("Leaf's fingerprint option LEAF_EXTRA" "${leaf_extra}" "ON")

foreach (name IN ITEMS Leaf Mid)
    if (NOT output MATCHES "-- epm: building ${name} from source into the cache")
        fail("no progress line for ${name}'s build in the configure output:\n${output}")
    endif ()
endforeach ()

# Each step's input, output and command stay in <build>/epm/steps/.
set(steps "${build}/epm/steps")

foreach (name IN ITEMS Leaf Mid)
    foreach (file IN ITEMS "${name}.in.json" "${name}.out.json" "${name}.command")
        if (NOT EXISTS "${steps}/${file}")
            fail("no ${steps}/${file}")
        endif ()
    endforeach ()
endforeach ()

file(READ "${steps}/Leaf.in.json" leaf_input)
string(JSON leaf_owner GET "${leaf_input}" recipeName)
expect_equal("Leaf's step recipeName" "${leaf_owner}" "Mid")

# Leaf.command: "# run from: <directory>", then the command, shell-quoted.
file(STRINGS "${steps}/Leaf.command" command_lines)
list(LENGTH command_lines command_line_count)
expect_equal("Leaf.command's line count" "${command_line_count}" "2")
list(GET command_lines 0 run_from)
list(GET command_lines 1 leaf_command)

if (NOT run_from MATCHES "^# run from: (.+)$")
    fail("Leaf.command's first line is not '# run from: <directory>': ${run_from}")
endif ()

set(run_from "${CMAKE_MATCH_1}")

if (NOT IS_DIRECTORY "${run_from}")
    fail("Leaf.command runs from ${run_from}, which is not a directory")
endif ()

separate_arguments(leaf_command ${command_syntax} "${leaf_command}")
list(GET leaf_command 0 mid_recipe)

if (NOT IS_ABSOLUTE "${mid_recipe}" OR NOT mid_recipe MATCHES "/epm-recipe(\\.exe)?$"
        OR NOT EXISTS "${mid_recipe}")
    fail("Leaf.command does not run a recipe executable: ${leaf_command}")
endif ()

# Leaf's step again, by hand, as Leaf.command has it: the entry is found, not
# rebuilt.
set(by_hand "${steps}/Leaf.out.json")
file(REMOVE "${by_hand}")
execute_process(
        COMMAND ${leaf_command}
        WORKING_DIRECTORY "${run_from}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE step_output
        ERROR_VARIABLE step_output)

if (NOT result EQUAL 0 OR NOT EXISTS "${by_hand}")
    fail("Leaf.command (${leaf_command}) failed (${result}):\n${step_output}")
endif ()

file(READ "${by_hand}" leaf_output)
string(JSON by_hand_prefix GET "${leaf_output}" prefix)
expect_equal("Leaf's prefix from the step run by hand" "${by_hand_prefix}"
        "${Leaf_prefix}")

if (step_output MATCHES "building Leaf")
    fail("Leaf's step run by hand built it again:\n${step_output}")
endif ()

build_and_run("11 1")
expect_recipe_configure()
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

# 2b. EPM_FETCH_SOURCES=OFF: prebuilt packages get no SOURCE_DIR, unless
# EPM_<Name>_FETCH_SOURCE asks for one; the entries are the same.
configure_consumer(no-sources -DEPM_FETCH_SOURCES=OFF -DEPM_Mid_FETCH_SOURCE=ON)
expect_loaded(prebuilt)
graph_value(Leaf source_dir sourceDir)
expect_equal("Leaf's sourceDir with EPM_FETCH_SOURCES=OFF" "${source_dir}" "")
graph_value(Mid source_dir sourceDir)

if (NOT IS_DIRECTORY "${source_dir}")
    fail("Mid's sourceDir with EPM_Mid_FETCH_SOURCE=ON is '${source_dir}'")
endif ()

foreach (name IN ITEMS Leaf Mid)
    graph_value(${name} prefix prefix)
    expect_equal("${name}'s prefix" "${prefix}" "${${name}_prefix}")
endforeach ()

build_and_run("11 1")

# 2c. Mid from source over a prebuilt Leaf, with EPM_FETCH_SOURCES=OFF: Mid's
# CMakeLists.txt reads ${Leaf_SOURCE_DIR}/include, so Leaf's snapshot is
# fetched for it whatever the consumer asked.
configure_consumer(mid-source-leaf-prebuilt -DEPM_Mid_MODE=source
        -DEPM_FETCH_SOURCES=OFF)
string(FIND "${output}" "-- epm: Leaf (prebuilt, " leaf_at)
string(FIND "${output}" "-- epm: Mid (source, " mid_at)

if (leaf_at EQUAL -1 OR mid_at EQUAL -1)
    fail("expected Leaf prebuilt and Mid from source:\n${output}")
endif ()

graph_value(Leaf source_dir sourceDir)

if (NOT EXISTS "${source_dir}/include/Leaf.h")
    fail("Leaf's sourceDir '${source_dir}' holds no include/Leaf.h")
endif ()

file(READ "${build}/epm/graph.cmake" graph_cmake)
string(FIND "${graph_cmake}" "set(epm_Leaf_SOURCE_DIR \"${source_dir}\")" set_at)

if (set_at EQUAL -1)
    fail("graph.cmake does not give Leaf's SOURCE_DIR:\n${graph_cmake}")
endif ()

if (NOT IS_DIRECTORY "${build}/_deps/Mid-build"
        OR EXISTS "${build}/_deps/Leaf-build")
    fail("expected Mid alone added as a subdirectory")
endif ()

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

# Leaf's step ran in Mid's recipe executable in source mode too: the option
# its configure hook sets reaches Leaf's add_subdirectory through graph.cmake.
graph_value(Leaf leaf_extra options LEAF_EXTRA)
expect_equal("Leaf's graph.json option LEAF_EXTRA" "${leaf_extra}" "ON")
file(READ "${build}/epm/graph.cmake" graph_cmake)

if (NOT graph_cmake MATCHES "set\\(epm_Leaf_OPTIONS \"[^\"]*LEAF_EXTRA=ON")
    fail("Leaf's OPTIONS in graph.cmake lack LEAF_EXTRA=ON:\n${graph_cmake}")
endif ()

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

# 6. A library pins its dependency to source: mid-source's epm.cpp declares
# Leaf with mode("source"), which forces Mid into source too (§5 rule 6).
# Leaf's steps still run in Mid's recipe executable.
set(consumer "${WORK_DIR}/Lib")
configure_consumer(lib-source)
expect_loaded(source)
graph_value(Leaf why provenance mode)
expect_equal("Leaf's mode provenance" "${why}" "manifest:Mid")
graph_value(Mid why provenance mode)
expect_equal("Mid's mode provenance" "${why}" "source dependency Leaf")
graph_value(Leaf leaf_extra options LEAF_EXTRA)
expect_equal("Leaf's graph.json option LEAF_EXTRA" "${leaf_extra}" "ON")

foreach (name IN ITEMS Leaf Mid)
    if (NOT IS_DIRECTORY "${build}/_deps/${name}-build")
        fail("${name} was not added as a subdirectory")
    endif ()
endforeach ()

build_and_run("11 1")

# 7. The app re-enables the binary cache for that Leaf: its root declares Leaf
# prebuilt for Release (and every config on a multi-config generator). The
# root's mode wins over Mid's, the losing declaration is a note, and Leaf's
# git, tag and steps still come from Mid, so the default scenario's entry is
# reused and Mid is no longer forced into source.
set(consumer "${WORK_DIR}/App")
set(mode_note "Leaf: Mid declares mode source, root declares mode prebuilt")
configure_consumer(lib-source-root-prebuilt)
expect_loaded(prebuilt)
graph_value(Leaf why provenance mode)
expect_equal("Leaf's mode provenance" "${why}" "root")

foreach (field IN ITEMS git tag)
    graph_value(Leaf why provenance ${field})
    expect_equal("Leaf's ${field} provenance" "${why}" "manifest:Mid")
endforeach ()

graph_value(Leaf git_url request gitUrl)
expect_equal("Leaf's request gitUrl" "${git_url}" "${LEAF_URL}")
graph_value(Leaf git_ref request gitRef)
expect_equal("Leaf's request gitRef" "${git_ref}" "v1")
graph_value(Leaf prefix prefix)
expect_equal("Leaf's prefix" "${prefix}" "${Leaf_prefix}")
file(READ "${prefix}/manifest.json" leaf_manifest)
string(JSON leaf_extra GET "${leaf_manifest}" fingerprint options LEAF_EXTRA)
expect_equal("Leaf's fingerprint option LEAF_EXTRA" "${leaf_extra}" "ON")
file(READ "${build}/epm/steps/Leaf.in.json" leaf_input)
string(JSON leaf_owner GET "${leaf_input}" recipeName)
expect_equal("Leaf's step recipeName" "${leaf_owner}" "Mid")

if (multi_config)
    foreach (marker IN ITEMS configs/Debug/epm-config.json
            configs/Release/epm-config.json)
        if (NOT EXISTS "${prefix}/${marker}")
            fail("Leaf's entry ${prefix} has no ${marker}")
        endif ()
    endforeach ()
endif ()

graph_value(Mid mode mode)
expect_equal("Mid's mode" "${mode}" prebuilt)
graph_value(Mid why provenance mode)
expect_equal("Mid's mode provenance" "${why}" "default")

file(READ "${build}/epm/graph.json" graph)
string(JSON notes GET "${graph}" notes)
string(JSON note_count LENGTH "${graph}" notes)
expect_equal("graph.json's note count (${notes})" "${note_count}" "1")
string(JSON note GET "${graph}" notes 0)
expect_equal("graph.json's note" "${note}" "${mode_note}")
include("${build}/epm/graph.cmake")
expect_equal("graph.cmake's epm_NOTES" "${epm_NOTES}" "${mode_note}")

foreach (name IN ITEMS Leaf Mid)
    if (EXISTS "${build}/_deps/${name}-build")
        fail("${name} was added as a subdirectory")
    endif ()
endforeach ()

build_and_run("11 1")

if (multi_config)
    build_and_run("11 1" Debug)
endif ()

# EPM_STRICT turns that note into an error, which CMake prints wrapped.
try_configure_consumer(lib-source-root-prebuilt-strict -DEPM_STRICT=ON)
string(REGEX REPLACE "[ \t\r\n]+" " " unwrapped "${output}")
string(FIND "${unwrapped}" "EPM_STRICT is on and the graph has notes" strict_at)
string(FIND "${unwrapped}" "${mode_note}" note_at)

if (result EQUAL 0 OR strict_at EQUAL -1 OR note_at EQUAL -1)
    fail("expected the configure to fail on the note (${result}):\n${output}")
endif ()

# 8. A Debug build of the app leaves Mid's source pin alone. Mode cannot
# differ per config, so a multi-config generator has no Debug-only build.
if (multi_config)
    message(STATUS "epm-graph-nested: lib-source-root-debug: not run under "
            "${TEST_GENERATOR}, a multi-config generator, where the root "
            "caches Leaf for every config")
else ()
    configure_consumer(lib-source-root-debug -DCMAKE_BUILD_TYPE=Debug)
    expect_loaded(source)
    graph_value(Leaf why provenance mode)
    expect_equal("Leaf's mode provenance" "${why}" "manifest:Mid")
    graph_value(Mid why provenance mode)
    expect_equal("Mid's mode provenance" "${why}" "source dependency Leaf")
    build_and_run("11 1" Debug)
endif ()

message(STATUS "All nested graph checks passed")
