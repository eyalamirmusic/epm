# epm.cmake -- the module that defines epm_add_package.
#
# Projects do not include this file directly: they include get_epm.cmake, which
# finds (or downloads, or builds) an epm binary, sets EPM_EXECUTABLE and then
# includes the epm.cmake installed with that binary, so the two always match.
#
#   include(path/to/get_epm.cmake)
#
#   epm_add_package(
#       NAME eacp
#       GITHUB_REPOSITORY eyalamirmusic/eacp      # or GIT_REPOSITORY <url>
#       GIT_TAG main                              # resolved to a commit by epm
#       MODE auto                                 # auto | prebuilt | source
#       OPTIONS "EACP_BUILD_GRAPHICS OFF" "EACP_ENABLE_TESTS OFF")
#
# Every call hands a request to the epm binary (`epm resolve`), which fetches the
# source and, unless the mode is `source`, builds or reuses cached binaries. The
# binary answers with a small CMake file saying which way the package went:
#
#   prebuilt  the cached targets file is included: imported targets under the
#             package's own names, pointing at cached archives. No add_subdirectory.
#   source    the OPTIONS are set as plain variables and the snapshot is added with
#             add_subdirectory, as CPM would.
#
# Either way <name>_SOURCE_DIR, <name>_ADDED and <name>_MODE are set in the
# caller's scope (plus <name>_BINARY_DIR in source mode).
#
# Variables read:
#   EPM_EXECUTABLE              the epm binary (required; set by get_epm.cmake)
#   EPM_BUILD_MISSING_BINARIES  ON (default): `auto` builds a missing binary
#                               entry; OFF: `auto` falls back to source instead
#   EPM_CACHE                   cache root handed to the binary as --cache
#                               (else the EPM_CACHE environment variable, else the
#                               platform cache directory, as the binary does)
#   EPM_VERBOSE                 pass --verbose and echo the binary's output
#
# Files exchanged with the binary live under ${CMAKE_BINARY_DIR}/epm/:
#   env.json                    toolchain description, written once per configure
#   requests/<name>.json        one per package
#   responses/<name>.cmake      written by the binary, included here

include_guard(GLOBAL)

if (NOT EPM_EXECUTABLE)
    message(FATAL_ERROR
            "epm: EPM_EXECUTABLE is not set. Include get_epm.cmake instead of "
            "epm.cmake: it finds the epm binary and includes the epm.cmake that "
            "belongs to it.")
endif ()

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

epm_set_policies()

option(EPM_BUILD_MISSING_BINARIES
        "Let MODE auto build a missing binary cache entry (OFF: fall back to source)"
        ON)

set(EPM_DIR "${CMAKE_BINARY_DIR}/epm")

# Quotes `value` as a JSON string literal, for string(JSON ... SET), which takes
# JSON text and so cannot be handed a raw string.
function(epm_json_quote value out_var)
    string(REPLACE "\\" "\\\\" value "${value}")
    string(REPLACE "\"" "\\\"" value "${value}")
    string(REPLACE "\n" "\\n" value "${value}")
    string(REPLACE "\r" "\\r" value "${value}")
    string(REPLACE "\t" "\\t" value "${value}")
    set(${out_var} "\"${value}\"" PARENT_SCOPE)
endfunction()

# Sets `key` of the JSON object held in `json_var` to the string `value`.
function(epm_json_set_string json_var key value)
    epm_json_quote("${value}" quoted)
    string(JSON json SET "${${json_var}}" "${key}" "${quoted}")
    set(${json_var} "${json}" PARENT_SCOPE)
endfunction()

# Splits a CPM-style option, "KEY VALUE" (or a bare "KEY", meaning ON). The value
# keeps any spaces after the first one.
function(epm_parse_option option)
    string(REGEX MATCH "^[^ ]+" key "${option}")
    string(LENGTH "${option}" option_length)
    string(LENGTH "${key}" key_length)

    if (key_length EQUAL option_length)
        set(value "ON")
    else ()
        math(EXPR key_length "${key_length} + 1")
        string(SUBSTRING "${option}" ${key_length} -1 value)
    endif ()

    set(EPM_OPTION_KEY "${key}" PARENT_SCOPE)
    set(EPM_OPTION_VALUE "${value}" PARENT_SCOPE)
endfunction()

# Writes ${EPM_DIR}/env.json, once per configure (on the first epm_add_package,
# so a CMAKE_CXX_STANDARD set after the include is still seen).
function(epm_write_env)
    get_property(written GLOBAL PROPERTY EPM_ENV_WRITTEN)

    if (written)
        return()
    endif ()

    get_property(is_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)

    if (is_multi_config)
        set(is_multi_config true)
    else ()
        set(is_multi_config false)
    endif ()

    set(json "{}")
    string(JSON json SET "${json}" schema 1)

    foreach (pair IN ITEMS
            "cmake=CMAKE_COMMAND"
            "cmakeVersion=CMAKE_VERSION"
            "generator=CMAKE_GENERATOR"
            "makeProgram=CMAKE_MAKE_PROGRAM")
        string(REPLACE "=" ";" pair "${pair}")
        list(GET pair 0 key)
        list(GET pair 1 var)
        epm_json_set_string(json ${key} "${${var}}")
    endforeach ()

    string(JSON json SET "${json}" isMultiConfig ${is_multi_config})
    epm_json_set_string(json buildType "${CMAKE_BUILD_TYPE}")

    set(config_types "[]")
    set(index 0)

    foreach (config IN LISTS CMAKE_CONFIGURATION_TYPES)
        epm_json_quote("${config}" quoted)
        string(JSON config_types SET "${config_types}" ${index} "${quoted}")
        math(EXPR index "${index} + 1")
    endforeach ()

    string(JSON json SET "${json}" configurationTypes "${config_types}")

    foreach (pair IN ITEMS
            "cCompiler=CMAKE_C_COMPILER"
            "cxxCompiler=CMAKE_CXX_COMPILER"
            "cxxCompilerId=CMAKE_CXX_COMPILER_ID"
            "cxxCompilerVersion=CMAKE_CXX_COMPILER_VERSION"
            "systemName=CMAKE_SYSTEM_NAME"
            "systemProcessor=CMAKE_SYSTEM_PROCESSOR"
            "osxArchitectures=CMAKE_OSX_ARCHITECTURES"
            "osxDeploymentTarget=CMAKE_OSX_DEPLOYMENT_TARGET"
            "cxxStandard=CMAKE_CXX_STANDARD"
            "toolchainFile=CMAKE_TOOLCHAIN_FILE")
        string(REPLACE "=" ";" pair "${pair}")
        list(GET pair 0 key)
        list(GET pair 1 var)
        epm_json_set_string(json ${key} "${${var}}")
    endforeach ()

    file(WRITE "${EPM_DIR}/env.json" "${json}\n")
    set_property(GLOBAL PROPERTY EPM_ENV_WRITTEN TRUE)
endfunction()

# A function, as CPMAddPackage is: OPTIONS stay local to the add_subdirectory
# they configure, and the results are handed to the caller with PARENT_SCOPE.
function(epm_add_package)
    epm_set_policies()

    cmake_parse_arguments(EPM_ARGS ""
            "NAME;GITHUB_REPOSITORY;GIT_REPOSITORY;GIT_TAG;MODE"
            "OPTIONS" ${ARGN})

    if (EPM_ARGS_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
                "epm_add_package: unknown arguments: ${EPM_ARGS_UNPARSED_ARGUMENTS}")
    endif ()

    if (EPM_ARGS_GITHUB_REPOSITORY AND EPM_ARGS_GIT_REPOSITORY)
        message(FATAL_ERROR
                "epm_add_package: give GITHUB_REPOSITORY or GIT_REPOSITORY, not both")
    endif ()

    if (EPM_ARGS_GITHUB_REPOSITORY)
        if (NOT EPM_ARGS_GITHUB_REPOSITORY MATCHES "^[^/ ]+/[^/ ]+$")
            message(FATAL_ERROR
                    "epm_add_package: GITHUB_REPOSITORY must be <user>/<repo>, "
                    "got '${EPM_ARGS_GITHUB_REPOSITORY}'")
        endif ()

        set(git_url "https://github.com/${EPM_ARGS_GITHUB_REPOSITORY}.git")
    elseif (EPM_ARGS_GIT_REPOSITORY)
        set(git_url "${EPM_ARGS_GIT_REPOSITORY}")
    else ()
        message(FATAL_ERROR
                "epm_add_package: GITHUB_REPOSITORY or GIT_REPOSITORY is required")
    endif ()

    set(name "${EPM_ARGS_NAME}")

    if (NOT name)
        # As CPM does: the repository's name, without a trailing .git.
        string(REGEX REPLACE "^.*/([^/]+)$" "\\1" name "${git_url}")
        string(REGEX REPLACE "\\.git$" "" name "${name}")
    endif ()

    if (NOT EPM_ARGS_GIT_TAG)
        message(FATAL_ERROR "epm_add_package(${name}): GIT_TAG is required")
    endif ()

    set(mode "${EPM_ARGS_MODE}")

    if (NOT mode)
        set(mode "auto")
    endif ()

    if (NOT mode MATCHES "^(auto|prebuilt|source)$")
        message(FATAL_ERROR
                "epm_add_package(${name}): MODE must be auto, prebuilt or source, "
                "got '${mode}'")
    endif ()

    if (EPM_BUILD_MISSING_BINARIES)
        set(build_missing true)
    else ()
        set(build_missing false)
    endif ()

    set(options_json "{}")

    foreach (option IN LISTS EPM_ARGS_OPTIONS)
        epm_parse_option("${option}")
        epm_json_set_string(options_json "${EPM_OPTION_KEY}" "${EPM_OPTION_VALUE}")
    endforeach ()

    set(request "{}")
    string(JSON request SET "${request}" schema 1)
    epm_json_set_string(request name "${name}")
    epm_json_set_string(request gitUrl "${git_url}")
    epm_json_set_string(request gitRef "${EPM_ARGS_GIT_TAG}")
    epm_json_set_string(request mode "${mode}")
    string(JSON request SET "${request}" options "${options_json}")
    string(JSON request SET "${request}" buildMissingBinaries ${build_missing})

    # A package already added in this configure: hand back the first result.
    get_property(previous_request GLOBAL PROPERTY EPM_PACKAGE_${name}_REQUEST)

    if (DEFINED previous_request AND NOT previous_request STREQUAL "")
        if (NOT previous_request STREQUAL request)
            message(WARNING
                    "epm: ${name} was already added with a different request; "
                    "using the first one.\n  first:  ${previous_request}\n"
                    "  this:   ${request}")
        endif ()

        get_property(first_mode GLOBAL PROPERTY EPM_PACKAGE_${name}_MODE)
        get_property(first_source_dir GLOBAL PROPERTY EPM_PACKAGE_${name}_SOURCE_DIR)
        get_property(first_binary_dir GLOBAL PROPERTY EPM_PACKAGE_${name}_BINARY_DIR)

        set(${name}_SOURCE_DIR "${first_source_dir}" PARENT_SCOPE)
        set(${name}_MODE "${first_mode}" PARENT_SCOPE)
        set(${name}_ADDED NO PARENT_SCOPE)

        if (first_mode STREQUAL "source")
            set(${name}_BINARY_DIR "${first_binary_dir}" PARENT_SCOPE)
        endif ()

        return()
    endif ()

    epm_write_env()

    set(request_file "${EPM_DIR}/requests/${name}.json")
    set(response_file "${EPM_DIR}/responses/${name}.cmake")

    file(WRITE "${request_file}" "${request}\n")
    file(MAKE_DIRECTORY "${EPM_DIR}/responses")
    file(REMOVE "${response_file}")

    set(command "${EPM_EXECUTABLE}" resolve
            --request "${request_file}"
            --env "${EPM_DIR}/env.json"
            --response "${response_file}")

    if (EPM_CACHE)
        list(APPEND command --cache "${EPM_CACHE}")
    endif ()

    set(echo_args "")

    if (EPM_VERBOSE)
        list(APPEND command --verbose)
        set(echo_args ECHO_OUTPUT_VARIABLE ECHO_ERROR_VARIABLE)
    endif ()

    execute_process(COMMAND ${command}
            RESULT_VARIABLE result
            OUTPUT_VARIABLE output
            ERROR_VARIABLE error
            ${echo_args})

    if (NOT result EQUAL 0)
        string(STRIP "${error}" error)
        message(FATAL_ERROR
                "epm: resolving ${name} failed (exit ${result}): ${error}")
    endif ()

    if (NOT EXISTS "${response_file}")
        message(FATAL_ERROR
                "epm: resolving ${name} succeeded but wrote no response file "
                "(${response_file})")
    endif ()

    include("${response_file}")

    set(resolved_mode "${epm_${name}_MODE}")
    set(source_dir "${epm_${name}_SOURCE_DIR}")
    set(binary_dir "")

    if (resolved_mode STREQUAL "prebuilt")
        set(targets_file "${epm_${name}_TARGETS_FILE}")

        if (NOT EXISTS "${targets_file}")
            message(FATAL_ERROR
                    "epm: ${name} resolved to prebuilt, but its targets file "
                    "'${targets_file}' does not exist")
        endif ()

        # Imported targets are local to the directory that creates them; make the
        # new ones global so they are seen everywhere, as add_subdirectory'd
        # targets would be.
        get_property(imported_before DIRECTORY PROPERTY IMPORTED_TARGETS)
        include("${targets_file}")
        get_property(imported_after DIRECTORY PROPERTY IMPORTED_TARGETS)

        foreach (target IN LISTS imported_after)
            if (NOT target IN_LIST imported_before)
                set_property(TARGET ${target} PROPERTY IMPORTED_GLOBAL TRUE)
            endif ()
        endforeach ()

        get_filename_component(id "${epm_${name}_PREFIX}" NAME)
    elseif (resolved_mode STREQUAL "source")
        if (NOT EXISTS "${source_dir}/CMakeLists.txt")
            message(FATAL_ERROR
                    "epm: ${name} resolved to source, but '${source_dir}' has no "
                    "CMakeLists.txt")
        endif ()

        foreach (option IN LISTS EPM_ARGS_OPTIONS)
            epm_parse_option("${option}")
            set(${EPM_OPTION_KEY} "${EPM_OPTION_VALUE}")
        endforeach ()

        set(binary_dir "${CMAKE_BINARY_DIR}/_deps/${name}-build")
        add_subdirectory("${source_dir}" "${binary_dir}")

        get_filename_component(id "${source_dir}" NAME)
    else ()
        message(FATAL_ERROR
                "epm: the response for ${name} has an unknown mode "
                "'${resolved_mode}' (${response_file})")
    endif ()

    message(STATUS "epm: ${name} (${resolved_mode}, ${id})")

    set_property(GLOBAL PROPERTY EPM_PACKAGE_${name}_REQUEST "${request}")
    set_property(GLOBAL PROPERTY EPM_PACKAGE_${name}_MODE "${resolved_mode}")
    set_property(GLOBAL PROPERTY EPM_PACKAGE_${name}_SOURCE_DIR "${source_dir}")
    set_property(GLOBAL PROPERTY EPM_PACKAGE_${name}_BINARY_DIR "${binary_dir}")

    set(${name}_SOURCE_DIR "${source_dir}" PARENT_SCOPE)
    set(${name}_MODE "${resolved_mode}" PARENT_SCOPE)
    set(${name}_ADDED YES PARENT_SCOPE)

    if (resolved_mode STREQUAL "source")
        set(${name}_BINARY_DIR "${binary_dir}" PARENT_SCOPE)
    endif ()
endfunction()
