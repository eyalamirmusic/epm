# epm.cmake -- the module that drives the epm binary from CMake.
#
# Projects do not include this file directly: they list get_epm.cmake in
# CMAKE_PROJECT_TOP_LEVEL_INCLUDES (or include it), and it finds (or builds) an
# epm binary, sets EPM_EXECUTABLE and then includes the epm.cmake
# installed with that binary, so the two always match. epm-provider.cmake must
# sit beside this file.
#
# Two flows, while the second replaces the first (Docs/Graph.md):
#
# 1. A manifest, the recipe epm.cpp beside the top-level CMakeLists.txt.
#    get_epm.cmake calls epm_use_manifest, which registers the dependency
#    provider of epm-provider.cmake. The provider's first call writes env.json
#    and overrides.json, runs `epm configure` for the whole graph (the binary
#    compiles and runs each epm.cpp) and reads the graph.cmake it writes;
#    find_package(<name>) then loads each package.
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
# epm_add_recipe(<target> [SOURCE <file>]) builds the project's epm.cpp as an
# executable of its own build, the same standalone program epm builds and runs
# to read the recipe, linked against epm's recipe libraries (epm::recipe, from
# `epm recipe-sdk`), for IDEs, the build's diagnostics and debugging, and adds
# the target <target>-configure running it (see the function below).
#
# Variables read:
#   EPM_EXECUTABLE              the epm binary (required; set by get_epm.cmake)
#   EPM_BUILD_MISSING_BINARIES  ON (the default when unset): `auto` builds a
#                               missing binary entry; OFF: `auto` falls back to
#                               source instead
#   EPM_CACHE                   cache root handed to the binary as --cache
#                               (else the EPM_CACHE environment variable, else the
#                               platform cache directory, as the binary does)
#   EPM_CACHE_STORES            read-only cache stores, a list searched in order
#                               for a binary the cache lacks, each handed to the
#                               binary as --cache-store (else the EPM_CACHE_STORES
#                               environment variable, its paths separated by ':',
#                               ';' on Windows)
#   EPM_VERBOSE                 pass --verbose and echo the binary's output
#   EPM_CONFIGS                 auto (the default): a single-config generator
#                               gets CMAKE_BUILD_TYPE (Debug, Release or
#                               RelWithDebInfo; empty, MinSizeRel and any
#                               other mean Release), a multi-config one
#                               Debug;Release. Or a list of Debug, Release,
#                               RelWithDebInfo, MinSizeRel (built as Release).
#                               A cache entry gains configs as they are asked
#                               for; it never rebuilds one. A build links the
#                               same-named config, else Release, else
#                               RelWithDebInfo; on the MSVC ABI a Debug build
#                               needs a Debug one (the runtimes do not mix),
#                               which the binary checks.
#   EPM_ABI_MATCH               which cached build may be used: compatible
#                               (default: same platform, compiler id and major
#                               version, deployment target, C++ standard,
#                               options, source, toolchain file), strict (every
#                               recorded detail, compiler path and full version
#                               included) or any (platform, options, source)
#   EPM_RECIPE_ALLOW_COMPILER_MISMATCH
#                               ON: epm_add_recipe goes on (with a warning) when
#                               the project's C++ compiler id is not the one
#                               epm and its recipe libraries were built with
#   EPM_FETCH_SOURCES           unset or ON (default): a prebuilt package's
#                               source snapshot is fetched too, so
#                               <name>_SOURCE_DIR names it (CMake modules and
#                               headers some projects read from there); OFF
#                               skips that clone for developers and CI that only
#                               consume prebuilts (SOURCE_DIR is then empty,
#                               unless a package built from source needs it).
#                               EPM_<name>_FETCH_SOURCE (ON/OFF) decides for one
#                               package. Neither changes a fingerprint.
#
# With a manifest, EPM_CONFIGS, EPM_ABI_MATCH and EPM_BUILD_MISSING_BINARIES are
# overrides: unset or empty, the manifest (or the binary's default) decides.
# The manifest flow also reads EPM_MODE, EPM_STRICT and EPM_<name>_MODE,
# _SOURCE, _CONFIGS and _OPTIONS (KEY=VALUE list); see epm_write_overrides.
# EPM_PROFILE (variable, else environment) is no override: it goes into
# env.json, where an epm.cpp may branch on it (env.profile).
#
# On the MSVC ABI (cl, clang-cl) this module defaults CMAKE_MSVC_RUNTIME_LIBRARY
# to the static runtime, epm's own (MultiThreaded$<$<CONFIG:Debug>:Debug>), for
# the project's targets; set it before the include (or with -D) for another.
# Dependencies always follow the project's (env.json's msvcRuntime, part of the
# fingerprint), so they link. epm's own archives, which a recipe executable
# links, have the runtime epm was built with: epm_add_recipe warns when the
# project chose the other kind and compiles the recipe with epm's.
#
# Files exchanged with the binary live under ${CMAKE_BINARY_DIR}/epm/:
#   env.json                    toolchain description, written once per configure
#   overrides.json              the overrides above (manifest flow)
#   graph.cmake, graph.json     written by `epm configure` (manifest flow)
#   requests/<name>.json        one per epm_add_package
#   responses/<name>.cmake      written by `epm resolve`, included here
#   recipe-sdk.cmake            written by `epm recipe-sdk` (epm_add_recipe)

include_guard(GLOBAL)

include("${CMAKE_CURRENT_LIST_DIR}/epm-provider.cmake")

# The configuration a requested name is built as (the binary's buildConfigFor):
# Debug, Release and RelWithDebInfo as they are, MinSizeRel as Release, any
# case; empty for another name.
function(epm_build_config name out_var)
    string(TOLOWER "${name}" lower)

    if (lower STREQUAL "debug")
        set(${out_var} Debug PARENT_SCOPE)
    elseif (lower STREQUAL "relwithdebinfo")
        set(${out_var} RelWithDebInfo PARENT_SCOPE)
    elseif (lower MATCHES "^(release|minsizerel)$")
        set(${out_var} Release PARENT_SCOPE)
    else ()
        set(${out_var} "" PARENT_SCOPE)
    endif ()
endfunction()

# Resolves an EPM_CONFIGS value into the configs to build and cache, each one
# Debug, Release or RelWithDebInfo (epm_build_config), in that order. `auto`
# (or empty) takes `build_type` for a single-config generator, where an empty
# or unknown type means Release, and Debug;Release for a multi-config one.
# Sets `error_var` instead for an unknown config name.
function(epm_resolve_configs spec is_multi_config build_type out_var error_var)
    set(configs "")
    set(error "")
    string(TOLOWER "${spec}" lower_spec)

    if (lower_spec STREQUAL "auto" OR lower_spec STREQUAL "")
        if (is_multi_config)
            set(configs Debug Release)
        else ()
            epm_build_config("${build_type}" built)

            if (built STREQUAL "")
                set(built Release)
            endif ()

            set(configs ${built})
        endif ()
    else ()
        set(wanted "")

        foreach (config IN LISTS spec)
            epm_build_config("${config}" built)

            if (built STREQUAL "")
                set(error "EPM_CONFIGS has an unknown configuration '${config}': "
                        "give auto, or a list of Debug, Release, RelWithDebInfo "
                        "and MinSizeRel")
                break()
            endif ()

            list(APPEND wanted ${built})
        endforeach ()

        foreach (config IN ITEMS Debug Release RelWithDebInfo)
            if (config IN_LIST wanted AND NOT error)
                list(APPEND configs ${config})
            endif ()
        endforeach ()

        if (NOT configs AND NOT error)
            set(error "EPM_CONFIGS lists no configuration")
        endif ()
    endif ()

    set(${out_var} "${configs}" PARENT_SCOPE)
    set(${error_var} "${error}" PARENT_SCOPE)
endfunction()

# The configurations of epm's recipe libraries (`available`, upper case) that a
# build in configuration `config` may link, best first: the same one, then
# Debug for Debug and Release for anything else (RelWithDebInfo, MinSizeRel, a
# custom or empty configuration), then the other of the two, then the rest.
function(epm_recipe_config_preference config available out_var)
    string(TOUPPER "${config}" upper)

    if (upper STREQUAL "DEBUG")
        set(order DEBUG RELEASE)
    else ()
        set(order RELEASE DEBUG)
    endif ()

    set(preference "")

    foreach (candidate IN ITEMS ${upper} ${order} ${available})
        if (candidate IN_LIST available AND NOT candidate IN_LIST preference)
            list(APPEND preference "${candidate}")
        endif ()
    endforeach ()

    set(${out_var} "${preference}" PARENT_SCOPE)
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

# Whether a prebuilt <name>'s source snapshot is fetched for its
# <name>_SOURCE_DIR (epm_add_package's --no-source-dir otherwise):
# EPM_<name>_FETCH_SOURCE, else EPM_FETCH_SOURCES, each a CMake variable or
# else the environment, else ON.
function(epm_fetches_source name out_var)
    foreach (value IN ITEMS "${EPM_${name}_FETCH_SOURCE}"
            "$ENV{EPM_${name}_FETCH_SOURCE}" "${EPM_FETCH_SOURCES}"
            "$ENV{EPM_FETCH_SOURCES}")
        if (NOT value STREQUAL "")
            if (value)
                set(${out_var} ON PARENT_SCOPE)
            else ()
                set(${out_var} OFF PARENT_SCOPE)
            endif ()

            return()
        endif ()
    endforeach ()

    set(${out_var} ON PARENT_SCOPE)
endfunction()

# Writes overrides.json (Docs/Graph.md §3) from the EPM_* variables visible
# here, normal or cache, a normal one winning as ${} has it. Empty values are
# left out; EPM_<name>_SOURCE is made absolute against CMAKE_SOURCE_DIR.
# buildMissingBinaries (EPM_BUILD_MISSING_BINARIES) is written only when set.
function(epm_write_overrides file)
    set(json "{}")
    string(JSON json SET "${json}" schema 1)
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

    if (NOT "${EPM_FETCH_SOURCES}" STREQUAL "")
        if (EPM_FETCH_SOURCES)
            string(JSON json SET "${json}" fetchSources true)
        else ()
            string(JSON json SET "${json}" fetchSources false)
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
        # FETCH_SOURCE first: EPM_<name>_FETCH_SOURCE also ends in _SOURCE.
        if (var MATCHES "^EPM_(.+)_(FETCH_SOURCE)$")
        elseif (NOT var MATCHES "^EPM_(.+)_(MODE|SOURCE|CONFIGS|OPTIONS)$")
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
        elseif (field STREQUAL "FETCH_SOURCE")
            if (value)
                string(JSON package SET "${package}" fetchSource true)
            else ()
                string(JSON package SET "${package}" fetchSource false)
            endif ()
        elseif (field STREQUAL "SOURCE")
            if (value MATCHES "^~([/\\]|$)")
                set(home "$ENV{HOME}")

                if (home STREQUAL "" AND CMAKE_HOST_WIN32)
                    set(home "$ENV{USERPROFILE}")
                endif ()

                # Not REGEX REPLACE: a Windows home's backslashes would be
                # escapes in its replacement.
                string(SUBSTRING "${value}" 1 -1 rest)
                set(value "${home}${rest}")
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

# env.json's osxArchitectures: CMAKE_OSX_ARCHITECTURES, else, for an Apple
# device or simulator SDK, which leaves CMAKE_SYSTEM_PROCESSOR empty, what it
# builds for by default: Xcode's ARCHS_STANDARD (CMAKE_XCODE_ARCHS, arm64;x86_64
# for a simulator), else the compiler's default target. The platform name and
# the fingerprint then carry the architecture, and child builds get it.
function(epm_osx_architectures out_var)
    set(architectures "${CMAKE_OSX_ARCHITECTURES}")

    if (architectures STREQUAL ""
            AND CMAKE_SYSTEM_NAME MATCHES "^(iOS|tvOS|watchOS|visionOS)$")
        foreach (variable IN ITEMS CMAKE_XCODE_ARCHS
                CMAKE_CXX_COMPILER_ARCHITECTURE_ID CMAKE_C_COMPILER_ARCHITECTURE_ID)
            if (architectures STREQUAL "")
                set(architectures "${${variable}}")
            endif ()
        endforeach ()
    endif ()

    set(${out_var} "${architectures}" PARENT_SCOPE)
endfunction()

# env.json's osxDeploymentTarget, the effective one: CMAKE_OSX_DEPLOYMENT_TARGET,
# else, for an Apple device or simulator SDK, the Xcode attribute a project may
# set instead (CMAKE_XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET, or TVOS_,
# WATCHOS_, XROS_), else the environment's MACOSX_DEPLOYMENT_TARGET (IPHONEOS_,
# TVOS_, WATCHOS_, XROS_), which clang reads when it is given no target. The
# fingerprint records it, and child builds get it as an explicit
# CMAKE_OSX_DEPLOYMENT_TARGET, so their environment cannot change it.
function(epm_osx_deployment_target out_var)
    set(target "${CMAKE_OSX_DEPLOYMENT_TARGET}")

    foreach (pair IN ITEMS Darwin=MACOSX iOS=IPHONEOS tvOS=TVOS watchOS=WATCHOS
            visionOS=XROS)
        string(REPLACE "=" ";" pair "${pair}")
        list(GET pair 0 system)
        list(GET pair 1 sdk)

        if (NOT CMAKE_SYSTEM_NAME STREQUAL system)
            continue()
        endif ()

        if (target STREQUAL "" AND NOT system STREQUAL "Darwin")
            set(target "${CMAKE_XCODE_ATTRIBUTE_${sdk}_DEPLOYMENT_TARGET}")
        endif ()

        if (target STREQUAL "")
            set(target "$ENV{${sdk}_DEPLOYMENT_TARGET}")
        endif ()
    endforeach ()

    set(${out_var} "${target}" PARENT_SCOPE)
endfunction()

# The Apple SDK directory the project compiles against: the one CMake's Darwin
# platform module resolved (_CMAKE_OSX_SYSROOT_PATH, also when the compiler's
# default SDK is used), else CMAKE_OSX_SYSROOT when that is a path, else what
# xcrun names for that SDK (or for none: the default, which SDKROOT sets).
function(epm_apple_sdk_path out_var)
    set(path "${_CMAKE_OSX_SYSROOT_PATH}")

    if (path STREQUAL "" AND "${CMAKE_OSX_SYSROOT}" MATCHES "/")
        set(path "${CMAKE_OSX_SYSROOT}")
    endif ()

    if (path STREQUAL "")
        set(sdk_arguments "")

        if (NOT "${CMAKE_OSX_SYSROOT}" STREQUAL "")
            set(sdk_arguments --sdk "${CMAKE_OSX_SYSROOT}")
        endif ()

        execute_process(COMMAND xcrun ${sdk_arguments} --show-sdk-path
                OUTPUT_VARIABLE path
                OUTPUT_STRIP_TRAILING_WHITESPACE
                RESULT_VARIABLE result
                ERROR_QUIET)

        if (NOT result EQUAL 0)
            set(path "")
        endif ()
    endif ()

    set(${out_var} "${path}" PARENT_SCOPE)
endfunction()

# env.json's osxSysrootPath: for an Apple system, the SDK directory
# (epm_apple_sdk_path) when it exists; empty elsewhere. Every Apple child build
# gets it as CMAKE_OSX_SYSROOT, so it compiles against the SDK that sdkVersion
# names, not whatever its own SDKROOT or xcode-select would pick.
function(epm_osx_sysroot_path out_var)
    set(path "")

    if (CMAKE_SYSTEM_NAME MATCHES "^(Darwin|iOS|tvOS|watchOS|visionOS)$")
        epm_apple_sdk_path(path)

        if (NOT IS_DIRECTORY "${path}")
            set(path "")
        endif ()
    endif ()

    set(${out_var} "${path}" PARENT_SCOPE)
endfunction()

# env.json's sdkVersion, part of the fingerprint: for an Apple system, the
# Version of the SDK's SDKSettings.json (epm_osx_sysroot_path); for Windows, the
# Visual Studio generator's CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION, else a
# developer prompt's WindowsSDKVersion. Empty elsewhere or when not found.
function(epm_sdk_version out_var)
    set(version "")

    if (CMAKE_SYSTEM_NAME MATCHES "^(Darwin|iOS|tvOS|watchOS|visionOS)$")
        epm_osx_sysroot_path(sdk)
        set(settings_file "${sdk}/SDKSettings.json")

        if (NOT sdk STREQUAL "" AND EXISTS "${settings_file}")
            file(READ "${settings_file}" settings)
            string(JSON version ERROR_VARIABLE error GET "${settings}" Version)

            if (error)
                set(version "")
            endif ()
        endif ()
    elseif (CMAKE_SYSTEM_NAME STREQUAL "Windows")
        set(version "${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}")

        if (version STREQUAL "")
            string(REGEX REPLACE "[/\\]+$" "" version "$ENV{WindowsSDKVersion}")
        endif ()
    endif ()

    set(${out_var} "${version}" PARENT_SCOPE)
endfunction()

# The compiler is cl or clang-cl (CMAKE_CXX_COMPILER_FRONTEND_VARIANT): the MSVC
# ABI, with its runtime library rules.
function(epm_is_msvc_abi out_var)
    if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC"
            OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        set(${out_var} TRUE PARENT_SCOPE)
    else ()
        set(${out_var} FALSE PARENT_SCOPE)
    endif ()
endfunction()

# The C driver beside a C++ compiler driver: clang for clang++, gcc for g++, cc
# for c++, icx for icpx, emcc for em++, keeping any prefix and suffix
# (x86_64-linux-gnu-g++-13 gives x86_64-linux-gnu-gcc-13, clang++.exe
# clang.exe). The compiler's symlinks are followed one at a time and the last
# link with a driver beside it wins, so a /usr/bin/c++ that an alternatives link
# turns into g++-13 gives gcc-13, not whatever /usr/bin/cc points at. Empty when
# no link has one.
function(epm_c_driver_beside cxx_compiler out_var)
    set(found "")
    set(path "${cxx_compiler}")

    foreach (link RANGE 15)
        get_filename_component(directory "${path}" DIRECTORY)
        get_filename_component(name "${path}" NAME)

        foreach (pair IN ITEMS "clang\\+\\+=clang" "g\\+\\+=gcc" "c\\+\\+=cc"
                "icpx=icx" "em\\+\\+=emcc")
            string(REPLACE "=" ";" pair "${pair}")
            list(GET pair 0 pattern)
            list(GET pair 1 driver)

            if (name MATCHES "^(.*)${pattern}(.*)$")
                set(candidate "${directory}/${CMAKE_MATCH_1}${driver}${CMAKE_MATCH_2}")

                if (EXISTS "${candidate}" AND NOT IS_DIRECTORY "${candidate}")
                    set(found "${candidate}")
                endif ()

                break()
            endif ()
        endforeach ()

        if (NOT IS_SYMLINK "${path}")
            break()
        endif ()

        file(READ_SYMLINK "${path}" target)

        if (NOT IS_ABSOLUTE "${target}")
            set(target "${directory}/${target}")
        endif ()

        set(path "${target}")
    endforeach ()

    set(${out_var} "${found}" PARENT_SCOPE)
endfunction()

# env.json's cCompiler, the C compiler of every child build: the project's
# CMAKE_C_COMPILER; for a project that enabled no C, on the MSVC ABI the C++
# compiler (cl and clang-cl compile both, and CMake refuses a build mixing
# clang-cl with the cl it would pick for C), elsewhere the C driver beside the
# C++ one (epm_c_driver_beside). A child build given none would take CC or
# CMake's default, which the fingerprint does not record. Empty when there is
# no C++ compiler or no driver beside it.
function(epm_c_compiler out_var)
    set(compiler "${CMAKE_C_COMPILER}")
    epm_is_msvc_abi(msvc_abi)

    if (compiler STREQUAL "" AND msvc_abi)
        set(compiler "${CMAKE_CXX_COMPILER}")
    elseif (compiler STREQUAL "" AND NOT "${CMAKE_CXX_COMPILER}" STREQUAL "")
        epm_c_driver_beside("${CMAKE_CXX_COMPILER}" compiler)
    endif ()

    set(${out_var} "${compiler}" PARENT_SCOPE)
endfunction()

# env.json's generatorPlatform: CMAKE_GENERATOR_PLATFORM, else the platform a
# Visual Studio generator picked by default (CMAKE_VS_PLATFORM_NAME); empty
# under other generators.
function(epm_generator_platform out_var)
    set(platform "${CMAKE_GENERATOR_PLATFORM}")

    if (platform STREQUAL "" AND CMAKE_GENERATOR MATCHES "^Visual Studio")
        set(platform "${CMAKE_VS_PLATFORM_NAME}")
    endif ()

    set(${out_var} "${platform}" PARENT_SCOPE)
endfunction()

# env.json's msvcRuntime: the runtime library the project's targets get on the
# MSVC ABI, CMAKE_MSVC_RUNTIME_LIBRARY, or CMake's own default when that is
# empty; empty elsewhere.
function(epm_msvc_runtime out_var)
    epm_is_msvc_abi(msvc_abi)

    if (NOT msvc_abi)
        set(${out_var} "" PARENT_SCOPE)
    elseif ("${CMAKE_MSVC_RUNTIME_LIBRARY}" STREQUAL "")
        set(${out_var} "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL" PARENT_SCOPE)
    else ()
        set(${out_var} "${CMAKE_MSVC_RUNTIME_LIBRARY}" PARENT_SCOPE)
    endif ()
endfunction()

# "dynamic" for a DLL runtime library, "static" for the other kind.
function(epm_msvc_runtime_kind runtime out_var)
    if (runtime MATCHES "DLL")
        set(${out_var} dynamic PARENT_SCOPE)
    else ()
        set(${out_var} static PARENT_SCOPE)
    endif ()
endfunction()

# Tests/EpmConfigsAuto.cmake, Tests/EpmOverrides.cmake and Tests/EpmEnv.cmake
# want the functions above only.
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

# The static runtime library on the MSVC ABI, unless the project chose one: in
# the scope that included this module, before any of its targets (a top-level
# include runs inside project(), before the compiler is known, so this is set
# everywhere and read only by cl and clang-cl). The policy default carries it
# into a subdirectory whose cmake_minimum_required predates CMP0091.
if (NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif ()

if (NOT DEFINED CMAKE_POLICY_DEFAULT_CMP0091)
    set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)
endif ()

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
            "generatorToolset=CMAKE_GENERATOR_TOOLSET"
            "generatorInstance=CMAKE_GENERATOR_INSTANCE"
            "makeProgram=CMAKE_MAKE_PROGRAM")
        string(REPLACE "=" ";" pair "${pair}")
        list(GET pair 0 key)
        list(GET pair 1 var)
        epm_json_set_string(json ${key} "${${var}}")
    endforeach ()

    epm_generator_platform(generator_platform)
    epm_json_set_string(json generatorPlatform "${generator_platform}")

    string(JSON json SET "${json}" isMultiConfig ${is_multi_config})
    epm_json_set_string(json buildType "${CMAKE_BUILD_TYPE}")
    epm_json_string_array(config_types ${CMAKE_CONFIGURATION_TYPES})
    string(JSON json SET "${json}" configurationTypes "${config_types}")

    epm_c_compiler(c_compiler)
    epm_json_set_string(json cCompiler "${c_compiler}")

    foreach (pair IN ITEMS
            "cCompilerId=CMAKE_C_COMPILER_ID"
            "cCompilerVersion=CMAKE_C_COMPILER_VERSION"
            "cxxCompiler=CMAKE_CXX_COMPILER"
            "cxxCompilerId=CMAKE_CXX_COMPILER_ID"
            "cxxCompilerVersion=CMAKE_CXX_COMPILER_VERSION"
            "cxxCompilerFrontend=CMAKE_CXX_COMPILER_FRONTEND_VARIANT"
            "systemName=CMAKE_SYSTEM_NAME"
            "systemProcessor=CMAKE_SYSTEM_PROCESSOR"
            "osxSysroot=CMAKE_OSX_SYSROOT"
            "cxxStandard=CMAKE_CXX_STANDARD"
            "toolchainFile=CMAKE_TOOLCHAIN_FILE")
        string(REPLACE "=" ";" pair "${pair}")
        list(GET pair 0 key)
        list(GET pair 1 var)
        epm_json_set_string(json ${key} "${${var}}")
    endforeach ()

    # A child build is told the target system only when CMake cross-compiles.
    if (CMAKE_CROSSCOMPILING)
        string(JSON json SET "${json}" crossCompiling true)
    else ()
        string(JSON json SET "${json}" crossCompiling false)
    endif ()

    epm_osx_architectures(architectures)
    epm_json_set_string(json osxArchitectures "${architectures}")
    epm_osx_deployment_target(deployment_target)
    epm_json_set_string(json osxDeploymentTarget "${deployment_target}")
    epm_osx_sysroot_path(sysroot_path)
    epm_json_set_string(json osxSysrootPath "${sysroot_path}")
    epm_sdk_version(sdk_version)
    epm_json_set_string(json sdkVersion "${sdk_version}")

    # CMake's own Android support sets CMAKE_ANDROID_ARCH_ABI; the NDK's
    # legacy toolchain file only ANDROID_ABI.
    set(android_abi "${CMAKE_ANDROID_ARCH_ABI}")

    if (NOT android_abi)
        set(android_abi "${ANDROID_ABI}")
    endif ()

    epm_json_set_string(json androidAbi "${android_abi}")

    # The runtime library the dependencies must share with the project's
    # targets. Under the old policy the variable never reaches a target, and
    # the project would link the DLL runtime against static dependencies.
    epm_msvc_runtime(msvc_runtime)
    epm_json_set_string(json msvcRuntime "${msvc_runtime}")

    if (NOT msvc_runtime STREQUAL "")
        cmake_policy(GET CMP0091 runtime_policy)

        if (NOT runtime_policy STREQUAL "NEW")
            message(FATAL_ERROR "epm: this project's targets ignore "
                    "CMAKE_MSVC_RUNTIME_LIBRARY (${msvc_runtime}), which epm "
                    "builds its dependencies with: policy CMP0091 is not NEW. "
                    "Raise cmake_minimum_required to 3.15 or newer, or add "
                    "cmake_policy(SET CMP0091 NEW) before the first target.")
        endif ()
    endif ()

    # For epm.cpp recipes to branch on; epm itself gives it no meaning.
    set(profile "${EPM_PROFILE}")

    if (profile STREQUAL "" AND DEFINED ENV{EPM_PROFILE})
        set(profile "$ENV{EPM_PROFILE}")
    endif ()

    epm_json_set_string(json profile "${profile}")

    # Where a recipe executable finds the recipe SDK: the prefix of this epm.
    epm_json_set_string(json epmExecutable "${EPM_EXECUTABLE}")

    file(WRITE "${EPM_DIR}/env.json" "${json}\n")
    set_property(GLOBAL PROPERTY EPM_ENV_WRITTEN TRUE)
endfunction()

# --cache-store <dir> for each entry of EPM_CACHE_STORES, made absolute against
# the source directory; nothing when it is empty, so the binary reads the
# EPM_CACHE_STORES environment variable instead.
function(epm_cache_store_arguments out_var)
    set(arguments "")

    foreach (store IN LISTS EPM_CACHE_STORES)
        if (NOT store STREQUAL "")
            cmake_path(ABSOLUTE_PATH store BASE_DIRECTORY "${CMAKE_SOURCE_DIR}"
                    OUTPUT_VARIABLE absolute)
            list(APPEND arguments --cache-store "${absolute}")
        endif ()
    endforeach ()

    set(${out_var} "${arguments}" PARENT_SCOPE)
endfunction()

# Runs the epm binary with `arguments` plus --cache, --cache-store and --verbose
# as asked for.
# Its stdout goes straight to the console as it is written: without --verbose
# that is only the "-- epm: building ..." progress of a dependency built into
# the cache. stderr is kept for the failure message (and echoed with
# EPM_VERBOSE). Fails the configure with `what` and stderr on a non-zero exit.
function(epm_run_binary what)
    set(command "${EPM_EXECUTABLE}" ${ARGN})

    if (EPM_CACHE)
        list(APPEND command --cache "${EPM_CACHE}")
    endif ()

    epm_cache_store_arguments(store_arguments)
    list(APPEND command ${store_arguments})

    set(echo_args "")

    if (EPM_VERBOSE)
        list(APPEND command --verbose)
        set(echo_args ECHO_ERROR_VARIABLE)
    endif ()

    # The project's directory, for any relative path the binary meets.
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
    set_property(GLOBAL PROPERTY EPM_OVERRIDES_WRITTEN TRUE)
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

# The compiler family of a CMAKE_CXX_COMPILER_ID and its
# CMAKE_CXX_COMPILER_FRONTEND_VARIANT: MSVC for cl and for clang-cl, which
# share an ABI; else the id itself.
function(epm_recipe_compiler_family id frontend out_var)
    if (id STREQUAL "MSVC" OR frontend STREQUAL "MSVC")
        set(${out_var} MSVC PARENT_SCOPE)
    else ()
        set(${out_var} "${id}" PARENT_SCOPE)
    endif ()
endfunction()

# Defines epm::recipe, once per configure, from what `epm recipe-sdk` says the
# epm binary builds a recipe executable with: an INTERFACE IMPORTED target with
# the include directories, definitions, C++ standard and compile options, which
# links, in order, one STATIC IMPORTED target per archive of epm's recipe
# libraries (one location per configuration epm installed, picked as
# epm_recipe_config_preference says), then EPM_RECIPE_LINK_OPTIONS. When the
# SDK's files are not here, sets the global EPM_RECIPE_SDK_PROBLEM instead, for
# epm_add_recipe to report. A malformed SDK, or a C++ compiler of another
# family than epm's (whose static libraries the recipe links), stops the
# configure; EPM_RECIPE_ALLOW_COMPILER_MISMATCH=ON makes the latter a warning.
function(epm_recipe_sdk)
    get_property(done GLOBAL PROPERTY EPM_RECIPE_SDK_DONE)

    if (done)
        return()
    endif ()

    set_property(GLOBAL PROPERTY EPM_RECIPE_SDK_DONE TRUE)
    set(sdk_file "${EPM_DIR}/recipe-sdk.cmake")
    file(REMOVE "${sdk_file}")
    file(MAKE_DIRECTORY "${EPM_DIR}")

    execute_process(COMMAND "${EPM_EXECUTABLE}" recipe-sdk --out "${sdk_file}"
            RESULT_VARIABLE result
            ERROR_VARIABLE error)

    if (NOT result EQUAL 0 OR NOT EXISTS "${sdk_file}")
        string(STRIP "${error}" error)
        message(FATAL_ERROR "epm: epm recipe-sdk failed (exit ${result}): ${error}")
    endif ()

    include("${sdk_file}")
    set(origin "the epm at ${EPM_EXECUTABLE}")
    set(configs "")

    foreach (config IN LISTS EPM_RECIPE_CONFIGS)
        string(TOUPPER "${config}" upper)
        list(APPEND configs "${upper}")
    endforeach ()

    list(REMOVE_DUPLICATES configs)

    if (configs STREQUAL "")
        message(FATAL_ERROR "epm: ${origin} names no EPM_RECIPE_CONFIGS in "
                "${sdk_file}: it predates recipe executables. Use an epm and "
                "epm.cmake of the same version.")
    endif ()

    # Each configuration's archives, by upper-case name, however recipe-sdk
    # spelled it.
    set(listed "")
    get_cmake_property(variables VARIABLES)

    foreach (variable IN LISTS variables)
        if (NOT variable MATCHES "^EPM_RECIPE_LINK_LIBRARIES_(.+)$")
            continue()
        endif ()

        string(TOUPPER "${CMAKE_MATCH_1}" upper)

        if (NOT upper IN_LIST configs)
            message(FATAL_ERROR "epm: ${sdk_file} of ${origin} sets ${variable}, "
                    "but ${CMAKE_MATCH_1} is not in EPM_RECIPE_CONFIGS "
                    "(${EPM_RECIPE_CONFIGS})")
        endif ()

        set(archives_${upper} "${${variable}}")
        list(APPEND listed "${upper}")
    endforeach ()

    set(count "")

    foreach (config IN LISTS configs)
        if (NOT config IN_LIST listed)
            message(FATAL_ERROR "epm: ${sdk_file} of ${origin} lists ${config} in "
                    "EPM_RECIPE_CONFIGS (${EPM_RECIPE_CONFIGS}) but sets no "
                    "EPM_RECIPE_LINK_LIBRARIES_${config}")
        endif ()

        list(LENGTH archives_${config} length)

        if (length EQUAL 0)
            message(FATAL_ERROR "epm: ${sdk_file} of ${origin} lists no "
                    "libraries for ${config}")
        elseif (count STREQUAL "")
            set(count ${length})
            set(first_config ${config})
        elseif (NOT length EQUAL count)
            message(FATAL_ERROR "epm: ${sdk_file} of ${origin} lists ${count} "
                    "libraries for ${first_config} but ${length} for ${config}; "
                    "each configuration must list the same libraries, in the "
                    "same order")
        endif ()
    endforeach ()

    # An imported target naming a missing file fails the generate or the link:
    # an epm whose build or install tree has moved cannot build recipes here.
    foreach (directory IN LISTS EPM_RECIPE_INCLUDE_DIRS)
        if (NOT IS_DIRECTORY "${directory}")
            set_property(GLOBAL PROPERTY EPM_RECIPE_SDK_PROBLEM
                    "${origin} was built against headers that are not here (${directory})")
            return()
        endif ()
    endforeach ()

    foreach (config IN LISTS configs)
        foreach (archive IN LISTS archives_${config})
            if (NOT EXISTS "${archive}")
                set_property(GLOBAL PROPERTY EPM_RECIPE_SDK_PROBLEM
                        "${origin} names a ${config} library that is not here (${archive})")
                return()
            endif ()
        endforeach ()
    endforeach ()

    # The archives hold epm's C++ types, compiled by epm's compiler: another
    # compiler family need not share their ABI (standard library, mangling).
    # cl and clang-cl are one family: clang-cl's id is Clang, but it takes
    # cl's command line (an MSVC frontend) and targets the MSVC ABI.
    epm_recipe_compiler_family("${CMAKE_CXX_COMPILER_ID}"
            "${CMAKE_CXX_COMPILER_FRONTEND_VARIANT}" ours_family)
    epm_recipe_compiler_family("${EPM_RECIPE_COMPILER_ID}"
            "${EPM_RECIPE_COMPILER_FRONTEND}" epms_family)

    if (NOT ours_family STREQUAL epms_family)
        get_filename_component(prefix "${EPM_EXECUTABLE}" DIRECTORY)
        get_filename_component(prefix "${prefix}" DIRECTORY)
        set(text "this build compiles C++ with ${CMAKE_CXX_COMPILER_ID} "
                "(${CMAKE_CXX_COMPILER}), but ${origin} was built with "
                "${EPM_RECIPE_COMPILER_ID} (${EPM_RECIPE_CXX_COMPILER}), and a "
                "recipe executable links epm's static libraries")
        string(JOIN "" text ${text})

        if (EPM_RECIPE_ALLOW_COMPILER_MISMATCH)
            message(WARNING "epm: ${text}. EPM_RECIPE_ALLOW_COMPILER_MISMATCH is "
                    "set: trying anyway.")
        else ()
            message(FATAL_ERROR "epm: ${text}. Configure with "
                    "-DCMAKE_CXX_COMPILER=${EPM_RECIPE_CXX_COMPILER}, or use an epm "
                    "built with this compiler (get_epm.cmake builds epm with the "
                    "compiler of the configure that first needs it: delete "
                    "${prefix} to rebuild it), or set "
                    "-DEPM_RECIPE_ALLOW_COMPILER_MISMATCH=ON to try anyway.")
        endif ()
    endif ()

    # One imported archive per position in the lists. IMPORTED_CONFIGURATIONS
    # and the configuration-less location put Release first, so an unmapped
    # configuration (or none) gets Release.
    epm_recipe_config_preference("" "${configs}" fallback)
    list(GET fallback 0 fallback_config)
    set(mapped_configs "")

    foreach (config IN ITEMS DEBUG RELEASE RELWITHDEBINFO MINSIZEREL NOCONFIG
            ${CMAKE_CONFIGURATION_TYPES} ${CMAKE_BUILD_TYPE})
        string(TOUPPER "${config}" upper)
        list(APPEND mapped_configs "${upper}")
    endforeach ()

    list(REMOVE_DUPLICATES mapped_configs)
    set(link_items "")
    math(EXPR last "${count} - 1")

    foreach (index RANGE ${last})
        list(GET archives_${fallback_config} ${index} archive)
        get_filename_component(stem "${archive}" NAME_WE)
        string(REGEX REPLACE "^lib" "" stem "${stem}")
        string(REGEX REPLACE "[^A-Za-z0-9_.+-]" "_" stem "${stem}")
        set(library "epm::recipe-${stem}")

        if (TARGET "${library}")
            string(APPEND library "-${index}")
        endif ()

        add_library(${library} STATIC IMPORTED GLOBAL)
        set_target_properties(${library} PROPERTIES
                IMPORTED_CONFIGURATIONS "${fallback}"
                IMPORTED_LOCATION "${archive}"
                IMPORTED_LINK_INTERFACE_LANGUAGES CXX)

        foreach (config IN LISTS configs)
            list(GET archives_${config} ${index} location)
            set_property(TARGET ${library} PROPERTY
                    IMPORTED_LOCATION_${config} "${location}")
        endforeach ()

        foreach (config IN LISTS mapped_configs)
            epm_recipe_config_preference("${config}" "${configs}" preference)
            set_property(TARGET ${library} PROPERTY
                    MAP_IMPORTED_CONFIG_${config} "${preference}")
        endforeach ()

        list(APPEND link_items ${library})
    endforeach ()

    # After the archives, which need them. A framework is one item with its
    # name, however recipe-sdk split it.
    set(pending "")

    foreach (item IN LISTS EPM_RECIPE_LINK_OPTIONS)
        if (NOT pending STREQUAL "")
            list(APPEND link_items "${pending} ${item}")
            set(pending "")
        elseif (item MATCHES "^-(weak_)?framework$")
            set(pending "${item}")
        else ()
            list(APPEND link_items "${item}")
        endif ()
    endforeach ()

    # recipe-sdk reports one set of definitions, the running (Release) epm's,
    # yet they reach the Debug archives' recipes too. None exist today.
    if (NOT "${EPM_RECIPE_COMPILE_DEFINITIONS}" STREQUAL "")
        message(AUTHOR_WARNING "epm: EPM_RECIPE_COMPILE_DEFINITIONS "
                "(${EPM_RECIPE_COMPILE_DEFINITIONS}) come from epm's own "
                "configuration and apply to every configuration of the recipe")
    endif ()

    add_library(epm::recipe INTERFACE IMPORTED GLOBAL)
    set_target_properties(epm::recipe PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${EPM_RECIPE_INCLUDE_DIRS}"
            INTERFACE_COMPILE_DEFINITIONS "${EPM_RECIPE_COMPILE_DEFINITIONS}"
            INTERFACE_LINK_LIBRARIES "${link_items}"
            EPM_CXX_STANDARD "${EPM_RECIPE_CXX_STANDARD}"
            EPM_CXX_EXTENSIONS "${EPM_RECIPE_CXX_EXTENSIONS}")

    if (EPM_RECIPE_CXX_STANDARD)
        set_property(TARGET epm::recipe PROPERTY
                INTERFACE_COMPILE_FEATURES cxx_std_${EPM_RECIPE_CXX_STANDARD})
    endif ()

    # epm-recipe's warning flags, which the recipe header compiles clean under,
    # are only meant for the compiler family that built epm: with the mismatch
    # allowed above, another family does without them.
    if (ours_family STREQUAL epms_family)
        set_property(TARGET epm::recipe APPEND PROPERTY INTERFACE_COMPILE_OPTIONS
                ${EPM_RECIPE_COMPILE_OPTIONS})
    endif ()

    # On the MSVC ABI a recipe's objects must be compiled against the runtime
    # library of the archives they link (LNK2038 otherwise): the one of the
    # SDK configuration each of this build's configurations maps to, which is
    # not always the same-named one (a build tree's epm has one
    # configuration). epm_add_recipe gives the target this property: the
    # fallback configuration's runtime, under one $<IF> per configuration
    # mapped above.
    set(runtime "")

    if (epms_family STREQUAL "MSVC"
            AND DEFINED EPM_RECIPE_MSVC_RUNTIME_${fallback_config})
        set(runtime "${EPM_RECIPE_MSVC_RUNTIME_${fallback_config}}")

        foreach (config IN LISTS mapped_configs)
            epm_recipe_config_preference("${config}" "${configs}" preference)
            list(GET preference 0 sdk_config)

            if (NOT config STREQUAL "NOCONFIG"
                    AND DEFINED EPM_RECIPE_MSVC_RUNTIME_${sdk_config})
                set(runtime
                        "$<IF:$<CONFIG:${config}>,${EPM_RECIPE_MSVC_RUNTIME_${sdk_config}},${runtime}>")
            endif ()
        endforeach ()
    endif ()

    set_property(GLOBAL PROPERTY EPM_RECIPE_MSVC_RUNTIME_LIBRARY "${runtime}")

    # epm's archives come with one runtime library, the static one unless epm
    # was built with another: a project that chose the other kind still gets
    # a recipe executable that links, compiled with epm's, while its
    # dependencies follow the project (env.json's msvcRuntime).
    if (NOT runtime STREQUAL "")
        epm_msvc_runtime(project_runtime)
        epm_msvc_runtime_kind("${project_runtime}" project_kind)
        epm_msvc_runtime_kind("${EPM_RECIPE_MSVC_RUNTIME_${fallback_config}}"
                epms_kind)

        if (NOT project_kind STREQUAL epms_kind)
            message(WARNING "epm: this project links the ${project_kind} runtime "
                    "library (CMAKE_MSVC_RUNTIME_LIBRARY is ${project_runtime}), "
                    "but ${origin} and its recipe libraries were built with the "
                    "${epms_kind} one: the recipe executable is compiled with "
                    "epm's runtime, the project's dependencies with the "
                    "project's. Build epm with the project's runtime, or leave "
                    "CMAKE_MSVC_RUNTIME_LIBRARY to epm's default, to have one.")
        endif ()
    endif ()

    set(ours "${CMAKE_CXX_COMPILER}")
    set(epms "${EPM_RECIPE_CXX_COMPILER}")

    foreach (path IN ITEMS ours epms)
        if (EXISTS "${${path}}")
            file(REAL_PATH "${${path}}" ${path})
        endif ()
    endforeach ()

    if (NOT ours STREQUAL epms)
        message(STATUS "epm: recipe targets build with ${CMAKE_CXX_COMPILER}, but "
                "epm compiles the epm.cpp it runs with ${EPM_RECIPE_CXX_COMPILER}")
    endif ()
endfunction()

# The inputs of epm_add_recipe's configure, written by the end of the configure
# even when nothing asked epm for a package: env.json, and overrides.json
# unless epm_configure_graph wrote it already.
function(epm_write_recipe_inputs)
    epm_write_env()
    get_property(written GLOBAL PROPERTY EPM_OVERRIDES_WRITTEN)

    if (NOT written)
        epm_write_overrides("${EPM_DIR}/overrides.json")
        set_property(GLOBAL PROPERTY EPM_OVERRIDES_WRITTEN TRUE)
    endif ()
endfunction()

# epm_add_recipe(<target> [SOURCE <file>])
#
# Builds an epm.cpp (by default ${CMAKE_CURRENT_SOURCE_DIR}/epm.cpp) as the
# executable <target>, linked against epm::recipe: the same program epm
# configure compiles into the cache, here built by this build, so that IDEs
# index it, the build reports its errors and a debugger can run it.
#
# The executable can run the whole configure: by default (its `configure`
# command) it describes the recipe, fetches the packages, finds or builds
# their cache entries and writes graph.json, graph.cmake and steps/, running
# the build steps this recipe owns in its own process and every other
# recipe's in that recipe's executable. The configure CMake runs still goes
# through `epm configure`, which compiles and runs epm's cached copy of the
# same source; this one runs only when asked:
#
#   <target> configure --manifest <file> --env <build dir>/epm/env.json
#            --overrides <build dir>/epm/overrides.json --out <build dir>/epm
#            [--cache <dir>] [--cache-store <dir>...] [--verbose]
#
# which is what the custom target <target>-configure runs (it refreshes the
# build's graph files) and what Xcode and Visual Studio get as the target's
# debugger arguments, in the top-level source directory.
#
# To debug describe() or a hook, run <target> with those default arguments
# under a debugger, from a Debug build, and stop in it. To debug one build
# step, run <target> with `build --package <name> --in <build dir>/epm/steps/
# <name>.in.json --out <any file>`: the steps this recipe owns are those of
# the packages it declares with add<T> (unless the package has an epm.cpp of
# its own) and the default steps of the packages it declares that no other
# declaration gives custom steps. A step another recipe owns runs in that
# recipe's executable: ${EPM_DIR}/steps/<name>.command holds the command line
# that runs it.
#
# `<target> describe --env <env.json> [--out <project.json>]` prints the
# Project describe() declares, as JSON.
#
# Does nothing when cross-compiling (a recipe runs on the host, against epm's
# host libraries), and in a project that is not the top-level one (a package
# added from source; a child build epm runs gets a stub from
# epm-export.cmake).
function(epm_add_recipe target)
    cmake_parse_arguments(EPM_ARGS "" "SOURCE" "" ${ARGN})

    if (EPM_ARGS_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
                "epm_add_recipe: unknown arguments: ${EPM_ARGS_UNPARSED_ARGUMENTS}")
    endif ()

    if (NOT PROJECT_IS_TOP_LEVEL)
        return()
    endif ()

    if (CMAKE_CROSSCOMPILING)
        message(STATUS "epm: epm_add_recipe(${target}) does nothing when "
                "cross-compiling: a recipe runs on the host")
        return()
    endif ()

    get_property(languages GLOBAL PROPERTY ENABLED_LANGUAGES)

    if (NOT CXX IN_LIST languages)
        message(FATAL_ERROR "epm_add_recipe(${target}): the project has no CXX "
                "language; give project() CXX")
    endif ()

    set(source "${EPM_ARGS_SOURCE}")

    if (source STREQUAL "")
        set(source epm.cpp)
    endif ()

    cmake_path(ABSOLUTE_PATH source BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            NORMALIZE)

    epm_recipe_sdk()
    get_property(problem GLOBAL PROPERTY EPM_RECIPE_SDK_PROBLEM)

    if (problem)
        message(STATUS "epm: epm_add_recipe(${target}) does nothing: ${problem}")
        return()
    endif ()

    # As RecipeBuild.cpp builds it: the SDK's standard and dialect, no IPO.
    add_executable(${target} "${source}")
    target_link_libraries(${target} PRIVATE epm::recipe)
    get_target_property(standard epm::recipe EPM_CXX_STANDARD)
    get_target_property(extensions epm::recipe EPM_CXX_EXTENSIONS)
    set_target_properties(${target} PROPERTIES
            CXX_EXTENSIONS ${extensions}
            INTERPROCEDURAL_OPTIMIZATION OFF
            FOLDER epm)

    if (standard)
        set_target_properties(${target} PROPERTIES
                CXX_STANDARD ${standard}
                CXX_STANDARD_REQUIRED ON)
    endif ()

    # The runtime library of the archives each configuration links
    # (epm_recipe_sdk); the default elsewhere.
    get_property(runtime GLOBAL PROPERTY EPM_RECIPE_MSVC_RUNTIME_LIBRARY)

    if (runtime)
        set_target_properties(${target} PROPERTIES MSVC_RUNTIME_LIBRARY "${runtime}")
    endif ()

    # A host program: a universal (or foreign-architecture) build of the
    # project would need epm's libraries in architectures it may not have.
    if (APPLE AND CMAKE_OSX_ARCHITECTURES)
        set_target_properties(${target} PROPERTIES
                OSX_ARCHITECTURES "${CMAKE_HOST_SYSTEM_PROCESSOR}")
    endif ()

    # The env.json and overrides.json the configure reads are the configure's
    # own, written by then even when nothing asked epm for a package.
    get_property(inputs_deferred GLOBAL PROPERTY EPM_RECIPE_INPUTS_DEFERRED)

    if (NOT inputs_deferred)
        set_property(GLOBAL PROPERTY EPM_RECIPE_INPUTS_DEFERRED TRUE)
        cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}"
                CALL epm_write_recipe_inputs)
    endif ()

    # epm configure's own arguments (epm_configure_graph, epm_run_binary), for
    # this build's executable rather than epm's cached one: in a Debug
    # configuration it has debug info, and a debugger running it stops in
    # describe(), a hook, or the steps of the packages this recipe owns.
    set(inputs --manifest "${source}"
            --env "${EPM_DIR}/env.json"
            --overrides "${EPM_DIR}/overrides.json")
    set(options "")

    # Against the source directory, as epm_run_binary's: an IDE may start the
    # executable elsewhere.
    if (EPM_CACHE)
        cmake_path(ABSOLUTE_PATH EPM_CACHE BASE_DIRECTORY "${CMAKE_SOURCE_DIR}"
                OUTPUT_VARIABLE cache)
        list(APPEND options --cache "${cache}")
    endif ()

    epm_cache_store_arguments(store_arguments)
    list(APPEND options ${store_arguments})

    if (EPM_VERBOSE)
        list(APPEND options --verbose)
    endif ()

    set(arguments configure ${inputs} --out "${EPM_DIR}" ${options})
    string(JOIN " " debugger_arguments ${arguments})
    set_target_properties(${target} PROPERTIES
            VS_DEBUGGER_COMMAND_ARGUMENTS "${debugger_arguments}"
            VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            XCODE_SCHEME_ARGUMENTS "${arguments}"
            XCODE_SCHEME_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

    add_custom_target(${target}-configure
            COMMAND ${target} ${arguments}
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            COMMENT "Configuring ${source} ..."
            USES_TERMINAL
            VERBATIM)
    add_dependencies(${target}-configure ${target})
    set_target_properties(${target}-configure PROPERTIES FOLDER epm)
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

    epm_fetches_source("${name}" fetch_source)
    set(source_dir_flag "")

    if (NOT fetch_source)
        set(source_dir_flag --no-source-dir)
    endif ()

    epm_run_binary("resolving ${name}"
            resolve
            --request "${request_file}"
            --env "${EPM_DIR}/env.json"
            --response "${response_file}"
            ${source_dir_flag})

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
            CONFIGS ${epm_${name}_CONFIGS}
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
