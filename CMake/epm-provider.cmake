# epm-provider.cmake -- the dependency provider fed from graph.cmake, and the
# package-loading helpers epm.cmake shares with it. Functions only: including
# this file does nothing else.
#
#   epm_provider_register(<graph.cmake> [PREPARE <command>])
#
# registers epm_provide_dependency for find_package() and
# FetchContent_MakeAvailable(). It must be called while the first project()
# processes CMAKE_PROJECT_TOP_LEVEL_INCLUDES (a CMake rule for every dependency
# provider). The graph file is read at the first call the
# provider receives, after running PREPARE once when one is given, so a
# PREPARE command that writes the graph sees the variables the project set
# between project() and its first find_package (CMAKE_CXX_STANDARD, say).
#
# On find_package(X) or FetchContent_MakeAvailable(X), for X in epm_PACKAGES:
# the names in epm_X_DEPS are loaded first, the same way, then X itself, once
# per configure:
#
#   source    epm_X_OPTIONS (KEY=VALUE) become plain variables and
#             epm_X_SOURCE_DIR is added as ${CMAKE_BINARY_DIR}/_deps/X-build
#   prebuilt  epm_X_TARGETS_FILE is included and its imported targets made
#             GLOBAL
#
# find_package then gets X_FOUND, X_SOURCE_DIR and X_MODE; FetchContent gets
# FetchContent_SetPopulated(X). Version arguments are ignored. A name that is
# not in epm_PACKAGES goes to epm.cmake: a FetchContent_MakeAvailable of one
# through the lock (epm_provide_through_the_lock), a find_package of one as a
# package added (epm_find_through_the_lock), unless an epm.cpp marks it
# system (Project::addSystem, epm_SYSTEM_PACKAGES), which alone leaves it to CMake's own search.
# Registering twice only replaces the graph.

include_guard(GLOBAL)

# The functions below are defined, and so run, with these policies whatever
# the including project asks for (IN_LIST, among others).
cmake_policy(VERSION 3.24...3.31)

# The same policies CPM sets (cpm_set_policies in CPM.cmake 0.40.2): with CMP0077
# and CMP0126 NEW a plain variable set before add_subdirectory wins over the
# subproject's option() / set(CACHE), so OPTIONS take effect without touching the
# cache. The CMAKE_POLICY_DEFAULT_* variables carry the setting into subprojects
# whose own cmake_minimum_required predates the policies.
macro(epm_set_policies)
    cmake_policy(SET CMP0077 NEW)
    set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)

    if (POLICY CMP0126)
        cmake_policy(SET CMP0126 NEW)
        set(CMAKE_POLICY_DEFAULT_CMP0126 NEW)
    endif ()
endmacro()

# Sets each KEY=VALUE argument as a plain variable in the caller's scope. A bare
# KEY means ON; the value is everything after the first `=`.
function(epm_set_options)
    foreach (option IN LISTS ARGN)
        string(FIND "${option}" "=" equals)

        if (equals EQUAL -1)
            set(key "${option}")
            set(value "ON")
        else ()
            string(SUBSTRING "${option}" 0 ${equals} key)
            math(EXPR equals "${equals} + 1")
            string(SUBSTRING "${option}" ${equals} -1 value)
        endif ()

        if (key STREQUAL "")
            message(FATAL_ERROR "epm: option '${option}' has no name")
        endif ()

        set(${key} "${value}" PARENT_SCOPE)
    endforeach ()
endfunction()

# Includes a targets file and makes the imported targets it creates GLOBAL.
# Imported targets are local to the directory that creates them; global, they
# are seen everywhere, as add_subdirectory'd targets would be.
function(epm_include_targets_globally targets_file)
    get_property(imported_before DIRECTORY PROPERTY IMPORTED_TARGETS)
    include("${targets_file}")
    get_property(imported_after DIRECTORY PROPERTY IMPORTED_TARGETS)

    foreach (target IN LISTS imported_after)
        if (NOT target IN_LIST imported_before)
            get_property(global TARGET ${target} PROPERTY IMPORTED_GLOBAL)

            if (NOT global)
                set_property(TARGET ${target} PROPERTY IMPORTED_GLOBAL TRUE)
            endif ()
        endif ()
    endforeach ()
endfunction()

# Brings one resolved package into the build, the same way for epm_add_package
# and the provider, and prints its status line:
#
#   epm_load_package(<name> MODE prebuilt|source SOURCE_DIR <dir>
#       [TARGETS_FILE <file>] [PREFIX <dir>] [PLATFORM <platform>] [NOTE <text>]
#       [OPTIONS KEY=VALUE...] [ORIGIN <text for errors>])
#
# Records the result in the GLOBAL properties EPM_PACKAGE_<name>_MODE,
# _SOURCE_DIR and _BINARY_DIR (empty when prebuilt).
function(epm_load_package name)
    cmake_parse_arguments(arg ""
            "MODE;SOURCE_DIR;TARGETS_FILE;PREFIX;PLATFORM;NOTE;ORIGIN" "OPTIONS"
            ${ARGN})

    set(origin "")

    if (arg_ORIGIN)
        set(origin " (${arg_ORIGIN})")
    endif ()

    set(binary_dir "")

    if (arg_MODE STREQUAL "prebuilt")
        if (NOT EXISTS "${arg_TARGETS_FILE}")
            message(FATAL_ERROR
                    "epm: ${name} resolved to prebuilt, but its targets file "
                    "'${arg_TARGETS_FILE}' does not exist${origin}")
        endif ()

        epm_include_targets_globally("${arg_TARGETS_FILE}")

        # PREFIX is <cache>/bin/<name>/<platform>/<fp12>.
        get_filename_component(id "${arg_PREFIX}" NAME)
        set(platform "${arg_PLATFORM}")

        if (NOT platform)
            get_filename_component(platform "${arg_PREFIX}" DIRECTORY)
            get_filename_component(platform "${platform}" NAME)
        endif ()

        set(id "${platform}, ${id}")

        if (arg_NOTE)
            string(APPEND id ", ${arg_NOTE}")
        endif ()
    elseif (arg_MODE STREQUAL "source")
        if (NOT EXISTS "${arg_SOURCE_DIR}/CMakeLists.txt")
            message(FATAL_ERROR
                    "epm: ${name} resolved to source, but '${arg_SOURCE_DIR}' has "
                    "no CMakeLists.txt${origin}")
        endif ()

        epm_set_policies()
        epm_set_options(${arg_OPTIONS})

        set(binary_dir "${CMAKE_BINARY_DIR}/_deps/${name}-build")
        # As a prebuilt entry would, a source package builds only what links it.
        add_subdirectory("${arg_SOURCE_DIR}" "${binary_dir}" EXCLUDE_FROM_ALL)

        get_filename_component(id "${arg_SOURCE_DIR}" NAME)
    else ()
        message(FATAL_ERROR
                "epm: ${name} has an unknown mode '${arg_MODE}'${origin}")
    endif ()

    message(STATUS "epm: ${name} (${arg_MODE}, ${id})")

    set_property(GLOBAL PROPERTY EPM_PACKAGE_${name}_MODE "${arg_MODE}")
    set_property(GLOBAL PROPERTY EPM_PACKAGE_${name}_SOURCE_DIR "${arg_SOURCE_DIR}")
    set_property(GLOBAL PROPERTY EPM_PACKAGE_${name}_BINARY_DIR "${binary_dir}")
endfunction()

function(epm_provider_register graph_file)
    cmake_parse_arguments(arg "" "PREPARE" "" ${ARGN})

    if (CMAKE_VERSION VERSION_LESS 3.24)
        message(FATAL_ERROR "epm: epm.cpp needs CMake 3.24 or newer (dependency "
                "providers); this is ${CMAKE_VERSION}")
    endif ()

    if (arg_PREPARE AND NOT COMMAND "${arg_PREPARE}")
        message(FATAL_ERROR "epm_provider_register: no command '${arg_PREPARE}'")
    endif ()

    set_property(GLOBAL PROPERTY EPM_PROVIDER_GRAPH "${graph_file}")
    set_property(GLOBAL PROPERTY EPM_PROVIDER_PREPARE "${arg_PREPARE}")
    get_property(registered GLOBAL PROPERTY EPM_PROVIDER_REGISTERED)

    if (NOT registered)
        set_property(GLOBAL PROPERTY EPM_PROVIDER_REGISTERED TRUE)
        cmake_language(SET_DEPENDENCY_PROVIDER epm_provide_dependency
                SUPPORTED_METHODS FIND_PACKAGE FETCHCONTENT_MAKEAVAILABLE_SERIAL)
    endif ()
endfunction()

# Runs the PREPARE command once, then includes the graph file into the
# caller's scope and prints its notes once. A macro, for that include.
macro(epm_provider_read_graph)
    get_property(_epm_prepare GLOBAL PROPERTY EPM_PROVIDER_PREPARE)

    if (_epm_prepare)
        set_property(GLOBAL PROPERTY EPM_PROVIDER_PREPARE "")
        cmake_language(CALL "${_epm_prepare}")
    endif ()

    get_property(_epm_graph GLOBAL PROPERTY EPM_PROVIDER_GRAPH)
    set(epm_PACKAGES "")

    if (NOT "${_epm_graph}" STREQUAL "")
        if (NOT EXISTS "${_epm_graph}")
            message(FATAL_ERROR
                    "epm: the dependency graph '${_epm_graph}' does not exist")
        endif ()

        include("${_epm_graph}")
    endif ()

    get_property(_epm_notes_shown GLOBAL PROPERTY EPM_PROVIDER_NOTES_SHOWN)

    if (NOT _epm_notes_shown)
        set_property(GLOBAL PROPERTY EPM_PROVIDER_NOTES_SHOWN TRUE)

        foreach (_epm_note IN LISTS epm_NOTES)
            message(STATUS "epm: note: ${_epm_note}")
        endforeach ()
    endif ()
endmacro()

# Loads `name` from the graph in scope, its dependencies first. Once per
# configure: a package epm_add_package already added counts as loaded.
function(epm_provider_load name)
    get_property(loaded GLOBAL PROPERTY EPM_PACKAGE_${name}_MODE SET)

    if (loaded)
        return()
    endif ()

    get_property(loading GLOBAL PROPERTY EPM_PROVIDER_LOADING)

    if (name IN_LIST loading)
        message(FATAL_ERROR "epm: dependency cycle: ${loading};${name}")
    endif ()

    set_property(GLOBAL APPEND PROPERTY EPM_PROVIDER_LOADING "${name}")

    foreach (dep IN LISTS epm_${name}_DEPS)
        if (NOT dep IN_LIST epm_PACKAGES)
            message(FATAL_ERROR "epm: ${name} depends on ${dep}, which is not in "
                    "the graph")
        endif ()

        epm_provider_load("${dep}")
    endforeach ()

    epm_load_package("${name}"
            MODE "${epm_${name}_MODE}"
            SOURCE_DIR "${epm_${name}_SOURCE_DIR}"
            TARGETS_FILE "${epm_${name}_TARGETS_FILE}"
            PREFIX "${epm_${name}_PREFIX}"
            PLATFORM "${epm_${name}_PLATFORM}"
            NOTE "${epm_${name}_NOTE}"
            OPTIONS ${epm_${name}_OPTIONS}
            ORIGIN "${_epm_graph}")

    get_property(loading GLOBAL PROPERTY EPM_PROVIDER_LOADING)
    list(REMOVE_ITEM loading "${name}")
    set_property(GLOBAL PROPERTY EPM_PROVIDER_LOADING "${loading}")
endfunction()

# The provider: find_package(<name> ...) arrives as FIND_PACKAGE <name> ...,
# FetchContent_MakeAvailable(<name>) as FETCHCONTENT_MAKEAVAILABLE_SERIAL
# <name> <declared details>.
function(epm_provide_dependency method name)
    epm_provider_read_graph()

    if (NOT name IN_LIST epm_PACKAGES)
        if (method STREQUAL "FETCHCONTENT_MAKEAVAILABLE_SERIAL"
                AND COMMAND epm_provide_through_the_lock)
            epm_provide_through_the_lock("${name}" ${ARGN})
            return()
        endif ()

        if (NOT method STREQUAL "FIND_PACKAGE" OR name IN_LIST epm_SYSTEM_PACKAGES
                OR NOT COMMAND epm_find_through_the_lock)
            return()
        endif ()

        # A Find module the project ships is its own code, not a system lookup.
        foreach (module_dir IN LISTS CMAKE_MODULE_PATH)
            if (EXISTS "${module_dir}/Find${name}.cmake")
                return()
            endif ()
        endforeach ()

        get_property(loaded GLOBAL PROPERTY EPM_PACKAGE_${name}_MODE SET)

        if (loaded)
            get_property(${name}_SOURCE_DIR GLOBAL PROPERTY EPM_PACKAGE_${name}_SOURCE_DIR)
        else ()
            set(_epm_find_is_system FALSE)
            epm_find_through_the_lock("${name}")

            # The root's epm.cpp marks it system: CMake's own search.
            if (_epm_find_is_system)
                return()
            endif ()
        endif ()

        set(${name}_FOUND TRUE PARENT_SCOPE)
        set(${name}_SOURCE_DIR "${${name}_SOURCE_DIR}" PARENT_SCOPE)
        return()
    endif ()

    epm_provider_load("${name}")

    get_property(mode GLOBAL PROPERTY EPM_PACKAGE_${name}_MODE)
    get_property(source_dir GLOBAL PROPERTY EPM_PACKAGE_${name}_SOURCE_DIR)
    get_property(binary_dir GLOBAL PROPERTY EPM_PACKAGE_${name}_BINARY_DIR)

    if (method STREQUAL "FIND_PACKAGE")
        set(${name}_FOUND TRUE PARENT_SCOPE)
        set(${name}_SOURCE_DIR "${source_dir}" PARENT_SCOPE)
        set(${name}_MODE "${mode}" PARENT_SCOPE)
    elseif (binary_dir)
        FetchContent_SetPopulated("${name}"
                SOURCE_DIR "${source_dir}" BINARY_DIR "${binary_dir}")
    else ()
        FetchContent_SetPopulated("${name}" SOURCE_DIR "${source_dir}")
    endif ()
endfunction()
