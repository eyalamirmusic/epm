# get_epm.cmake -- the one file a project vendors to use epm.
#
# With a manifest (Docs/Graph.md): list this file in
# CMAKE_PROJECT_TOP_LEVEL_INCLUDES before project(), and keep epm.cpp (the
# recipe naming the dependencies) beside the top-level CMakeLists.txt:
#
#   cmake_minimum_required(VERSION 3.25)
#   set(EPM_VERSION "0.2...<0.3")    # optional; default: the newest release
#   list(APPEND CMAKE_PROJECT_TOP_LEVEL_INCLUDES cmake/get_epm.cmake)
#   project(App CXX)
#   find_package(fmt REQUIRED)
#
# The first project() then includes this file, which bootstraps the binary (below)
# and registers epm's dependency provider. At the first find_package() or
# FetchContent_MakeAvailable(), the provider writes ${CMAKE_BINARY_DIR}/epm/
# env.json and overrides.json, runs `epm configure` over epm.cpp (which the
# binary builds into an executable and runs itself) and loads each package the
# graph names, dependencies first; other names are left to CMake. With
# EPM_NESTED set (a child build epm runs) this file does nothing at all;
# without an epm.cpp, nothing is registered.
#
# Without a manifest, the older form still works, for epm_add_package only:
#
#   include(cmake/get_epm.cmake)     # after project()
#   epm_add_package(NAME ... GITHUB_REPOSITORY ... GIT_TAG ...)
#
# Either way it finds an epm binary, then includes the epm.cmake installed with
# that binary, so module and binary always match. No binary is downloaded: a
# recipe links epm's static libraries, which have to come from this machine's
# compiler, so an epm that is not installed already is built from source (once
# per version, Debug and Release; a cold build takes under a minute on a fast
# machine, longer in CI). First in-range match wins:
#
#   1. EPM_EXECUTABLE, when set.
#      Then EPM_SOURCE_DIR, when set (skips 2-4): that epm checkout, built and
#      installed into <cache>/tool/local-<sha256(real path)[:12]>/. The build
#      directories stay in the entry, so later configures are incremental
#      `cmake --build` runs. Its version is printed, not checked against
#      EPM_VERSION. For testing an unreleased epm from a consumer.
#      Else, when this file is <dir>/CMake/get_epm.cmake of an epm checkout
#      (<dir>/CMakeLists.txt has project(epm), as for the examples in the epm
#      repository) and no EPM_EXECUTABLE, EPM_SOURCE_REF or EPM_SOURCE_URL is
#      given: EPM_SOURCE_DIR defaults to <dir>. A copy vendored into a consumer
#      never matches.
#   2. epm on PATH, with <prefix>/share/epm/cmake/epm.cmake beside it (an
#      installed prefix).
#   3. The newest entry an earlier configure built in <cache>/tool/v<version>/,
#      unless a newer release (4) is in range.
#   4. A source build of the newest release tag v<x.y.z> in range, from
#      `git ls-remote --tags` of the epm repository (listed at most once a day,
#      kept in <cache>/tool/latest.json; EPM_UPDATE=ON lists now). When the
#      tags cannot be listed (offline, no git): the exact lower bound x.y.z of
#      EPM_VERSION, else main. Fetched with git into <cache>/tool/<tag>/src
#      (without git, GitHub's tarball of the tag), built and installed into
#      <cache>/tool/<tag>/.
#
# A tool entry holds what `cmake --install --config <C>` of epm produces for
# Debug and then Release: bin/epm, lib/<Config>/, include/ and
# share/epm/cmake/ (epm.cmake, epm-provider.cmake), share/epm/epm-config (one
# line: the configuration of bin/epm; Release always wins bin/epm), plus
# epm-bootstrap.log, and src/ for a tag entry or the build directories and
# cpm/ for a local-<hash> one. Its marker <cache>/tool/<key>.json, written last (and removed before a
# rebuild), marks it complete: a later configure uses it and builds nothing.
#
# The build is a host build: nothing from the consumer's toolchain file, build
# type or target architectures reaches it, so it works while the consumer
# targets iOS or Android (when cross-compiling, CC, CXX, CFLAGS, CXXFLAGS and
# LDFLAGS are dropped from its environment too, and always SDKROOT and the
# deployment targets an Xcode build exports). Whatever the consumer's
# generator (Xcode, Visual Studio, Ninja Multi-Config), it is built with Ninja
# when there is a ninja, else Unix Makefiles; on Windows without a ninja, or
# for a Visual Studio consumer outside a developer prompt, with the consumer's
# generator. It uses any compiler launcher, and the consumer's C and C++
# compilers and CMAKE_LINKER_TYPE when they are known and native (no toolchain
# file, not cross-compiling), so that epm's libraries match the compiler that
# builds the consumer's recipe target (epm_add_recipe).
#
# Variables read, all optional (set before the include, or with -D):
#   EPM_VERSION               "0.1.2" (meaning >= 0.1.2, as in find_package),
#                             "0.1...<0.2" or "0.1...0.2"; unset: any version
#   EPM_EXECUTABLE            an epm binary to try first
#   EPM_SOURCE_DIR            an epm checkout to build and use (else the
#                             EPM_SOURCE_DIR environment variable; a relative
#                             path is relative to the top-level source dir)
#   EPM_UPDATE                ON: list the release tags now (a -D value is
#                             cleared from the cache afterwards)
#   EPM_SOURCE_REF            build this epm tag, branch or commit (skips 2-3)
#   EPM_SOURCE_URL            build this source tarball (skips 2-3)
#   EPM_REPOSITORY            the epm git repository (default: GitHub's), for a
#                             mirror or a fork
#   EPM_BOOTSTRAP_SOURCE_DIR  deprecated: read as EPM_SOURCE_DIR when that is
#                             unset
#   EPM_CACHE                 cache root (else the EPM_CACHE environment
#                             variable, else the platform cache directory)
#   EPM_CACHE_STORES          read-only cache stores binaries are copied from
#                             when the cache lacks them (else the
#                             EPM_CACHE_STORES environment variable); used by
#                             epm.cmake, never by the bootstrap
#   EPM_VERBOSE               say what is tried; stream the build output
#   EPM_NESTED                set by epm for its child builds: do nothing
#
# The binary ends up in EPM_EXECUTABLE (cache INTERNAL). See Docs/PoC.md §8 and
# Docs/Graph.md §8.

include_guard(GLOBAL)

set(GET_EPM_REPOSITORY "https://github.com/eyalamirmusic/epm")
set(GET_EPM_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(GET_EPM_FILE "${CMAKE_CURRENT_LIST_FILE}")

if (CMAKE_HOST_WIN32)
    set(GET_EPM_SUFFIX ".exe")
else ()
    set(GET_EPM_SUFFIX "")
endif ()

function(get_epm_note text)
    if (EPM_VERBOSE)
        message(STATUS "epm: ${text}")
    else ()
        message(VERBOSE "epm: ${text}")
    endif ()
endfunction()

# Splits an EPM_VERSION value into range_min, range_max ("" when open) and
# range_exclusive (the `...<` form).
function(get_epm_parse_range range)
    string(STRIP "${range}" range)
    set(number "[0-9]+(\\.[0-9]+)*")
    set(min "")
    set(max "")
    set(exclusive FALSE)

    if (range MATCHES "^(${number})\\.\\.\\.(<?)(${number})$")
        set(min "${CMAKE_MATCH_1}")
        set(max "${CMAKE_MATCH_4}")

        if (CMAKE_MATCH_3 STREQUAL "<")
            set(exclusive TRUE)
        endif ()
    elseif (range MATCHES "^${number}$")
        set(min "${range}")
    elseif (NOT range STREQUAL "")
        message(FATAL_ERROR "epm: EPM_VERSION '${range}' is neither a version "
                "(0.1.2) nor a range (0.1...<0.2, 0.1...0.2)")
    endif ()

    set(range_min "${min}" PARENT_SCOPE)
    set(range_max "${max}" PARENT_SCOPE)
    set(range_exclusive ${exclusive} PARENT_SCOPE)
endfunction()

# Whether `version` satisfies `range` (EPM_VERSION syntax; empty means any).
# Only plain numeric versions qualify.
function(epm_version_in_range version range out_var)
    get_epm_parse_range("${range}")
    set(in_range FALSE)

    if (version MATCHES "^[0-9]+(\\.[0-9]+)*$"
            AND (range_min STREQUAL "" OR version VERSION_GREATER_EQUAL range_min))
        if (range_max STREQUAL "")
            set(in_range TRUE)
        elseif (range_exclusive AND version VERSION_LESS range_max)
            set(in_range TRUE)
        elseif (NOT range_exclusive AND version VERSION_LESS_EQUAL range_max)
            set(in_range TRUE)
        endif ()
    endif ()

    set(${out_var} ${in_range} PARENT_SCOPE)
endfunction()

# The lower bound of `range` when it is a full x.y.z, else empty.
function(get_epm_exact_lower_bound range out_var)
    get_epm_parse_range("${range}")

    if (range_min MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
        set(${out_var} "${range_min}" PARENT_SCOPE)
    else ()
        set(${out_var} "" PARENT_SCOPE)
    endif ()
endfunction()

# The release versions in `git ls-remote --tags` output: the x.y.z of every
# refs/tags/vx.y.z (no prerelease spellings, no peeled ^{} lines, no nightly).
function(get_epm_parse_tags output out_var)
    string(REPLACE "\r" "" output "${output}")
    string(REPLACE "\n" ";" lines "${output}")
    set(versions "")

    foreach (line IN LISTS lines)
        if (line MATCHES "refs/tags/v([0-9]+(\\.[0-9]+)*)$")
            list(APPEND versions "${CMAKE_MATCH_1}")
        endif ()
    endforeach ()

    list(REMOVE_DUPLICATES versions)
    set(${out_var} "${versions}" PARENT_SCOPE)
endfunction()

# The newest of `versions` within `range`, or empty.
function(get_epm_newest_in_range versions range out_var)
    set(newest "")

    foreach (version IN LISTS versions)
        epm_version_in_range("${version}" "${range}" in_range)

        if (in_range AND (newest STREQUAL "" OR version VERSION_GREATER newest))
            set(newest "${version}")
        endif ()
    endforeach ()

    set(${out_var} "${newest}" PARENT_SCOPE)
endfunction()

# Whether `generator` builds several configurations from one build directory.
function(get_epm_is_multi_config generator out_var)
    if (generator MATCHES "^(Visual Studio|Xcode)|Multi-Config$")
        set(${out_var} TRUE PARENT_SCOPE)
    else ()
        set(${out_var} FALSE PARENT_SCOPE)
    endif ()
endfunction()

# The cache root the binary uses without --cache (CacheRoot::fromEnvironment and
# eacp's FilePath::cacheDirectory()); the EPM_CACHE CMake variable comes first
# because epm.cmake passes it as --cache.
function(get_epm_cache_root out_var)
    if (EPM_CACHE)
        set(root "${EPM_CACHE}")
    elseif (NOT "$ENV{EPM_CACHE}" STREQUAL "")
        set(root "$ENV{EPM_CACHE}")
    elseif (CMAKE_HOST_WIN32)
        set(root "$ENV{LOCALAPPDATA}/epm")
    elseif (CMAKE_HOST_APPLE)
        set(root "$ENV{HOME}/Library/Caches/epm")
    elseif (NOT "$ENV{XDG_CACHE_HOME}" STREQUAL "")
        set(root "$ENV{XDG_CACHE_HOME}/epm")
    else ()
        # FilePath::homeDirectory() drops a trailing separator from HOME.
        string(REGEX REPLACE "(.)/$" "\\1" home "$ENV{HOME}")
        set(root "${home}/.cache/epm")
    endif ()

    file(TO_CMAKE_PATH "${root}" root)
    set(${out_var} "${root}" PARENT_SCOPE)
endfunction()

# The epm git repository: EPM_REPOSITORY, else GitHub's.
function(get_epm_repository out_var)
    if (EPM_REPOSITORY)
        set(${out_var} "${EPM_REPOSITORY}" PARENT_SCOPE)
    else ()
        set(${out_var} "${GET_EPM_REPOSITORY}" PARENT_SCOPE)
    endif ()
endfunction()

# The host's git (GIT_EXECUTABLE when set), never re-rooted by a cross
# toolchain; empty when there is none.
function(get_epm_find_git out_var)
    if (GIT_EXECUTABLE AND EXISTS "${GIT_EXECUTABLE}")
        set(${out_var} "${GIT_EXECUTABLE}" PARENT_SCOPE)
        return()
    endif ()

    find_program(git NAMES git NO_CACHE NO_CMAKE_FIND_ROOT_PATH)

    if (NOT git)
        set(git "")
    endif ()

    set(${out_var} "${git}" PARENT_SCOPE)
endfunction()

# What `executable --version` prints; empty when it does not run.
function(get_epm_version executable out_var)
    execute_process(COMMAND "${executable}" --version
            RESULT_VARIABLE result
            OUTPUT_VARIABLE version
            ERROR_QUIET
            OUTPUT_STRIP_TRAILING_WHITESPACE
            TIMEOUT 60)

    if (NOT result STREQUAL "0")
        set(version "")
    endif ()

    set(${out_var} "${version}" PARENT_SCOPE)
endfunction()

# <prefix>/share/epm/cmake/epm.cmake for <prefix>/bin/epm (symlinks resolved),
# or empty when there is none.
function(get_epm_module_beside executable out_var)
    file(REAL_PATH "${executable}" real)
    get_filename_component(prefix "${real}" DIRECTORY)
    get_filename_component(prefix "${prefix}" DIRECTORY)
    set(module "${prefix}/share/epm/cmake/epm.cmake")

    if (NOT EXISTS "${module}")
        set(module "")
    endif ()

    set(${out_var} "${module}" PARENT_SCOPE)
endfunction()

# Writes <tool_dir>.json, which makes the entry visible. Written last, by rename.
function(get_epm_write_marker tool_dir ref origin url version)
    string(TIMESTAMP built_at "%Y-%m-%dT%H:%M:%SZ" UTC)
    set(json "{}")
    string(JSON json SET "${json}" schema 1)

    foreach (field ref origin url version built_at)
        string(REPLACE "\\" "\\\\" value "${${field}}")
        string(REPLACE "\"" "\\\"" value "${value}")
        string(REPLACE "built_at" "builtAt" key "${field}")
        string(JSON json SET "${json}" "${key}" "\"${value}\"")
    endforeach ()

    file(WRITE "${tool_dir}.json.tmp" "${json}\n")
    file(RENAME "${tool_dir}.json.tmp" "${tool_dir}.json")
endfunction()

# Step 3: the newest in-range entry under <cache>/tool/v*/ with its marker. Sets
# cached_version and cached_dir (empty when there is none).
function(get_epm_find_cached cache)
    set(best_version "")
    set(best_dir "")
    file(GLOB markers "${cache}/tool/v*.json")

    foreach (marker IN LISTS markers)
        string(REGEX REPLACE "\\.json$" "" dir "${marker}")
        file(READ "${marker}" json)
        string(JSON version ERROR_VARIABLE error GET "${json}" version)
        epm_version_in_range("${version}" "${EPM_VERSION}" in_range)

        if (in_range AND EXISTS "${dir}/bin/epm${GET_EPM_SUFFIX}"
                AND EXISTS "${dir}/share/epm/cmake/epm.cmake"
                AND (best_version STREQUAL "" OR version VERSION_GREATER best_version))
            set(best_version "${version}")
            set(best_dir "${dir}")
        endif ()
    endforeach ()

    set(cached_version "${best_version}" PARENT_SCOPE)
    set(cached_dir "${best_dir}" PARENT_SCOPE)
endfunction()

# The versions of the repository's release tags, from `git ls-remote`. Empty on
# any failure (offline, no git, no such repository).
function(get_epm_list_remote_tags repository out_var)
    get_epm_find_git(git)
    set(versions "")
    set(result "")

    if (git STREQUAL "")
        get_epm_note("no git: cannot list the release tags of ${repository}")
    else ()
        get_epm_note("listing the release tags of ${repository}")
        execute_process(
                COMMAND "${CMAKE_COMMAND}" -E env GIT_TERMINAL_PROMPT=0
                "${git}" ls-remote --tags --refs "${repository}"
                RESULT_VARIABLE result
                OUTPUT_VARIABLE output
                ERROR_VARIABLE error
                TIMEOUT 60)

        if (result EQUAL 0)
            get_epm_parse_tags("${output}" versions)
        else ()
            string(STRIP "${error}" error)
            get_epm_note("listing the tags of ${repository} failed: ${error}")
        endif ()
    endif ()

    if ("${result}" STREQUAL "0")
        set(${out_var}_listed TRUE PARENT_SCOPE)
    else ()
        set(${out_var}_listed FALSE PARENT_SCOPE)
    endif ()

    set(${out_var} "${versions}" PARENT_SCOPE)
endfunction()

# Step 4's release versions: the copy saved in <cache>/tool/latest.json when it
# is under a day old, of the same repository (and EPM_UPDATE is off), else a
# fresh listing, else the stale copy. Sets `out_var` and `${out_var}_known`
# (FALSE when there is no listing at all). A saved copy that is not what this
# function writes (another schema, a malformed field) counts as no copy.
function(get_epm_release_versions cache out_var)
    get_epm_repository(repository)
    set(saved "${cache}/tool/latest.json")
    string(TIMESTAMP now "%s" UTC)
    set(versions "")
    set(known FALSE)
    set(age -1)

    if (EXISTS "${saved}")
        file(READ "${saved}" json)
        string(JSON schema ERROR_VARIABLE schema_error GET "${json}" schema)
        string(JSON checked_at ERROR_VARIABLE checked_error GET "${json}" checkedAt)
        string(JSON saved_repository ERROR_VARIABLE repository_error
                GET "${json}" repository)
        string(JSON list_type ERROR_VARIABLE type_error TYPE "${json}" versions)
        string(LENGTH "${checked_at}" checked_length)

        if (NOT schema_error AND schema STREQUAL "2"
                AND NOT checked_error AND checked_at MATCHES "^[0-9]+$"
                AND checked_length LESS 16
                AND NOT repository_error AND saved_repository STREQUAL repository
                AND NOT type_error AND list_type STREQUAL "ARRAY")
            set(known TRUE)
            math(EXPR age "${now} - ${checked_at}")
            string(JSON count LENGTH "${json}" versions)

            if (count GREATER 0)
                math(EXPR last "${count} - 1")

                foreach (index RANGE ${last})
                    string(JSON version GET "${json}" versions ${index})

                    if (version MATCHES "^[0-9]+(\\.[0-9]+)*$")
                        list(APPEND versions "${version}")
                    endif ()
                endforeach ()
            endif ()
        endif ()
    endif ()

    if (known AND NOT EPM_UPDATE AND age GREATER_EQUAL 0 AND age LESS 86400)
        get_epm_note("release tags listed ${age} s ago")
        set(${out_var} "${versions}" PARENT_SCOPE)
        set(${out_var}_known TRUE PARENT_SCOPE)
        return()
    endif ()

    get_epm_list_remote_tags("${repository}" fetched)

    if (fetched_listed)
        set(json "{}")
        string(JSON json SET "${json}" schema 2)
        string(JSON json SET "${json}" checkedAt ${now})
        string(JSON json SET "${json}" repository "\"${repository}\"")
        string(JSON json SET "${json}" versions "[]")
        set(index 0)

        foreach (version IN LISTS fetched)
            string(JSON json SET "${json}" versions ${index} "\"${version}\"")
            math(EXPR index "${index} + 1")
        endforeach ()

        file(MAKE_DIRECTORY "${cache}/tool")
        string(RANDOM LENGTH 12 ALPHABET "0123456789abcdef" id)
        file(WRITE "${saved}.${id}.tmp" "${json}\n")
        file(RENAME "${saved}.${id}.tmp" "${saved}")
        set(versions "${fetched}")
        set(known TRUE)
    elseif (known)
        get_epm_note("using the release tags as listed ${age} s ago")
    endif ()

    set(${out_var} "${versions}" PARENT_SCOPE)
    set(${out_var}_known ${known} PARENT_SCOPE)
endfunction()

# Whether the consumer builds for this host: no toolchain file, not
# cross-compiling, no foreign CMAKE_SYSTEM_NAME. Only then may its compilers and
# their environment reach the host build of epm.
function(get_epm_consumer_is_native out_var)
    if (NOT CMAKE_CROSSCOMPILING AND NOT CMAKE_TOOLCHAIN_FILE
            AND (NOT CMAKE_SYSTEM_NAME OR CMAKE_SYSTEM_NAME STREQUAL CMAKE_HOST_SYSTEM_NAME))
        set(${out_var} TRUE PARENT_SCOPE)
    else ()
        set(${out_var} FALSE PARENT_SCOPE)
    endif ()
endfunction()

# Runs one step of a source build of epm, the command in ARGN, appending its
# output to `log`. A failure is fatal.
#
# A host build: nothing from the consumer's toolchain, generator or target
# architectures reaches it unless passed on the command line, so it works while
# the consumer targets iOS or Android. CMake reads these variables from the
# environment too. A cross consumer's CC, CXX and flags (a Yocto-style SDK
# environment) are dropped as well. So are the SDK and deployment targets an
# Xcode build exports, which reach a CMake re-run from inside one and would
# point the host build at an iOS SDK.
function(get_epm_run_step step log)
    set(clean_env "${CMAKE_COMMAND}" -E env
            --unset=CMAKE_TOOLCHAIN_FILE
            --unset=CMAKE_GENERATOR
            --unset=CMAKE_GENERATOR_PLATFORM
            --unset=CMAKE_GENERATOR_TOOLSET
            --unset=CMAKE_BUILD_TYPE
            --unset=CMAKE_CONFIGURATION_TYPES
            --unset=CMAKE_OSX_ARCHITECTURES
            --unset=CMAKE_INSTALL_PREFIX
            --unset=DESTDIR
            --unset=SDKROOT
            --unset=MACOSX_DEPLOYMENT_TARGET
            --unset=IPHONEOS_DEPLOYMENT_TARGET)
    get_epm_consumer_is_native(native)

    if (NOT native)
        list(APPEND clean_env --unset=CC --unset=CXX --unset=CFLAGS
                --unset=CXXFLAGS --unset=LDFLAGS)
    endif ()

    set(echo "")

    if (EPM_VERBOSE)
        set(echo ECHO_OUTPUT_VARIABLE ECHO_ERROR_VARIABLE)
    endif ()

    string(JOIN " " command_line ${ARGN})
    file(APPEND "${log}" "\n$ ${command_line}\n")
    execute_process(COMMAND ${clean_env} ${ARGN}
            RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE output
            ${echo})
    file(APPEND "${log}" "${output}")

    if (NOT result EQUAL 0)
        string(LENGTH "${output}" length)

        if (length GREATER 4000)
            math(EXPR start "${length} - 4000")
            string(SUBSTRING "${output}" ${start} -1 output)
        endif ()

        message(FATAL_ERROR "epm: building epm from source failed at the ${step} "
                "step (${result}).\n${output}\n\nFull log: ${log}")
    endif ()
endfunction()

# The generator of the epm build, a host program whatever the consumer
# targets: Ninja when there is one (`ninja`, the path of the ninja found or
# empty), else Unix Makefiles, except on a Windows host (`windows`), where
# there may be no make and the consumer's generator is kept. A Visual Studio
# consumer on Windows outside a developer prompt (`msvc_env` false: no
# VCINSTALLDIR) keeps its generator too, since Ninja would find no compiler
# there. Never the consumer's Xcode on macOS: epm's per-config sources need a
# generator that has them. Sets tool_generator, empty for CMake's default.
function(get_epm_pick_generator consumer windows msvc_env ninja)
    if (windows AND consumer MATCHES "^Visual Studio" AND NOT msvc_env)
        set(generator "${consumer}")
    elseif (NOT ninja STREQUAL "")
        set(generator Ninja)
    elseif (NOT windows)
        set(generator "Unix Makefiles")
    else ()
        set(generator "${consumer}")
    endif ()

    set(tool_generator "${generator}" PARENT_SCOPE)
endfunction()

# The host's ninja for the epm build: the consumer's make program when its
# generator is a Ninja one, else the first ninja on PATH, never re-rooted by a
# cross toolchain; empty when there is none.
function(get_epm_find_ninja out_var)
    if (CMAKE_GENERATOR MATCHES "^Ninja" AND CMAKE_MAKE_PROGRAM)
        set(${out_var} "${CMAKE_MAKE_PROGRAM}" PARENT_SCOPE)
        return()
    endif ()

    find_program(ninja NAMES ninja ninja-build NO_CACHE NO_CMAKE_FIND_ROOT_PATH)

    if (NOT ninja)
        set(ninja "")
    endif ()

    set(${out_var} "${ninja}" PARENT_SCOPE)
endfunction()

# The generator, make program, compiler launchers, compilers and linker type
# for building epm. Sets toolchain_args, tool_generator (get_epm_pick_generator)
# and build_name (the build directory: one per generator, since a build
# directory cannot change generator). The consumer's toolset and make program
# go along only when the consumer's generator is the one picked. The compilers
# and linker type are left out when they may be a cross toolchain's: with a
# toolchain file or a foreign CMAKE_SYSTEM_NAME (get_epm_consumer_is_native).
# get_epm, as a top-level include, runs before languages are enabled, so the
# compilers are often not known yet; the build then finds the same ones the
# consumer will (CC, CXX, the default).
function(get_epm_consumer_toolchain)
    set(args "")
    set(name "build")
    get_epm_find_ninja(ninja)
    set(msvc_env FALSE)

    if (NOT "$ENV{VCINSTALLDIR}" STREQUAL "")
        set(msvc_env TRUE)
    endif ()

    get_epm_pick_generator("${CMAKE_GENERATOR}" "${CMAKE_HOST_WIN32}" ${msvc_env}
            "${ninja}")

    if (NOT tool_generator STREQUAL "")
        list(APPEND args -G "${tool_generator}")
        string(TOLOWER "${tool_generator}" generator)
        string(REGEX REPLACE "[^a-z0-9]+" "-" generator "${generator}")
        string(APPEND name "-${generator}")
    endif ()

    if (tool_generator STREQUAL "Ninja" AND NOT ninja STREQUAL "")
        list(APPEND args "-DCMAKE_MAKE_PROGRAM=${ninja}")
    elseif (NOT tool_generator STREQUAL "" AND tool_generator STREQUAL CMAKE_GENERATOR)
        if (CMAKE_GENERATOR_TOOLSET)
            list(APPEND args -T "${CMAKE_GENERATOR_TOOLSET}")
        endif ()

        if (CMAKE_MAKE_PROGRAM)
            list(APPEND args "-DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}")
        endif ()
    endif ()

    get_epm_consumer_is_native(native)

    # The linker type too: eacp's Release IPO needs a linker that reads the
    # compiler's LTO objects (lld for clang on Linux).
    if (native)
        foreach (variable CMAKE_C_COMPILER CMAKE_CXX_COMPILER CMAKE_LINKER_TYPE)
            if (${variable})
                list(APPEND args "-D${variable}=${${variable}}")
            endif ()
        endforeach ()
    endif ()

    # A launcher (ccache) runs on the host whatever the target. One with
    # arguments (a list) would not survive the argument lists below.
    foreach (variable CMAKE_C_COMPILER_LAUNCHER CMAKE_CXX_COMPILER_LAUNCHER)
        if (${variable} AND NOT "${${variable}}" MATCHES ";")
            list(APPEND args "-D${variable}=${${variable}}")
        endif ()
    endforeach ()

    set(toolchain_args "${args}" PARENT_SCOPE)
    set(build_name "${name}" PARENT_SCOPE)
endfunction()

# Configures, builds and installs the epm source at `source_dir` into
# `tool_dir`, Debug and then Release (so bin/epm is the Release one): one build
# directory for a multi-config generator, else <build_name>-debug and
# <build_name> (the Release one keeps the name a Release-only build used). A
# build directory is configured only the first time, so a later call is an
# incremental build; the compilers of that first configure stay (delete the
# entry to change them). Each step of a first build prints a status line,
# with `what` naming the epm being built. Sets build_dirs, the build
# directories used.
function(get_epm_build_configs source_dir tool_dir log what)
    get_epm_consumer_toolchain()
    get_epm_is_multi_config("${tool_generator}" multi_config)
    set(common_args -S "${source_dir}" ${toolchain_args} -DEPM_ENABLE_TESTS=OFF
            "-DCMAKE_INSTALL_PREFIX=${tool_dir}")

    # The Debug and Release builds share one download of epm's dependencies.
    if ("$ENV{CPM_SOURCE_CACHE}" STREQUAL "")
        list(APPEND common_args "-DCPM_SOURCE_CACHE=${tool_dir}/cpm")
    endif ()

    set(cold FALSE)
    set(dirs "")

    foreach (config IN ITEMS Debug Release)
        if (multi_config)
            set(build_dir "${tool_dir}/${build_name}")
            set(configure_args "")
            set(configure_text "configuring")
        elseif (config STREQUAL "Debug")
            set(build_dir "${tool_dir}/${build_name}-debug")
            set(configure_args -DCMAKE_BUILD_TYPE=Debug)
            set(configure_text "configuring Debug")
        else ()
            set(build_dir "${tool_dir}/${build_name}")
            set(configure_args -DCMAKE_BUILD_TYPE=Release)
            set(configure_text "configuring Release")
        endif ()

        # Written after a configure succeeds: a cache left by a failed one is
        # redone.
        set(configured "${build_dir}/epm-configured")

        if (NOT EXISTS "${configured}")
            if (NOT cold)
                message(STATUS "epm: building ${what} into ${tool_dir} (Debug and "
                        "Release; first time only)")
                set(cold TRUE)
            endif ()

            message(STATUS "epm: ${what}: ${configure_text}")
            file(REMOVE_RECURSE "${build_dir}")
            file(MAKE_DIRECTORY "${build_dir}")
            get_epm_run_step("configure (${config})" "${log}"
                    "${CMAKE_COMMAND}" ${common_args} -B "${build_dir}"
                    ${configure_args})
            file(WRITE "${configured}" "")
        endif ()

        foreach (step IN ITEMS build install)
            if (step STREQUAL "build")
                set(text "building ${config}")
                set(command "${CMAKE_COMMAND}" --build "${build_dir}"
                        --config ${config} --parallel)
            else ()
                set(text "installing ${config}")
                set(command "${CMAKE_COMMAND}" --install "${build_dir}"
                        --config ${config} --prefix "${tool_dir}")
            endif ()

            if (cold)
                message(STATUS "epm: ${what}: ${text}")
            else ()
                get_epm_note("${what}: ${text}")
            endif ()

            get_epm_run_step("${step} (${config})" "${log}" ${command})
        endforeach ()

        list(APPEND dirs "${build_dir}")
    endforeach ()

    list(REMOVE_DUPLICATES dirs)
    set(build_dirs "${dirs}" PARENT_SCOPE)
endfunction()

# Fetches epm `ref` into `source_dir`: with git from the repository, else (or
# for EPM_SOURCE_URL, `url`) a source tarball. Sets get_epm_error (empty on
# success).
function(get_epm_fetch_source cache ref url source_dir log)
    get_epm_repository(repository)
    get_epm_find_git(git)
    file(REMOVE_RECURSE "${source_dir}")

    if (url STREQUAL "" AND git STREQUAL "" AND repository MATCHES "^https://github.com/")
        set(url "${repository}/archive/${ref}.tar.gz")
    endif ()

    if (url STREQUAL "" AND git STREQUAL "")
        set(get_epm_error "no git to fetch epm ${ref} from ${repository} with"
                PARENT_SCOPE)
        return()
    endif ()

    if (url STREQUAL "")
        message(STATUS "epm: fetching epm ${ref} from ${repository}")
        file(MAKE_DIRECTORY "${source_dir}")
        set(git_env "${CMAKE_COMMAND}" -E env GIT_TERMINAL_PROMPT=0 "${git}"
                -c advice.detachedHead=false)

        foreach (arguments IN ITEMS
                "init;-q"
                "fetch;-q;--depth;1;${repository};${ref}"
                "checkout;-q;--detach;FETCH_HEAD")
            string(REPLACE ";" " " shown "${arguments}")
            file(APPEND "${log}" "\n$ git ${shown}\n")
            execute_process(COMMAND ${git_env} ${arguments}
                    WORKING_DIRECTORY "${source_dir}"
                    RESULT_VARIABLE result
                    OUTPUT_VARIABLE output
                    ERROR_VARIABLE output)
            file(APPEND "${log}" "${output}")

            if (NOT result EQUAL 0)
                string(STRIP "${output}" output)
                set(get_epm_error "git could not fetch epm ${ref} from "
                        "${repository}: ${output}")
                string(JOIN "" get_epm_error ${get_epm_error})
                set(get_epm_error "${get_epm_error}" PARENT_SCOPE)
                return()
            endif ()
        endforeach ()

        set(get_epm_error "" PARENT_SCOPE)
        return()
    endif ()

    message(STATUS "epm: downloading ${url}")
    string(RANDOM LENGTH 12 ALPHABET "0123456789abcdef" id)
    set(staging "${cache}/tmp/epm-source-${id}")
    file(DOWNLOAD "${url}" "${staging}/epm.tar.gz" STATUS status TLS_VERIFY ON)
    list(GET status 0 code)
    list(GET status 1 text)
    set(extracted "")

    if (code EQUAL 0)
        file(ARCHIVE_EXTRACT INPUT "${staging}/epm.tar.gz"
                DESTINATION "${staging}/extract")
        file(GLOB extracted LIST_DIRECTORIES true "${staging}/extract/*")
    endif ()

    list(LENGTH extracted count)

    if (NOT count EQUAL 1 OR NOT EXISTS "${extracted}/CMakeLists.txt")
        file(REMOVE_RECURSE "${staging}")
        set(get_epm_error "no epm source at ${url} (${text})" PARENT_SCOPE)
        return()
    endif ()

    get_filename_component(parent "${source_dir}" DIRECTORY)
    file(MAKE_DIRECTORY "${parent}")
    file(RENAME "${extracted}" "${source_dir}")
    file(REMOVE_RECURSE "${staging}")
    set(get_epm_error "" PARENT_SCOPE)
endfunction()

# Removes the build directories (and the dependency downloads) of a finished
# tool entry, except any the installed epm still names in its recipe SDK (an
# epm whose recipes compile against its build tree).
function(get_epm_prune tool_dir)
    set(removable ${ARGN} "${tool_dir}/cpm")
    set(sdk_file "${tool_dir}/recipe-sdk-check.cmake")
    execute_process(COMMAND "${tool_dir}/bin/epm${GET_EPM_SUFFIX}" recipe-sdk
            --out "${sdk_file}"
            RESULT_VARIABLE result OUTPUT_QUIET ERROR_QUIET TIMEOUT 60)
    set(sdk "")

    if (result EQUAL 0 AND EXISTS "${sdk_file}")
        file(READ "${sdk_file}" sdk)
    endif ()

    file(REMOVE "${sdk_file}")

    foreach (dir IN LISTS removable)
        set(spellings "${dir}")

        if (EXISTS "${dir}")
            file(REAL_PATH "${dir}" real)
            list(APPEND spellings "${real}")
        endif ()

        set(used FALSE)

        foreach (spelling IN LISTS spellings)
            string(FIND "${sdk}" "${spelling}/" at)

            if (NOT at EQUAL -1)
                set(used TRUE)
            endif ()
        endforeach ()

        if (used)
            get_epm_note("keeping ${dir}: the installed epm's recipe SDK names it")
        else ()
            file(REMOVE_RECURSE "${dir}")
        endif ()
    endforeach ()
endfunction()

# Step 4: fetches epm `ref` (or the tarball `url`) into <tool_dir>/src, builds
# and installs it into `tool_dir`, then writes the marker. Sets get_epm_error
# when the source cannot be had; a failed build is fatal. The old marker goes
# first: step 3 reads markers without the lock, and a half-built entry already
# has bin/epm and epm.cmake once Debug is installed.
function(get_epm_build cache tool_dir ref url description)
    file(REMOVE "${tool_dir}.json")
    file(REMOVE_RECURSE "${tool_dir}")
    file(MAKE_DIRECTORY "${tool_dir}" "${cache}/tmp")
    set(log "${tool_dir}/epm-bootstrap.log")
    file(WRITE "${log}" "epm bootstrap of ${description}\n")
    get_epm_fetch_source("${cache}" "${ref}" "${url}" "${tool_dir}/src" "${log}")

    if (NOT get_epm_error STREQUAL "")
        file(REMOVE_RECURSE "${tool_dir}")
        set(get_epm_error "${get_epm_error}" PARENT_SCOPE)
        return()
    endif ()

    get_epm_build_configs("${tool_dir}/src" "${tool_dir}" "${log}" "${description}")
    get_epm_version("${tool_dir}/bin/epm${GET_EPM_SUFFIX}" version)

    if (version STREQUAL "")
        message(FATAL_ERROR "epm: the freshly built ${tool_dir}/bin/epm does not run."
                "\nFull log: ${log}")
    endif ()

    get_epm_prune("${tool_dir}" ${build_dirs})

    if (url STREQUAL "")
        get_epm_repository(repository)
        set(url "${repository}@${ref}")
    endif ()

    get_epm_write_marker("${tool_dir}" "${ref}" source "${url}" "${version}")
    set(get_epm_error "" PARENT_SCOPE)
    set(get_epm_message "built epm ${version} into ${tool_dir}" PARENT_SCOPE)
endfunction()

# Step 4 under file(LOCK <cache>/tool/<key>.lock): a parallel configure waits,
# then finds the entry the first one finished (its marker written).
function(get_epm_install cache key ref url description)
    set(tool_dir "${cache}/tool/${key}")
    file(MAKE_DIRECTORY "${cache}/tool")
    file(LOCK "${tool_dir}.lock" GUARD FUNCTION TIMEOUT 3600 RESULT_VARIABLE locked)

    if (NOT locked EQUAL 0)
        message(FATAL_ERROR "epm: cannot lock ${tool_dir}.lock: ${locked}")
    endif ()

    set(get_epm_error "")
    set(version_error "no marker")

    if (EXISTS "${tool_dir}.json")
        file(READ "${tool_dir}.json" json)
        string(JSON version ERROR_VARIABLE version_error GET "${json}" version)
    endif ()

    if (NOT version_error AND EXISTS "${tool_dir}/bin/epm${GET_EPM_SUFFIX}"
            AND EXISTS "${tool_dir}/share/epm/cmake/epm.cmake")
        set(get_epm_message "using epm ${version} from ${tool_dir}/bin/epm${GET_EPM_SUFFIX}")
    else ()
        get_epm_build("${cache}" "${tool_dir}" "${ref}" "${url}" "${description}")
    endif ()

    set(get_epm_error "${get_epm_error}" PARENT_SCOPE)
    set(get_epm_message "${get_epm_message}" PARENT_SCOPE)
endfunction()

# Checks that `dir` is an epm checkout: a CMakeLists.txt with project(epm. Sets
# source_dir (absolute, symlinks resolved) and source_error (empty when it is
# one).
function(get_epm_check_source_dir dir)
    get_filename_component(absolute "${dir}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
    set(source_dir "")
    set(error "")

    if (NOT IS_DIRECTORY "${absolute}")
        set(error "EPM_SOURCE_DIR '${dir}' is not a directory")
    else ()
        file(REAL_PATH "${absolute}" source_dir)
        set(lists "${source_dir}/CMakeLists.txt")
        set(project_epm "^[ \t]*[Pp][Rr][Oo][Jj][Ee][Cc][Tt][ \t]*\\([ \t]*epm([ \t)]|$)")

        if (NOT EXISTS "${lists}")
            set(error "EPM_SOURCE_DIR '${source_dir}' is not an epm checkout "
                    "(no CMakeLists.txt)")
        else ()
            file(STRINGS "${lists}" project_lines REGEX "${project_epm}")

            if (project_lines STREQUAL "")
                set(error "EPM_SOURCE_DIR '${source_dir}' is not an epm checkout "
                        "(its CMakeLists.txt has no project(epm ...))")
            endif ()
        endif ()
    endif ()

    string(JOIN "" error ${error})
    set(source_dir "${source_dir}" PARENT_SCOPE)
    set(source_error "${error}" PARENT_SCOPE)
endfunction()

# Whether EPM_EXECUTABLE is one the user gave: not the one an earlier configure
# picked, nor one the pre-get_epm epm.cmake bootstrapped.
function(get_epm_user_executable out_var)
    if (EPM_EXECUTABLE AND NOT EPM_EXECUTABLE STREQUAL "${EPM_SELECTED_EXECUTABLE}"
            AND NOT EPM_EXECUTABLE STREQUAL "${EPM_BOOTSTRAPPED_EXECUTABLE}")
        set(${out_var} TRUE PARENT_SCOPE)
    else ()
        set(${out_var} FALSE PARENT_SCOPE)
    endif ()
endfunction()

# The epm checkout to build, empty for none: the EPM_SOURCE_DIR CMake variable,
# else the environment variable, else the deprecated EPM_BOOTSTRAP_SOURCE_DIR,
# else the checkout this file belongs to (<checkout>/CMake/get_epm.cmake) when
# nothing else asks for a particular epm. Sets `out_var` and `${out_var}_origin`
# (variable, environment, deprecated, enclosing or empty).
function(get_epm_source_dir_setting out_var)
    set(setting "")
    set(origin "")

    if (NOT "${EPM_SOURCE_DIR}" STREQUAL "")
        set(setting "${EPM_SOURCE_DIR}")
        set(origin variable)
    elseif (NOT "$ENV{EPM_SOURCE_DIR}" STREQUAL "")
        set(setting "$ENV{EPM_SOURCE_DIR}")
        set(origin environment)
    elseif (NOT "${EPM_BOOTSTRAP_SOURCE_DIR}" STREQUAL "")
        set(setting "${EPM_BOOTSTRAP_SOURCE_DIR}")
        set(origin deprecated)
    else ()
        get_epm_user_executable(user_executable)

        if (NOT user_executable AND NOT EPM_SOURCE_REF AND NOT EPM_SOURCE_URL)
            get_epm_check_source_dir("${GET_EPM_DIR}/..")

            if (source_error STREQUAL "")
                set(setting "${source_dir}")
                set(origin enclosing)
            endif ()
        endif ()
    endif ()

    set(${out_var} "${setting}" PARENT_SCOPE)
    set(${out_var}_origin "${origin}" PARENT_SCOPE)
endfunction()

# The tool entry of a checkout: <cache>/tool/local-<sha256(source_dir)[:12]>.
function(get_epm_source_entry cache source_dir out_var)
    string(SHA256 hash "${source_dir}")
    string(SUBSTRING "${hash}" 0 12 hash)
    set(${out_var} "${cache}/tool/local-${hash}" PARENT_SCOPE)
endfunction()

# The EPM_SOURCE_DIR step: builds the checkout `source_dir` and installs it into
# its entry, under the entry's lock, at every configure (incrementally after
# the first). A failed build is fatal. Sets checkout_dir (the entry) and
# checkout_version.
function(get_epm_build_checkout cache source_dir)
    get_epm_source_entry("${cache}" "${source_dir}" tool_dir)
    get_filename_component(key "${tool_dir}" NAME)
    file(MAKE_DIRECTORY "${tool_dir}")
    file(LOCK "${tool_dir}.lock" GUARD FUNCTION TIMEOUT 3600 RESULT_VARIABLE locked)

    if (NOT locked EQUAL 0)
        message(FATAL_ERROR "epm: cannot lock ${tool_dir}.lock: ${locked}")
    endif ()

    set(log "${tool_dir}/epm-bootstrap.log")
    file(WRITE "${log}" "epm build of ${source_dir}\n")
    get_epm_build_configs("${source_dir}" "${tool_dir}" "${log}"
            "epm from ${source_dir}")
    get_epm_version("${tool_dir}/bin/epm${GET_EPM_SUFFIX}" version)

    if (version STREQUAL "")
        message(FATAL_ERROR "epm: the epm built from ${source_dir} "
                "(${tool_dir}/bin/epm${GET_EPM_SUFFIX}) does not run.\nLog: ${log}")
    endif ()

    get_epm_write_marker("${tool_dir}" "${key}" source "${source_dir}" "${version}")
    set(checkout_dir "${tool_dir}" PARENT_SCOPE)
    set(checkout_version "${version}" PARENT_SCOPE)
endfunction()

# Hands the choice to the file scope of get_epm.cmake. `remember` is OFF for a
# binary the user gave.
macro(get_epm_found executable module remember text)
    message(STATUS "epm: ${text}")
    set(get_epm_executable "${executable}" PARENT_SCOPE)
    set(get_epm_module "${module}" PARENT_SCOPE)
    set(get_epm_remember ${remember} PARENT_SCOPE)
endmacro()

function(get_epm_select)
    get_epm_cache_root(cache)
    get_epm_exact_lower_bound("${EPM_VERSION}" exact_min)
    get_epm_repository(repository)
    set(cached_dir "")

    # 1. A binary the user gave. Its module: the one installed beside it, else
    # the one beside this file (true inside the epm repository).
    get_epm_user_executable(user_executable)

    if (user_executable)
        get_epm_version("${EPM_EXECUTABLE}" version)
        epm_version_in_range("${version}" "${EPM_VERSION}" in_range)
        get_epm_module_beside("${EPM_EXECUTABLE}" module)

        if (module STREQUAL "" AND EXISTS "${GET_EPM_DIR}/epm.cmake")
            set(module "${GET_EPM_DIR}/epm.cmake")
        endif ()

        if (in_range AND NOT module STREQUAL "")
            get_epm_found("${EPM_EXECUTABLE}" "${module}" OFF
                    "using epm ${version} from ${EPM_EXECUTABLE}")
            return()
        endif ()

        message(WARNING "epm: not using EPM_EXECUTABLE '${EPM_EXECUTABLE}' (version "
                "'${version}', EPM_VERSION '${EPM_VERSION}', module '${module}')")
    endif ()

    # An epm checkout, rebuilt (incrementally) at every configure. Unreleased by
    # nature, so its version is reported rather than checked.
    get_epm_source_dir_setting(source_setting)

    if (source_setting_origin STREQUAL "deprecated")
        message(STATUS "epm: EPM_BOOTSTRAP_SOURCE_DIR is deprecated; "
                "using it as EPM_SOURCE_DIR")
    elseif (source_setting_origin STREQUAL "enclosing")
        message(STATUS "epm: get_epm.cmake is inside the epm checkout "
                "${source_setting}; building epm from it (-DEPM_EXECUTABLE or "
                "-DEPM_SOURCE_DIR to use another)")
    endif ()

    if (NOT source_setting STREQUAL "")
        get_epm_check_source_dir("${source_setting}")

        if (NOT source_error STREQUAL "")
            message(FATAL_ERROR "epm: ${source_error}")
        endif ()

        get_epm_build_checkout("${cache}" "${source_dir}")
        set(text "using epm ${checkout_version} built from ${source_dir}")

        if (NOT "${EPM_VERSION}" STREQUAL "")
            string(APPEND text " (EPM_VERSION '${EPM_VERSION}' not checked)")
        endif ()

        get_epm_found("${checkout_dir}/bin/epm${GET_EPM_SUFFIX}"
                "${checkout_dir}/share/epm/cmake/epm.cmake" ON "${text}")
        return()
    endif ()

    if (EPM_SOURCE_REF)
        set(ref "${EPM_SOURCE_REF}")
    elseif (EPM_SOURCE_URL)
        set(ref "url")
    else ()
        # 2. epm on PATH (and only PATH: never re-rooted by a cross toolchain).
        find_program(path_epm NAMES epm NO_CACHE NO_PACKAGE_ROOT_PATH NO_CMAKE_PATH
                NO_CMAKE_ENVIRONMENT_PATH NO_CMAKE_SYSTEM_PATH NO_CMAKE_INSTALL_PREFIX
                NO_CMAKE_FIND_ROOT_PATH)

        if (path_epm)
            get_epm_version("${path_epm}" version)
            epm_version_in_range("${version}" "${EPM_VERSION}" in_range)
            get_epm_module_beside("${path_epm}" module)

            if (in_range AND NOT module STREQUAL "")
                get_epm_found("${path_epm}" "${module}" ON
                        "using epm ${version} from ${path_epm}")
                return()
            endif ()

            get_epm_note("skipping ${path_epm}: version '${version}', "
                    "module '${module}'")
        endif ()

        # 3. A cached entry, unless a newer release is in range.
        get_epm_find_cached("${cache}")
        get_epm_release_versions("${cache}" versions)
        get_epm_newest_in_range("${versions}" "${EPM_VERSION}" newest)

        if (NOT cached_dir STREQUAL "" AND (newest STREQUAL ""
                OR NOT newest VERSION_GREATER cached_version))
            get_epm_found("${cached_dir}/bin/epm${GET_EPM_SUFFIX}"
                    "${cached_dir}/share/epm/cmake/epm.cmake" ON
                    "using epm ${cached_version} from ${cached_dir}/bin/epm${GET_EPM_SUFFIX}")
            return()
        endif ()

        # 4. The newest release in range, else (no listing) the lower bound,
        # else main.
        if (NOT newest STREQUAL "")
            set(ref "v${newest}")
        elseif (versions_known AND NOT "${EPM_VERSION}" STREQUAL "")
            list(JOIN versions ", " listed)
            message(FATAL_ERROR "epm: no release of ${repository} is in EPM_VERSION "
                    "'${EPM_VERSION}' (releases: ${listed}). Configure with "
                    "-DEPM_EXECUTABLE=/path/to/epm, -DEPM_SOURCE_REF=<ref> or "
                    "another EPM_VERSION.")
        elseif (NOT exact_min STREQUAL "")
            set(ref "v${exact_min}")
        else ()
            set(ref "main")
        endif ()
    endif ()

    string(REGEX REPLACE "[^A-Za-z0-9._-]" "_" key "${ref}")
    set(url "")
    set(description "epm ${ref}")

    if (EPM_SOURCE_URL)
        set(url "${EPM_SOURCE_URL}")
        set(description "epm from ${EPM_SOURCE_URL}")
        string(SHA256 url_hash "${EPM_SOURCE_URL}")
        string(SUBSTRING "${url_hash}" 0 12 url_hash)
        string(APPEND key "-${url_hash}")
    endif ()

    get_epm_install("${cache}" "${key}" "${ref}" "${url}" "${description}")

    if (NOT get_epm_error STREQUAL "")
        if (NOT cached_dir STREQUAL "")
            get_epm_found("${cached_dir}/bin/epm${GET_EPM_SUFFIX}"
                    "${cached_dir}/share/epm/cmake/epm.cmake" ON
                    "${get_epm_error}; using epm ${cached_version} from the cache")
            return()
        endif ()

        message(FATAL_ERROR "epm: no epm binary: ${get_epm_error}\n"
                "Configure with -DEPM_EXECUTABLE=/path/to/epm or put an installed "
                "epm on PATH.")
    endif ()

    get_epm_found("${cache}/tool/${key}/bin/epm${GET_EPM_SUFFIX}"
            "${cache}/tool/${key}/share/epm/cmake/epm.cmake" ON "${get_epm_message}")
endfunction()

# Whether this file is being included by the first project(), from
# CMAKE_PROJECT_TOP_LEVEL_INCLUDES (whose relative entries CMake resolves
# against the top-level source directory), rather than by an include().
function(get_epm_is_top_level_include out_var)
    set(listed FALSE)
    file(REAL_PATH "${GET_EPM_FILE}" this_file)

    foreach (entry IN LISTS CMAKE_PROJECT_TOP_LEVEL_INCLUDES)
        file(REAL_PATH "${entry}" entry BASE_DIRECTORY "${CMAKE_SOURCE_DIR}")

        if (entry STREQUAL this_file)
            set(listed TRUE)
        endif ()
    endforeach ()

    if (listed AND DEFINED PROJECT_NAME
            AND CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
        set(${out_var} TRUE PARENT_SCOPE)
    else ()
        set(${out_var} FALSE PARENT_SCOPE)
    endif ()
endfunction()

# Tests/GetEpmRanges.cmake wants the functions only. A child build epm runs
# (Docs/Graph.md §10) has the provider injected by its parent and needs no
# binary: bootstrapping one there could find no module beside an EPM_EXECUTABLE
# in a build tree and go on to build an epm.
if (GET_EPM_FUNCTIONS_ONLY OR EPM_NESTED)
    return()
endif ()

get_epm_select()

if (get_epm_remember)
    # Remembered as picked here, so the next configure picks afresh instead of
    # treating it as a binary the user gave.
    set(EPM_SELECTED_EXECUTABLE "${get_epm_executable}" CACHE INTERNAL "")
    set(EPM_EXECUTABLE "${get_epm_executable}" CACHE INTERNAL "Path to the epm binary")
    set(EPM_EXECUTABLE "${get_epm_executable}")
    unset(EPM_BOOTSTRAPPED_EXECUTABLE CACHE)
endif ()

# -DEPM_UPDATE=ON is a one-off request, not a setting.
unset(EPM_UPDATE CACHE)

include("${get_epm_module}")

# The manifest flow (Docs/Graph.md §8). A project without epm.cpp may still
# call epm_add_package.
if (NOT EXISTS "${CMAKE_SOURCE_DIR}/epm.cpp")
    return()
endif ()

get_epm_is_top_level_include(get_epm_top_level)

if (NOT get_epm_top_level)
    message(FATAL_ERROR "epm: ${CMAKE_SOURCE_DIR}/epm.cpp needs ${GET_EPM_FILE} "
            "listed in CMAKE_PROJECT_TOP_LEVEL_INCLUDES before project(), not "
            "include()d:\n  list(APPEND CMAKE_PROJECT_TOP_LEVEL_INCLUDES "
            "path/to/get_epm.cmake)")
endif ()

if (NOT COMMAND epm_use_manifest)
    message(FATAL_ERROR "epm: the epm at ${EPM_EXECUTABLE} predates "
            "manifests (its module ${get_epm_module} has no epm_use_manifest). Use "
            "a newer one: -DEPM_EXECUTABLE=/path/to/epm, -DEPM_SOURCE_DIR=/path/to/"
            "epm-checkout (built from source), or EPM_VERSION once a release with "
            "manifests exists.")
endif ()

epm_use_manifest("${CMAKE_SOURCE_DIR}/epm.cpp")
