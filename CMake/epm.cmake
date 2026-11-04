# epm.cmake -- the module that drives the epm binary from CMake.
#
# Projects do not include this file directly: they list get_epm.cmake in
# CMAKE_PROJECT_TOP_LEVEL_INCLUDES (or include it), and it finds (or downloads,
# or builds) an epm binary, sets EPM_EXECUTABLE and then includes the epm.cmake
# installed with that binary, so the two always match. epm-provider.cmake must
# sit beside this file. The binary writes both into every child build it runs
# (EPM_NESTED), so a dependency's own package calls go through the same lock.
#
# Every package is pinned in the lock (EPM_LOCK_FILE, default
# ${CMAKE_SOURCE_DIR}/epm-lock.json) to a commit and a Merkle hash of its files,
# whichever way it is declared, and epm never builds what the lock cannot
# satisfy (Docs/Resolution.md):
#
# 1. A manifest, epm.json beside the top-level CMakeLists.txt (Docs/Graph.md).
#    The dependency provider's first call writes env.json and overrides.json,
#    runs `epm configure` for the whole graph and reads the graph.cmake it
#    writes; find_package(<name>) then loads each package.
#
# 2. epm_add_package, CPMAddPackage, CPMFindPackage, CPMDeclarePackage,
#    CPMGetPackage and (through the provider) FetchContent_MakeAvailable, one
#    package per call. epm_add_package takes CPMAddPackage's arguments plus
#    MODE (auto, prebuilt, binary-only or source; the CPM commands are always
#    source). A name the graph holds is loaded from the graph instead.
#
# Either way <name>_SOURCE_DIR, <name>_ADDED and <name>_MODE are set in the
# caller's scope (plus <name>_BINARY_DIR in source mode).
#
# Variables read:
#   EPM_EXECUTABLE              the epm binary (required; set by get_epm.cmake)
#   EPM_LOCK_FILE               the lock (default ${CMAKE_SOURCE_DIR}/epm-lock.json)
#   EPM_LOCK_MODE               locked (default; writes a missing lock), update
#                               (re-resolve every ref and rewrite the lock) or
#                               offline (the cache only, never the network)
#   EPM_MANIFEST_FILE           the root's epm.json, whose "overrides" replace
#                               every requirement on a package (default
#                               ${CMAKE_SOURCE_DIR}/epm.json; a dependency's
#                               own epm.json overrides nothing)
#   EPM_CI                      ON refuses <Name>_SOURCE overrides and a missing
#                               lock in locked mode
#   EPM_BUILD_MISSING_BINARIES  ON (the default when unset): `auto` builds a
#                               missing binary entry; OFF: `auto` falls back to
#                               source instead
#   EPM_CACHE                   cache root handed to the binary as --cache
#   EPM_VERBOSE                 pass --verbose and echo the binary's output
#   EPM_CONFIGS                 auto (the default): a single-config generator
#                               gets CMAKE_BUILD_TYPE (empty, RelWithDebInfo and
#                               MinSizeRel mean Release), a multi-config one
#                               Debug;Release. Or a list of Debug, Release,
#                               RelWithDebInfo, MinSizeRel (the last two are
#                               built as Release). A cache entry gains configs
#                               as they are asked for; it never rebuilds one.
#   EPM_ABI_MATCH               which cached build may be used: compatible
#                               (default), strict or any (Docs/PoC.md)
#   EPM_<Name>_SOURCE, CPM_<Name>_SOURCE   a local checkout to use instead
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
#   session.json                the packages this configure has added
#   add.cmake                   written by the binary for each package call

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
        # EPM_LOCK_MODE is the lock's mode, not a package's.
        if (NOT var MATCHES "^EPM_(.+)_(MODE|SOURCE|CONFIGS|OPTIONS)$"
                OR var STREQUAL "EPM_LOCK_MODE")
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

                # A Windows home's backslashes would be escapes in the replacement.
                file(TO_CMAKE_PATH "${home}" home)
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
            "toolchainFile=CMAKE_TOOLCHAIN_FILE"
            "prefixPath=CMAKE_PREFIX_PATH")
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
    set(echo_args "")

    if (EPM_VERBOSE)
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
            configure ${_EPM_OPTIONS}
            "--env=${EPM_DIR}/env.json"
            "--overrides=${EPM_DIR}/overrides.json"
            "--out=${EPM_DIR}")

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

set(EPM_LOCK_MODE locked CACHE STRING "locked, update or offline")
option(EPM_CI "Refuse <Name>_SOURCE overrides and a missing lock in locked mode" OFF)

if (NOT EPM_LOCK_FILE)
    set(EPM_LOCK_FILE "${CMAKE_SOURCE_DIR}/epm-lock.json")
endif ()

if (NOT EPM_MANIFEST_FILE)
    set(EPM_MANIFEST_FILE "${CMAKE_SOURCE_DIR}/epm.json")
endif ()

set(_EPM_OPTIONS
        "--cmake=${CMAKE_COMMAND}"
        "--source-dir=${CMAKE_SOURCE_DIR}" "--build-dir=${CMAKE_BINARY_DIR}"
        "--lock=${EPM_LOCK_FILE}" "--mode=${EPM_LOCK_MODE}" "--ci=${EPM_CI}"
        "--manifest=${EPM_MANIFEST_FILE}")

if (EPM_CACHE)
    list(APPEND _EPM_OPTIONS "--cache=${EPM_CACHE}")
endif ()

if (NOT "${EPM_BUILD_MISSING_BINARIES}" STREQUAL "")
    list(APPEND _EPM_OPTIONS
            "--build-missing-binaries=${EPM_BUILD_MISSING_BINARIES}")
endif ()

if (EPM_ROOT_NAME)
    list(APPEND _EPM_OPTIONS "--root-name=${EPM_ROOT_NAME}")
endif ()

if (EPM_VERBOSE)
    list(APPEND _EPM_OPTIONS --verbose)
endif ()

# The next build reconfigures when the lock or epm.json is edited, pulled or
# deleted, so a missing lock is written again and a changed one is checked.
set_property(DIRECTORY "${CMAKE_SOURCE_DIR}" APPEND PROPERTY
        CMAKE_CONFIGURE_DEPENDS "${EPM_LOCK_FILE}" "${EPM_MANIFEST_FILE}")

macro(_epm_report)
    string(STRIP "${_epm_error}" _epm_error)

    if (_epm_error)
        message("${_epm_error}")
    endif ()

    if (NOT _epm_result EQUAL 0)
        message(FATAL_ERROR "epm stopped the configure; its message is above.")
    endif ()
endmacro()

# The overrides and whether this configure writes a new lock, for every
# later call of this configure.
get_cmake_property(_epm_variables VARIABLES)
list(FILTER _epm_variables INCLUDE REGEX "^(EPM|CPM)_.+_SOURCE$")
set(_epm_overrides)

foreach (variable IN LISTS _epm_variables)
    list(APPEND _epm_overrides "--override=${variable}=${${variable}}")
endforeach ()

execute_process(COMMAND "${EPM_EXECUTABLE}" lock begin ${_EPM_OPTIONS} ${_epm_overrides}
        RESULT_VARIABLE _epm_result ERROR_VARIABLE _epm_error)
_epm_report()

# The configs and ABI policy of a package call, as epm_add_package always took
# them: EPM_CONFIGS through epm_resolve_configs, EPM_ABI_MATCH or compatible.
function(epm_package_call_options out_var)
    get_property(is_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
    epm_resolve_configs("${EPM_CONFIGS}" "${is_multi_config}" "${CMAKE_BUILD_TYPE}"
            configs configs_error)

    if (configs_error)
        message(FATAL_ERROR "epm: ${configs_error}")
    endif ()

    set(abi_match "${EPM_ABI_MATCH}")

    if (abi_match STREQUAL "")
        set(abi_match "compatible")
    endif ()

    string(REPLACE ";" "," configs "${configs}")
    set(${out_var} "--configs=${configs}" "--abi-match=${abi_match}" PARENT_SCOPE)
endfunction()

# Pasted into each command, so the arguments expand once and "a\;b" stays whole.
# The graph is read first, so a name it holds is loaded from it.
set(_epm_run [=[
    epm_set_policies()
    epm_write_env()
    epm_provider_read_graph()
    epm_package_call_options(_epm_call_options)
    execute_process(COMMAND "${EPM_EXECUTABLE}" ${_epm_subcommand} ${_EPM_OPTIONS}
            ${_epm_call_options}
            "--env=${EPM_DIR}/env.json" "--default-mode=${_epm_default_mode}"
            "--graph-packages=${epm_PACKAGES}"
            "--requested-by=${CMAKE_CURRENT_SOURCE_DIR}"
            "--requested-by-file=${_epm_requested_by_file}"
            "--requested-binary-dir=${CMAKE_CURRENT_BINARY_DIR}"
            "--output=${EPM_DIR}/add.cmake" -- ${_epm_arguments}
            RESULT_VARIABLE _epm_result ERROR_VARIABLE _epm_error)
    _epm_report()
    include("${EPM_DIR}/add.cmake")]=])

# A function, as CPMAddPackage is: OPTIONS stay local to the add_subdirectory
# they configure, and the results are handed to the caller.
function(epm_add_package)
    set(_epm_requested_by_file "${CMAKE_CURRENT_LIST_FILE}")
    set(_epm_subcommand resolve)
    set(_epm_default_mode auto)
    set(_epm_arguments "${ARGN}")
    cmake_language(EVAL CODE "${_epm_run}")
endfunction()

# The CPM commands go through epm as well, always as source: a dependency that
# calls CPMAddPackage expects its targets add_subdirectory'd. CPMFindPackage
# always fetches, never find_package(), so the build stays hermetic.
foreach (command CPMAddPackage CPMFindPackage)
    cmake_language(EVAL CODE "function(${command})
        set(_epm_requested_by_file \"\${CMAKE_CURRENT_LIST_FILE}\")
        set(_epm_subcommand resolve)
        set(_epm_default_mode source)
        set(_epm_arguments \"\${ARGN}\")
        cmake_language(EVAL CODE \"\${_epm_run}\")
    endfunction()")
endforeach ()

function(CPMDeclarePackage name)
    set_property(GLOBAL PROPERTY EPM_DECLARED_${name} "${ARGN}")
endfunction()

function(CPMGetPackage name)
    get_property(declared GLOBAL PROPERTY EPM_DECLARED_${name})
    set(_epm_requested_by_file "${CMAKE_CURRENT_LIST_FILE}")
    set(_epm_subcommand resolve)
    set(_epm_default_mode source)
    set(_epm_arguments NAME ${name} ${declared})
    cmake_language(EVAL CODE "${_epm_run}")
endfunction()

function(CPMUsePackageLock)
endfunction()

# include(CPM) finds this stub; a dependency's CPM.cmake returns when
# CPM_INITIALIZED is set, before it defines anything.
file(WRITE "${EPM_DIR}/cpm/CPM.cmake" "# epm defines CPMAddPackage\n")
list(PREPEND CMAKE_MODULE_PATH "${EPM_DIR}/cpm")
set(CPM_DIRECTORY "${EPM_DIR}/cpm" CACHE INTERNAL "")
set(CPM_VERSION 999999.0.0)
set(CPM_FILE "${EPM_DIR}/cpm/CPM.cmake")
set(CPM_DOWNLOAD_LOCATION "${EPM_DIR}/cpm/CPM.cmake")
set_property(GLOBAL PROPERTY CPM_INITIALIZED true)

# FetchContent_MakeAvailable of a name the graph does not hold, which the
# provider hands here.
macro(epm_provide_through_the_lock name)
    set(_epm_requested_by_file "${CMAKE_CURRENT_LIST_FILE}")
    set(_epm_subcommand provide)
    set(_epm_default_mode source)
    set(_epm_arguments NAME ${name} ${ARGN})
    cmake_language(EVAL CODE "${_epm_run}")
endmacro()

# A dependency provider can only be set while the first project() runs its
# CMAKE_PROJECT_TOP_LEVEL_INCLUDES; included any later, find_package and
# FetchContent keep searching and fetching for themselves.
set(_epm_loaded_during_project OFF)

foreach (include IN LISTS CMAKE_PROJECT_TOP_LEVEL_INCLUDES)
    get_filename_component(include "${include}" REALPATH)
    get_filename_component(parent "${CMAKE_PARENT_LIST_FILE}" REALPATH)
    get_filename_component(current "${CMAKE_CURRENT_LIST_FILE}" REALPATH)

    if (include STREQUAL parent OR include STREQUAL current)
        set(_epm_loaded_during_project ON)
    endif ()
endforeach ()

if (_epm_loaded_during_project)
    if (EPM_NESTED AND EPM_GRAPH)
        epm_provider_register("${EPM_GRAPH}")
    elseif (NOT EPM_NESTED AND EXISTS "${EPM_MANIFEST_FILE}")
        epm_use_manifest("${EPM_MANIFEST_FILE}")
    else ()
        epm_provider_register("")
    endif ()
endif ()

cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL
        execute_process COMMAND "${EPM_EXECUTABLE}" lock root ${_EPM_OPTIONS} --announce)
