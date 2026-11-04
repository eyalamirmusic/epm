# epm.cmake -- the module that drives the epm binary from CMake.
#
# Projects do not include this file directly: they list get_epm.cmake in
# CMAKE_PROJECT_TOP_LEVEL_INCLUDES (or include it), and it finds (or downloads,
# or builds) an epm binary, sets EPM_EXECUTABLE and then includes the epm.cmake
# installed with that binary, so the two always match. epm-provider.cmake must
# sit beside this file.
#
# Two flows, while the second replaces the first (Docs/Graph.md):
#
# 1. A manifest, epm.json beside the top-level CMakeLists.txt. get_epm.cmake
#    calls epm_use_manifest, which registers the dependency provider of
#    epm-provider.cmake. The provider's first call writes env.json and
#    overrides.json, runs `epm configure` for the whole graph and reads the
#    graph.cmake it writes; find_package(<name>) then loads each package.
#
# 2. epm_add_package, one package per call (Docs/PoC.md):
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
#   EPM_BUILD_MISSING_BINARIES  ON (the default when unset): `auto` builds a
#                               missing binary entry; OFF: `auto` falls back to
#                               source instead
#   EPM_CACHE                   cache root handed to the binary as --cache
#                               (else the EPM_CACHE environment variable, else the
#                               platform cache directory, as the binary does)
#   EPM_VERBOSE                 pass --verbose and echo the binary's output
#   EPM_CONFIGS                 auto (the default): a single-config generator
#                               gets CMAKE_BUILD_TYPE (empty, RelWithDebInfo and
#                               MinSizeRel mean Release), a multi-config one
#                               Debug;Release. Or a list of Debug, Release,
#                               RelWithDebInfo, MinSizeRel (the last two are
#                               built as Release). A cache entry gains configs
#                               as they are asked for; it never rebuilds one.
#   EPM_ABI_MATCH               which cached build may be used: compatible
#                               (default: same platform, compiler id and major
#                               version, deployment target, C++ standard,
#                               options, source, toolchain file), strict (every
#                               recorded detail, compiler path and full version
#                               included) or any (platform, options, source)
#
# With a manifest, EPM_CONFIGS, EPM_ABI_MATCH and EPM_BUILD_MISSING_BINARIES are
# overrides: unset or empty, the manifest (or the binary's default) decides.
# The manifest flow also reads EPM_PROFILE, EPM_MODE, EPM_STRICT and
# EPM_<name>_MODE, _SOURCE, _CONFIGS and _OPTIONS (KEY=VALUE list); see
# epm_write_overrides.
#
# Files exchanged with the binary live under ${CMAKE_BINARY_DIR}/epm/:
#   env.json                    toolchain description, written once per configure
#   overrides.json              the overrides above (manifest flow)
#   graph.cmake, graph.json     written by `epm configure` (manifest flow)
#   requests/<name>.json        one per epm_add_package
#   responses/<name>.cmake      written by `epm resolve`, included here

include_guard(GLOBAL)

include("${CMAKE_CURRENT_LIST_DIR}/epm-provider.cmake")

# Resolves an EPM_CONFIGS value into the configs to build and cache, each one
# Debug or Release (RelWithDebInfo and MinSizeRel are built as Release), Debug
# first. `auto` (or empty) takes `build_type` for a single-config generator,
# where an empty or unknown type means Release, and Debug;Release for a
# multi-config one. Sets `error_var` instead for an unknown config name.
function(epm_resolve_configs spec is_multi_config build_type out_var error_var)
    set(configs "")
    set(error "")
    string(TOLOWER "${spec}" lower_spec)

    if (lower_spec STREQUAL "auto" OR lower_spec STREQUAL "")
        string(TOLOWER "${build_type}" lower_type)

        if (is_multi_config)
            set(configs Debug Release)
        elseif (lower_type STREQUAL "debug")
            set(configs Debug)
        else ()
            set(configs Release)
        endif ()
    else ()
        set(debug FALSE)
        set(release FALSE)

        foreach (config IN LISTS spec)
            string(TOLOWER "${config}" lower)

            if (lower STREQUAL "debug")
                set(debug TRUE)
            elseif (lower MATCHES "^(release|relwithdebinfo|minsizerel)$")
                set(release TRUE)
            else ()
                set(error "EPM_CONFIGS has an unknown configuration '${config}': "
                        "give auto, or a list of Debug, Release, RelWithDebInfo "
                        "and MinSizeRel")
                break()
            endif ()
        endforeach ()

        if (debug AND NOT error)
            list(APPEND configs Debug)
        endif ()

        if (release AND NOT error)
            list(APPEND configs Release)
        endif ()

        if (NOT configs AND NOT error)
            set(error "EPM_CONFIGS lists no configuration")
        endif ()
    endif ()

    set(${out_var} "${configs}" PARENT_SCOPE)
    set(${error_var} "${error}" PARENT_SCOPE)
endfunction()

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

# A JSON array of the strings in ARGN.
function(epm_json_string_array out_var)
    set(array "[]")
    set(index 0)

    foreach (item IN LISTS ARGN)
        epm_json_quote("${item}" quoted)
        string(JSON array SET "${array}" ${index} "${quoted}")
        math(EXPR index "${index} + 1")
    endforeach ()

    set(${out_var} "${array}" PARENT_SCOPE)
endfunction()

# Writes overrides.json (Docs/Graph.md §3) from the EPM_* variables visible
# here, normal or cache, a normal one winning as ${} has it. Empty values are
# left out; EPM_<name>_SOURCE is made absolute against CMAKE_SOURCE_DIR.
# buildMissingBinaries (EPM_BUILD_MISSING_BINARIES) is written only when set.
function(epm_write_overrides file)
    set(json "{}")
    string(JSON json SET "${json}" schema 1)
    epm_json_set_string(json profile "${EPM_PROFILE}")
    epm_json_set_string(json mode "${EPM_MODE}")
    epm_json_string_array(configs ${EPM_CONFIGS})
    string(JSON json SET "${json}" configs "${configs}")
    epm_json_set_string(json abiMatch "${EPM_ABI_MATCH}")

    if (EPM_STRICT)
        string(JSON json SET "${json}" strict true)
    else ()
        string(JSON json SET "${json}" strict false)
    endif ()

    if (NOT "${EPM_BUILD_MISSING_BINARIES}" STREQUAL "")
        if (EPM_BUILD_MISSING_BINARIES)
            string(JSON json SET "${json}" buildMissingBinaries true)
        else ()
            string(JSON json SET "${json}" buildMissingBinaries false)
        endif ()
    endif ()

    get_cmake_property(normal_names VARIABLES)
    get_cmake_property(cache_names CACHE_VARIABLES)
    set(names ${normal_names} ${cache_names})
    list(REMOVE_DUPLICATES names)
    list(SORT names)

    set(packages "{}")
    set(empty_package [=[{"mode": "", "source": "", "configs": [], "options": {}}]=])

    foreach (var IN LISTS names)
        # Only matching names are read: reading some others warns (CMP0218).
        if (NOT var MATCHES "^EPM_(.+)_(MODE|SOURCE|CONFIGS|OPTIONS)$")
            continue()
        endif ()

        set(name "${CMAKE_MATCH_1}")
        set(field "${CMAKE_MATCH_2}")
        set(value "${${var}}")

        if (value STREQUAL "")
            continue()
        endif ()

        string(JSON package ERROR_VARIABLE missing GET "${packages}" "${name}")

        if (missing)
            set(package "${empty_package}")
        endif ()

        if (field STREQUAL "MODE")
            epm_json_set_string(package mode "${value}")
        elseif (field STREQUAL "SOURCE")
            if (value MATCHES "^~([/\\]|$)")
                set(home "$ENV{HOME}")

                if (home STREQUAL "" AND CMAKE_HOST_WIN32)
                    set(home "$ENV{USERPROFILE}")
                endif ()

                string(REGEX REPLACE "^~" "${home}" value "${value}")
            endif ()

            file(TO_CMAKE_PATH "${value}" value)
            cmake_path(ABSOLUTE_PATH value BASE_DIRECTORY "${CMAKE_SOURCE_DIR}"
                    NORMALIZE)
            epm_json_set_string(package source "${value}")
        elseif (field STREQUAL "CONFIGS")
            epm_json_string_array(configs ${value})
            string(JSON package SET "${package}" configs "${configs}")
        else ()
            set(options "{}")

            foreach (option IN LISTS value)
                string(FIND "${option}" "=" equals)

                if (equals EQUAL -1)
                    set(key "${option}")
                    set(option_value "ON")
                else ()
                    string(SUBSTRING "${option}" 0 ${equals} key)
                    math(EXPR equals "${equals} + 1")
                    string(SUBSTRING "${option}" ${equals} -1 option_value)
                endif ()

                if (key STREQUAL "")
                    message(FATAL_ERROR "epm: ${var} has an option without a name: "
                            "'${option}' (give KEY=VALUE)")
                endif ()

                epm_json_set_string(options "${key}" "${option_value}")
            endforeach ()

            string(JSON package SET "${package}" options "${options}")
        endif ()

        string(JSON packages SET "${packages}" "${name}" "${package}")
    endforeach ()

    string(JSON json SET "${json}" packages "${packages}")
    file(WRITE "${file}" "${json}\n")
endfunction()

# Tests/EpmConfigsAuto.cmake and Tests/EpmOverrides.cmake want the functions
# above only.
if (EPM_FUNCTIONS_ONLY)
    return()
endif ()

if (NOT EPM_EXECUTABLE)
    message(FATAL_ERROR
            "epm: EPM_EXECUTABLE is not set. Include get_epm.cmake instead of "
            "epm.cmake: it finds the epm binary and includes the epm.cmake that "
            "belongs to it.")
endif ()

epm_set_policies()

# These three are empty by default (EPM_BUILD_MISSING_BINARIES unset), so a
# default never overrides a manifest's setting.
set(EPM_CONFIGS "" CACHE STRING
        "Configs to build and cache: auto (single-config: CMAKE_BUILD_TYPE; \
multi-config: Debug;Release) or a list of Debug, Release, RelWithDebInfo, \
MinSizeRel. Empty: the manifest's, else auto")

set(EPM_ABI_MATCH "" CACHE STRING
        "Which cached build may be used: compatible, strict or any. Empty: the \
manifest's, else compatible")
set_property(CACHE EPM_ABI_MATCH PROPERTY STRINGS compatible strict any)

set(EPM_DIR "${CMAKE_BINARY_DIR}/epm")

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

# Writes ${EPM_DIR}/env.json, once per configure, as late as it can: on the
# first epm_add_package or the provider's first call, so a CMAKE_CXX_STANDARD
# set after the include is still seen.
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
    epm_json_string_array(config_types ${CMAKE_CONFIGURATION_TYPES})
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
            "osxSysroot=CMAKE_OSX_SYSROOT"
            "cxxStandard=CMAKE_CXX_STANDARD"
            "toolchainFile=CMAKE_TOOLCHAIN_FILE")
        string(REPLACE "=" ";" pair "${pair}")
        list(GET pair 0 key)
        list(GET pair 1 var)
        epm_json_set_string(json ${key} "${${var}}")
    endforeach ()

    # CMake's own Android support sets CMAKE_ANDROID_ARCH_ABI; the NDK's
    # legacy toolchain file only ANDROID_ABI.
    set(android_abi "${CMAKE_ANDROID_ARCH_ABI}")

    if (NOT android_abi)
        set(android_abi "${ANDROID_ABI}")
    endif ()

    epm_json_set_string(json androidAbi "${android_abi}")

    file(WRITE "${EPM_DIR}/env.json" "${json}\n")
    set_property(GLOBAL PROPERTY EPM_ENV_WRITTEN TRUE)
endfunction()

# Runs the epm binary with `arguments` plus --cache and --verbose as asked for.
# Its stdout goes straight to the console as it is written: without --verbose
# that is only the "-- epm: building ..." progress of a dependency built into
# the cache. stderr is kept for the failure message (and echoed with
# EPM_VERBOSE). Fails the configure with `what` and stderr on a non-zero exit.
function(epm_run_binary what)
    set(command "${EPM_EXECUTABLE}" ${ARGN})

    if (EPM_CACHE)
        list(APPEND command --cache "${EPM_CACHE}")
    endif ()

    set(echo_args "")

    if (EPM_VERBOSE)
        list(APPEND command --verbose)
        set(echo_args ECHO_ERROR_VARIABLE)
    endif ()

    # The project's directory, for paths given relative to it (a profile file).
    execute_process(COMMAND ${command}
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            RESULT_VARIABLE result
            ERROR_VARIABLE error
            ${echo_args})

    if (NOT result EQUAL 0)
        string(STRIP "${error}" error)
        message(FATAL_ERROR "epm: ${what} failed (exit ${result}): ${error}")
    endif ()
endfunction()

# The provider's PREPARE step: env.json, overrides.json, then `epm configure`
# over the manifest, which writes graph.cmake and graph.json.
function(epm_configure_graph)
    get_property(manifest GLOBAL PROPERTY EPM_MANIFEST)
    set(graph_file "${EPM_DIR}/graph.cmake")

    epm_write_env()
    epm_write_overrides("${EPM_DIR}/overrides.json")
    file(REMOVE "${graph_file}")

    epm_run_binary("epm configure"
            configure
            --manifest "${manifest}"
            --env "${EPM_DIR}/env.json"
            --overrides "${EPM_DIR}/overrides.json"
            --out "${EPM_DIR}")

    if (NOT EXISTS "${graph_file}")
        message(FATAL_ERROR "epm: epm configure succeeded but wrote no "
                "${graph_file}")
    endif ()
endfunction()

# The manifest flow, from get_epm.cmake while the first project() processes
# CMAKE_PROJECT_TOP_LEVEL_INCLUDES: the graph is resolved at the provider's
# first call.
function(epm_use_manifest manifest)
    set_property(GLOBAL PROPERTY EPM_MANIFEST "${manifest}")
    epm_provider_register("${EPM_DIR}/graph.cmake" PREPARE epm_configure_graph)
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

    if ("${EPM_BUILD_MISSING_BINARIES}" STREQUAL "" OR EPM_BUILD_MISSING_BINARIES)
        set(build_missing true)
    else ()
        set(build_missing false)
    endif ()

    set(options_json "{}")
    set(options "")

    foreach (option IN LISTS EPM_ARGS_OPTIONS)
        epm_parse_option("${option}")
        epm_json_set_string(options_json "${EPM_OPTION_KEY}" "${EPM_OPTION_VALUE}")
        list(APPEND options "${EPM_OPTION_KEY}=${EPM_OPTION_VALUE}")
    endforeach ()

    get_property(is_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
    epm_resolve_configs("${EPM_CONFIGS}" "${is_multi_config}" "${CMAKE_BUILD_TYPE}"
            configs configs_error)

    if (configs_error)
        message(FATAL_ERROR "epm_add_package(${name}): ${configs_error}")
    endif ()

    epm_json_string_array(configs_json ${configs})

    set(abi_match "${EPM_ABI_MATCH}")

    if (abi_match STREQUAL "")
        set(abi_match "compatible")
    endif ()

    if (NOT abi_match MATCHES "^(compatible|strict|any)$")
        message(FATAL_ERROR
                "epm_add_package(${name}): EPM_ABI_MATCH must be compatible, "
                "strict or any, got '${abi_match}'")
    endif ()

    set(request "{}")
    string(JSON request SET "${request}" schema 2)
    epm_json_set_string(request name "${name}")
    epm_json_set_string(request gitUrl "${git_url}")
    epm_json_set_string(request gitRef "${EPM_ARGS_GIT_TAG}")
    epm_json_set_string(request mode "${mode}")
    string(JSON request SET "${request}" options "${options_json}")
    string(JSON request SET "${request}" buildMissingBinaries ${build_missing})
    string(JSON request SET "${request}" configs "${configs_json}")
    epm_json_set_string(request abiMatch "${abi_match}")

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

    epm_run_binary("resolving ${name}"
            resolve
            --request "${request_file}"
            --env "${EPM_DIR}/env.json"
            --response "${response_file}")

    if (NOT EXISTS "${response_file}")
        message(FATAL_ERROR
                "epm: resolving ${name} succeeded but wrote no response file "
                "(${response_file})")
    endif ()

    include("${response_file}")

    epm_load_package("${name}"
            MODE "${epm_${name}_MODE}"
            SOURCE_DIR "${epm_${name}_SOURCE_DIR}"
            TARGETS_FILE "${epm_${name}_TARGETS_FILE}"
            PREFIX "${epm_${name}_PREFIX}"
            PLATFORM "${epm_${name}_PLATFORM}"
            NOTE "${epm_${name}_NOTE}"
            OPTIONS ${options}
            ORIGIN "${response_file}")

    set_property(GLOBAL PROPERTY EPM_PACKAGE_${name}_REQUEST "${request}")
    get_property(resolved_mode GLOBAL PROPERTY EPM_PACKAGE_${name}_MODE)
    get_property(source_dir GLOBAL PROPERTY EPM_PACKAGE_${name}_SOURCE_DIR)
    get_property(binary_dir GLOBAL PROPERTY EPM_PACKAGE_${name}_BINARY_DIR)

    set(${name}_SOURCE_DIR "${source_dir}" PARENT_SCOPE)
    set(${name}_MODE "${resolved_mode}" PARENT_SCOPE)
    set(${name}_ADDED YES PARENT_SCOPE)

    if (resolved_mode STREQUAL "source")
        set(${name}_BINARY_DIR "${binary_dir}" PARENT_SCOPE)
    endif ()
endfunction()
