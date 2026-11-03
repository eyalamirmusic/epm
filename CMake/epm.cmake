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
#       GITHUB_REPOSITORY eyalamirmusic/eacp      # or GIT_REPOSITORY, URL, ...
#       GIT_TAG main                              # resolved to a commit by epm
#       MODE auto                                 # auto | prebuilt | source
#       OPTIONS "EACP_BUILD_GRAPHICS OFF" "EACP_ENABLE_TESTS OFF")
#
# epm_add_package takes CPMAddPackage's arguments, plus MODE. Every call hands
# them to the epm binary (`epm resolve`), which fetches the source and, unless
# the mode is `source`, builds or reuses cached binaries. It answers with the
# CMake to run, which either includes the cached imported targets (prebuilt)
# or sets the OPTIONS and add_subdirectory()s the source, as CPM would. Either
# way <name>_SOURCE_DIR, <name>_BINARY_DIR, <name>_ADDED and <name>_MODE are
# set in the caller's scope.
#
# The lock (opt-in): set EPM_LOCK_FILE before this file is included, usually
#   set(EPM_LOCK_FILE "${CMAKE_SOURCE_DIR}/epm.lock")
# Then every package is pinned in it to a commit and a Merkle hash of its
# files, and CPMAddPackage, CPMFindPackage, CPMDeclarePackage, CPMGetPackage
# and (when this file is loaded through CMAKE_PROJECT_TOP_LEVEL_INCLUDES)
# FetchContent_MakeAvailable go through epm too, so the packages dependencies
# ask for are in the same lock.
#
# Variables read:
#   EPM_EXECUTABLE              the epm binary (required; set by get_epm.cmake)
#   EPM_BUILD_MISSING_BINARIES  ON (default): `auto` builds a missing binary
#                               entry; OFF: `auto` falls back to source instead
#   EPM_CACHE                   cache root handed to the binary as --cache
#   EPM_VERBOSE                 pass --verbose and echo the binary's output
#   EPM_LOCK_FILE               the lock; unset (default) means no lock
#   EPM_LOCK_MODE               locked (default; writes a missing lock), update
#                               (re-resolve every ref and rewrite the lock) or
#                               offline (the cache only, never the network)
#   EPM_CI                      ON refuses <Name>_SOURCE overrides in locked mode
#   EPM_<Name>_SOURCE, CPM_<Name>_SOURCE   a local checkout to use instead
#
# Files exchanged with the binary live under ${CMAKE_BINARY_DIR}/epm/:
#   env.json                    toolchain description, written once per configure
#   session.json                the packages this configure has added
#   add.cmake                   written by the binary for each call, included here

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
set(_EPM_MODULE_FILE "${CMAKE_CURRENT_LIST_FILE}")

set(EPM_LOCK_MODE locked CACHE STRING "locked, update or offline")
option(EPM_CI "Refuse <Name>_SOURCE overrides in locked mode" OFF)

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
            "toolchainFile=CMAKE_TOOLCHAIN_FILE"
            "module=_EPM_MODULE_FILE")
        string(REPLACE "=" ";" pair "${pair}")
        list(GET pair 0 key)
        list(GET pair 1 var)
        epm_json_set_string(json ${key} "${${var}}")
    endforeach ()

    file(WRITE "${EPM_DIR}/env.json" "${json}\n")
    set_property(GLOBAL PROPERTY EPM_ENV_WRITTEN TRUE)
endfunction()


set(_EPM_OPTIONS
        "--cmake=${CMAKE_COMMAND}"
        "--source-dir=${CMAKE_SOURCE_DIR}" "--build-dir=${CMAKE_BINARY_DIR}"
        "--build-missing-binaries=${EPM_BUILD_MISSING_BINARIES}")

if (EPM_CACHE)
    list(APPEND _EPM_OPTIONS "--cache=${EPM_CACHE}")
endif ()

if (EPM_LOCK_FILE)
    list(APPEND _EPM_OPTIONS "--lock=${EPM_LOCK_FILE}" "--mode=${EPM_LOCK_MODE}"
            "--ci=${EPM_CI}")
endif ()

if (EPM_VERBOSE)
    list(APPEND _EPM_OPTIONS --verbose)
endif ()

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

# Pasted into each command, so the arguments expand once and "a\;b" stays whole.
set(_epm_run [=[
    epm_set_policies()
    epm_write_env()
    execute_process(COMMAND "${EPM_EXECUTABLE}" ${_epm_subcommand} ${_EPM_OPTIONS}
            "--env=${EPM_DIR}/env.json" "--default-mode=${_epm_default_mode}"
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

if (NOT EPM_LOCK_FILE)
    return()
endif ()

# With a lock, the CPM commands go through epm as well, always as source: a
# dependency that calls CPMAddPackage expects its targets add_subdirectory'd.
# CPMFindPackage always fetches, never find_package(), so the build stays
# hermetic.
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

macro(_epm_provide method name)
    set(_epm_requested_by_file "${CMAKE_CURRENT_LIST_FILE}")
    set(_epm_subcommand provide)
    set(_epm_default_mode source)
    set(_epm_arguments NAME ${name} ${ARGN})
    cmake_language(EVAL CODE "${_epm_run}")
endmacro()

# A dependency provider can only be set while the first project() runs its
# CMAKE_PROJECT_TOP_LEVEL_INCLUDES; included any later, FetchContent keeps
# fetching for itself.
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
    cmake_language(SET_DEPENDENCY_PROVIDER _epm_provide
            SUPPORTED_METHODS FETCHCONTENT_MAKEAVAILABLE_SERIAL)
endif ()

cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL
        execute_process COMMAND "${EPM_EXECUTABLE}" lock root ${_EPM_OPTIONS} --announce)
