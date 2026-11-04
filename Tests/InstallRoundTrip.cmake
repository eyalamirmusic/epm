# Installs the build tree into a fresh prefix and runs the installed epm
# through a symlink, as one on PATH is: `recipe-sdk` must name the prefix's
# headers and archives and nothing of the build or source tree, and `describe`
# must compile and run Tests/Recipe/epm.cpp against them. Then checks which
# bin/epm a later install of another configuration leaves (share/epm/epm-config).
#
#   cmake -DBUILD_DIR=<build> -DCONFIG=<config> [-DWORK_DIR=<dir>]
#         -P Tests/InstallRoundTrip.cmake

cmake_minimum_required(VERSION 3.25)

get_filename_component(repo_dir "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

if (CMAKE_HOST_WIN32)
    message(STATUS "epm-install-round-trip: skipped: no epm.cpp recipes on Windows yet")
    return()
endif ()

foreach (var IN ITEMS BUILD_DIR CONFIG)
    if (NOT ${var})
        message(FATAL_ERROR "epm-install-round-trip: -D${var}=... is required")
    endif ()
endforeach ()

if (NOT WORK_DIR)
    set(WORK_DIR "${CMAKE_CURRENT_BINARY_DIR}/epm-install-round-trip")
endif ()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}/prefix" "${WORK_DIR}/path")
file(REAL_PATH "${WORK_DIR}/prefix" prefix)
file(REAL_PATH "${BUILD_DIR}" build_dir)

function(run_checked)
    execute_process(COMMAND ${ARGN}
            RESULT_VARIABLE result
            OUTPUT_VARIABLE output
            ERROR_VARIABLE output)

    if (NOT result EQUAL 0)
        string(REPLACE ";" " " command "${ARGN}")
        message(FATAL_ERROR "epm-install-round-trip: ${command} failed "
                "(${result}):\n${output}")
    endif ()

    set(output "${output}" PARENT_SCOPE)
endfunction()

function(install_config config)
    run_checked("${CMAKE_COMMAND}" --install "${build_dir}" --prefix "${prefix}"
            --config "${config}")
endfunction()

install_config("${CONFIG}")

set(epm "${WORK_DIR}/path/epm")
file(CREATE_LINK "${prefix}/bin/epm" "${epm}" SYMBOLIC)

# recipe-sdk: the prefix's include/ and lib/<config>/, nothing of the trees.
run_checked("${epm}" recipe-sdk --out "${WORK_DIR}/recipe-sdk.cmake")
include("${WORK_DIR}/recipe-sdk.cmake")

if (NOT EPM_RECIPE_INCLUDE_DIRS STREQUAL "${prefix}/include")
    message(FATAL_ERROR "epm-install-round-trip: include directories "
            "'${EPM_RECIPE_INCLUDE_DIRS}', expected ${prefix}/include")
endif ()

string(TOUPPER "${CONFIG}" upper)
set(libraries "${EPM_RECIPE_LINK_LIBRARIES_${upper}}")

if (NOT libraries)
    message(FATAL_ERROR "epm-install-round-trip: no EPM_RECIPE_LINK_LIBRARIES_${upper}")
endif ()

foreach (library IN LISTS libraries)
    string(FIND "${library}" "${prefix}/lib/${CONFIG}/" found)

    if (NOT found EQUAL 0 OR NOT EXISTS "${library}")
        message(FATAL_ERROR "epm-install-round-trip: library ${library} is not "
                "in ${prefix}/lib/${CONFIG}/")
    endif ()
endforeach ()

file(READ "${WORK_DIR}/recipe-sdk.cmake" sdk_text)
string(REPLACE "${prefix}" "<prefix>" sdk_text "${sdk_text}")

foreach (tree IN ITEMS "${build_dir}" "${repo_dir}")
    string(FIND "${sdk_text}" "${tree}" found)

    if (NOT found EQUAL -1)
        message(FATAL_ERROR "epm-install-round-trip: recipe-sdk names ${tree}:\n"
                "${sdk_text}")
    endif ()
endforeach ()

# describe: compiles and runs a recipe against the installed SDK.
run_checked("${epm}" describe "${repo_dir}/Tests/Recipe/epm.cpp"
        --cache "${WORK_DIR}/cache")

if (NOT output MATCHES "\"fmtlib/fmt\"")
    message(FATAL_ERROR "epm-install-round-trip: describe printed:\n${output}")
endif ()

# configure: compiles a recipe against the installed SDK and runs its own
# configure command, which writes the graph files. The env names no epm, so
# epm hands the recipe one that names itself.
set(project_dir "${WORK_DIR}/project")
file(WRITE "${project_dir}/epm.cpp"
        "#include <epm/Recipe/Recipe.h>\n\n"
        "struct Empty final : epm::Recipe\n{\n"
        "    void describe() override {}\n};\n\nEPM_RECIPE(Empty)\n")
file(WRITE "${WORK_DIR}/env.json" "{\"schema\": 1}\n")
file(WRITE "${WORK_DIR}/overrides.json" "{\"schema\": 1}\n")
run_checked("${epm}" configure --manifest "${project_dir}/epm.cpp"
        --env "${WORK_DIR}/env.json" --overrides "${WORK_DIR}/overrides.json"
        --out "${WORK_DIR}/configure" --cache "${WORK_DIR}/cache" --verbose)

foreach (file IN ITEMS graph.cmake graph.json)
    if (NOT EXISTS "${WORK_DIR}/configure/${file}")
        message(FATAL_ERROR "epm-install-round-trip: configure wrote no ${file}:\n"
                "${output}")
    endif ()
endforeach ()

if (NOT output MATCHES "/epm-recipe configure --manifest ")
    message(FATAL_ERROR "epm-install-round-trip: configure ran no recipe "
            "executable:\n${output}")
endif ()

# One bin/epm per prefix: Release wins, any other configuration refreshes its
# own and replaces one that is not Release.
set(config_file "${prefix}/share/epm/epm-config")
file(STRINGS "${config_file}" installed_config)

if (NOT installed_config STREQUAL "${CONFIG}")
    message(FATAL_ERROR "epm-install-round-trip: epm-config says "
            "'${installed_config}', expected ${CONFIG}")
endif ()

function(expect_binary_kept kept why)
    file(READ "${prefix}/bin/epm" binary LIMIT 6)

    if (kept AND NOT binary STREQUAL "marker")
        message(FATAL_ERROR "epm-install-round-trip: bin/epm replaced: ${why}")
    elseif (NOT kept AND binary STREQUAL "marker")
        message(FATAL_ERROR "epm-install-round-trip: bin/epm kept: ${why}")
    endif ()
endfunction()

file(WRITE "${prefix}/bin/epm" "marker")
file(WRITE "${config_file}" "Release\n")
install_config(Debug)
expect_binary_kept(ON "a Debug install over a Release binary")

file(WRITE "${config_file}" "Debug\n")
install_config(Debug)
expect_binary_kept(OFF "a Debug install over a Debug binary")

file(WRITE "${prefix}/bin/epm" "marker")
file(WRITE "${config_file}" "RelWithDebInfo\n")
install_config(Debug)
expect_binary_kept(OFF "a Debug install over a RelWithDebInfo binary")

file(WRITE "${prefix}/bin/epm" "marker")
file(WRITE "${config_file}" "Debug\n")
install_config(Release)
expect_binary_kept(OFF "a Release install over a Debug binary")

message(STATUS "epm-install-round-trip: passed")
