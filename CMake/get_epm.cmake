# get_epm.cmake -- the one file a project vendors to use epm.
#
# With a manifest: list this file in
# CMAKE_PROJECT_TOP_LEVEL_INCLUDES before project(), and keep epm.json beside
# the top-level CMakeLists.txt:
#
#   cmake_minimum_required(VERSION 3.25)
#   set(EPM_VERSION "0.2...<0.3")    # optional; default: the latest release
#   list(APPEND CMAKE_PROJECT_TOP_LEVEL_INCLUDES cmake/get_epm.cmake)
#   project(App CXX)
#   find_package(fmt REQUIRED)
#
# The first project() then includes this file, which bootstraps the binary (below)
# and registers epm's dependency provider. At the first find_package() or
# FetchContent_MakeAvailable(), the provider writes ${CMAKE_BINARY_DIR}/epm/
# env.json and overrides.json, runs `epm configure` over epm.json and loads each
# package the graph names, dependencies first; other names are left to CMake.
# With EPM_NESTED set (a child build epm runs) this file does nothing at all;
# without an epm.json, nothing is registered.
#
# Without a manifest, the older form still works, for epm_add_package only:
#
#   include(cmake/get_epm.cmake)     # after project()
#   epm_add_package(NAME ... GITHUB_REPOSITORY ... GIT_TAG ...)
#
# Either way it finds an epm binary, then includes the epm.cmake installed with
# that binary, so module and binary always match. First in-range match wins:
#
#   1. EPM_EXECUTABLE, when set.
#      Then EPM_SOURCE_DIR, when set (skips 2-5): that epm checkout, built and
#      installed into <cache>/tool/local-<sha256(real path)[:12]>/ with the
#      consumer's generator (and its compilers, when known and native). The
#      build directory stays in the entry, so later configures are incremental
#      `cmake --build` runs. Its version is printed, not checked against
#      EPM_VERSION. For testing an unreleased epm from a consumer.
#      Else, when this file is <dir>/CMake/get_epm.cmake of an epm checkout
#      (<dir>/CMakeLists.txt has project(epm), as for the examples in the epm
#      repository) and no EPM_EXECUTABLE, EPM_SOURCE_REF or EPM_SOURCE_URL is
#      given: EPM_SOURCE_DIR defaults to <dir>. A copy vendored into a consumer
#      never matches.
#   2. epm on PATH, with <prefix>/share/epm/cmake/epm.cmake beside it.
#   3. The newest entry an earlier configure left in <cache>/tool/v<version>/,
#      unless the latest release (4) is newer and also in range.
#   4. The latest release, from releases/latest/download/manifest.json (checked at
#      most once a day, cached in <cache>/tool/latest.json; EPM_UPDATE=ON checks
#      now). When the latest is out of range and EPM_VERSION has an exact lower
#      bound x.y.z, release vx.y.z is tried instead; no other release is. The
#      archive for this host is checked against the manifest's SHA-256 and
#      unpacked into <cache>/tool/<tag>/.
#   5. A source build of that release (else of the lower bound, else of main),
#      installed into <cache>/tool/<ref>/.
#
# A tool entry is exactly what `cmake --install` of epm produces (bin/epm and
# share/epm/cmake/epm.cmake with epm-provider.cmake), whether unpacked from a
# release or built.
#
# Variables read, all optional (set before the include, or with -D):
#   EPM_VERSION               "0.1.2" (meaning >= 0.1.2, as in find_package),
#                             "0.1...<0.2" or "0.1...0.2"; unset: any version
#   EPM_EXECUTABLE            an epm binary to try first
#   EPM_SOURCE_DIR            an epm checkout to build and use (else the
#                             EPM_SOURCE_DIR environment variable; a relative
#                             path is relative to the top-level source dir)
#   EPM_UPDATE                ON: look for a newer release now (a -D value is
#                             cleared from the cache afterwards)
#   EPM_BOOTSTRAP_NO_DOWNLOAD ON: never download a release binary (skips 4)
#   EPM_SOURCE_REF            build this epm tag, branch or commit (skips 2-4)
#   EPM_SOURCE_URL            build this source tarball (skips 2-4)
#   EPM_RELEASE_REPOSITORY    the GitHub repository whose releases are used
#                             (default https://github.com/eyalamirmusic/epm)
#   EPM_CACHE                cache root (else the EPM_CACHE environment
#                             variable, else the platform cache directory)
#   EPM_VERBOSE               say what is tried; stream the build output
#   EPM_NESTED                set by epm for its child builds: do nothing
#
# The binary ends up in EPM_EXECUTABLE (cache INTERNAL).

include_guard(GLOBAL)

set(GET_EPM_REPOSITORY "https://github.com/eyalamirmusic/epm")

if (EPM_RELEASE_REPOSITORY)
    set(GET_EPM_REPOSITORY "${EPM_RELEASE_REPOSITORY}")
endif ()
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

# The release asset key of the machine running CMake: macos-universal,
# linux-x64, linux-arm64, windows-x64 or windows-arm64.
function(get_epm_host_platform out_var)
    if (CMAKE_HOST_APPLE)
        set(${out_var} "macos-universal" PARENT_SCOPE)
        return()
    elseif (CMAKE_HOST_WIN32)
        set(os "windows")
    else ()
        string(TOLOWER "${CMAKE_HOST_SYSTEM_NAME}" os)
    endif ()

    string(TOLOWER "${CMAKE_HOST_SYSTEM_PROCESSOR}" arch)

    if (arch MATCHES "^(amd64|x86_64)$")
        set(arch "x64")
    elseif (arch STREQUAL "aarch64")
        set(arch "arm64")
    endif ()

    set(${out_var} "${os}-${arch}" PARENT_SCOPE)
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

# Downloads a release's manifest.json. Empty on any failure (offline, 404, junk).
function(get_epm_fetch_manifest url cache out_var)
    string(RANDOM LENGTH 12 ALPHABET "0123456789abcdef" id)
    set(file "${cache}/tmp/manifest-${id}.json")
    get_epm_note("reading ${url}")
    file(DOWNLOAD "${url}" "${file}" STATUS status TLS_VERIFY ON
            TIMEOUT 60 INACTIVITY_TIMEOUT 15)
    list(GET status 0 code)
    set(manifest "")

    if (code EQUAL 0)
        file(READ "${file}" manifest)
        string(JSON tag ERROR_VARIABLE tag_error GET "${manifest}" tag)
        string(JSON version ERROR_VARIABLE version_error GET "${manifest}" version)

        if (tag_error OR version_error)
            get_epm_note("${url} is not a release manifest")
            set(manifest "")
        endif ()
    else ()
        list(GET status 1 text)
        get_epm_note("reading ${url} failed: ${text}")
    endif ()

    file(REMOVE "${file}")
    set(${out_var} "${manifest}" PARENT_SCOPE)
endfunction()

# Step 4's manifest: the saved copy when it is under a day old (and EPM_UPDATE is
# off), else a fresh one, else the stale copy. Empty when there is none.
function(get_epm_latest_manifest cache out_var)
    set(saved "${cache}/tool/latest.json")
    string(TIMESTAMP now "%s" UTC)
    set(manifest "")
    set(age -1)

    if (EXISTS "${saved}")
        file(READ "${saved}" json)
        string(JSON checked_at ERROR_VARIABLE checked_error GET "${json}" checkedAt)
        string(JSON manifest ERROR_VARIABLE manifest_error GET "${json}" manifest)

        if (checked_error OR manifest_error)
            set(manifest "")
        else ()
            math(EXPR age "${now} - ${checked_at}")
        endif ()
    endif ()

    if (NOT manifest STREQUAL "" AND NOT EPM_UPDATE
            AND age GREATER_EQUAL 0 AND age LESS 86400)
        get_epm_note("latest release checked ${age} s ago")
        set(${out_var} "${manifest}" PARENT_SCOPE)
        return()
    endif ()

    get_epm_fetch_manifest(
            "${GET_EPM_REPOSITORY}/releases/latest/download/manifest.json"
            "${cache}" fetched)

    if (NOT fetched STREQUAL "")
        set(json "{}")
        string(JSON json SET "${json}" schema 1)
        string(JSON json SET "${json}" checkedAt ${now})
        string(JSON json SET "${json}" manifest "${fetched}")
        string(RANDOM LENGTH 12 ALPHABET "0123456789abcdef" id)
        file(WRITE "${saved}.${id}.tmp" "${json}\n")
        file(RENAME "${saved}.${id}.tmp" "${saved}")
        set(manifest "${fetched}")
    elseif (NOT manifest STREQUAL "")
        get_epm_note("using the latest release as seen ${age} s ago")
    endif ()

    set(${out_var} "${manifest}" PARENT_SCOPE)
endfunction()

# Step 4: downloads this host's archive of the release `manifest` describes,
# checks its SHA-256, unpacks it into `tool_dir` and checks that it runs. Sets
# get_epm_error (empty on success) and get_epm_message.
function(get_epm_download cache tool_dir manifest)
    string(JSON version GET "${manifest}" version)
    string(JSON tag GET "${manifest}" tag)
    get_epm_host_platform(platform)
    string(JSON asset ERROR_VARIABLE asset_error GET "${manifest}" assets ${platform} file)
    string(JSON expected ERROR_VARIABLE hash_error GET "${manifest}" assets ${platform} sha256)

    if (asset_error OR hash_error)
        set(get_epm_error "release ${tag} has no ${platform} binary" PARENT_SCOPE)
        return()
    endif ()

    set(url "${GET_EPM_REPOSITORY}/releases/download/${tag}/${asset}")
    string(RANDOM LENGTH 12 ALPHABET "0123456789abcdef" id)
    set(staging "${cache}/tmp/epm-release-${id}")
    get_epm_note("downloading ${url}")
    file(DOWNLOAD "${url}" "${staging}/${asset}" STATUS status TLS_VERIFY ON)
    list(GET status 0 code)
    list(GET status 1 text)

    if (NOT code EQUAL 0)
        file(REMOVE_RECURSE "${staging}")
        set(get_epm_error "downloading ${url}: ${text}" PARENT_SCOPE)
        return()
    endif ()

    # Compared by hand: an EXPECTED_HASH mismatch would end the configure.
    file(SHA256 "${staging}/${asset}" actual)
    string(TOLOWER "${expected}" expected)

    if (NOT actual STREQUAL expected)
        file(REMOVE_RECURSE "${staging}")
        set(get_epm_error "SHA-256 of ${asset} is ${actual}, expected ${expected}"
                PARENT_SCOPE)
        return()
    endif ()

    file(REMOVE_RECURSE "${tool_dir}")
    file(ARCHIVE_EXTRACT INPUT "${staging}/${asset}" DESTINATION "${tool_dir}")
    file(REMOVE_RECURSE "${staging}")

    # Catches a binary this host cannot run (glibc too old, no libcurl, no VC++
    # runtime).
    get_epm_version("${tool_dir}/bin/epm${GET_EPM_SUFFIX}" actual_version)

    if (NOT actual_version STREQUAL version)
        file(REMOVE_RECURSE "${tool_dir}")
        set(get_epm_error "the ${tag} binary prints version '${actual_version}', "
                "expected ${version}" PARENT_SCOPE)
        return()
    endif ()

    get_epm_write_marker("${tool_dir}" "${tag}" release "${url}" "${version}")
    set(get_epm_error "" PARENT_SCOPE)
    set(get_epm_message "downloaded epm ${version} (${platform}) into ${tool_dir}"
            PARENT_SCOPE)
endfunction()

# Runs one step (configure, build or install) of a source build of epm, the
# command in ARGN, appending its output to `log`. A failure is fatal.
#
# A host build: nothing from the consumer's toolchain, generator or target
# architectures reaches it unless passed on the command line, so it works while
# the consumer targets iOS or Android. CMake reads these variables from the
# environment too.
function(get_epm_run_step step log)
    set(clean_env "${CMAKE_COMMAND}" -E env
            --unset=CMAKE_TOOLCHAIN_FILE
            --unset=CMAKE_GENERATOR
            --unset=CMAKE_GENERATOR_PLATFORM
            --unset=CMAKE_GENERATOR_TOOLSET
            --unset=CMAKE_BUILD_TYPE
            --unset=CMAKE_OSX_ARCHITECTURES
            --unset=CMAKE_INSTALL_PREFIX
            --unset=DESTDIR)
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

# Step 5: builds the source tarball at `source` on the host and installs it into
# `tool_dir` with epm's own install rules. Sets get_epm_error when the source
# cannot be had; a failed build is fatal.
function(get_epm_build cache tool_dir ref source description)
    message(STATUS "epm: building ${description} into ${tool_dir} (first time only)")
    file(REMOVE_RECURSE "${tool_dir}")
    set(build_dir "${tool_dir}/build")
    set(log "${build_dir}/epm-bootstrap.log")
    file(MAKE_DIRECTORY "${build_dir}")
    string(RANDOM LENGTH 12 ALPHABET "0123456789abcdef" id)
    set(staging "${cache}/tmp/epm-bootstrap-${id}")
    set(source_dir "")
    get_epm_note("downloading ${source}")
    file(DOWNLOAD "${source}" "${staging}/epm.tar.gz" STATUS status TLS_VERIFY ON)
    list(GET status 0 code)
    list(GET status 1 text)

    if (code EQUAL 0)
        file(ARCHIVE_EXTRACT INPUT "${staging}/epm.tar.gz"
                DESTINATION "${staging}/extract")
        file(GLOB source_dir LIST_DIRECTORIES true "${staging}/extract/*")
    endif ()

    list(LENGTH source_dir count)

    if (NOT count EQUAL 1 OR NOT EXISTS "${source_dir}/CMakeLists.txt")
        file(REMOVE_RECURSE "${staging}" "${tool_dir}")
        set(get_epm_error "no epm source at ${source} (${text})" PARENT_SCOPE)
        return()
    endif ()

    set(configure "${CMAKE_COMMAND}" -S "${source_dir}" -B "${build_dir}"
            -DCMAKE_BUILD_TYPE=Release -DEPM_ENABLE_TESTS=OFF
            "-DCMAKE_INSTALL_PREFIX=${tool_dir}")
    set(build "${CMAKE_COMMAND}" --build "${build_dir}" --config Release --parallel)
    set(install "${CMAKE_COMMAND}" --install "${build_dir}" --config Release)
    file(WRITE "${log}" "epm bootstrap of ${source}\n")

    foreach (step configure build install)
        get_epm_run_step(${step} "${log}" ${${step}})
    endforeach ()

    get_epm_version("${tool_dir}/bin/epm${GET_EPM_SUFFIX}" version)

    if (version STREQUAL "")
        message(FATAL_ERROR "epm: the freshly built ${tool_dir}/bin/epm does not run."
                "\nFull log: ${log}")
    endif ()

    file(REMOVE_RECURSE "${build_dir}" "${staging}")
    get_epm_write_marker("${tool_dir}" "${ref}" source "${source}" "${version}")
    set(get_epm_error "" PARENT_SCOPE)
    set(get_epm_message "built epm ${version} into ${tool_dir}" PARENT_SCOPE)
endfunction()

# Steps 4 and 5 under file(LOCK <cache>/tool/<key>.lock): a parallel configure
# waits, then finds the entry the first one finished. `kind` is `release`
# (`source` is a manifest) or `source`.
function(get_epm_install cache key kind source description)
    set(tool_dir "${cache}/tool/${key}")
    file(MAKE_DIRECTORY "${cache}/tool" "${cache}/tmp")
    file(LOCK "${tool_dir}.lock" GUARD FUNCTION TIMEOUT 3600 RESULT_VARIABLE locked)

    if (NOT locked EQUAL 0)
        message(FATAL_ERROR "epm: cannot lock ${tool_dir}.lock: ${locked}")
    endif ()

    set(get_epm_error "")

    if (EXISTS "${tool_dir}.json" AND EXISTS "${tool_dir}/bin/epm${GET_EPM_SUFFIX}")
        file(READ "${tool_dir}.json" json)
        string(JSON version GET "${json}" version)
        set(get_epm_message "using epm ${version} from ${tool_dir}/bin/epm${GET_EPM_SUFFIX}")
    elseif (kind STREQUAL "release")
        get_epm_download("${cache}" "${tool_dir}" "${source}")
    else ()
        get_epm_build("${cache}" "${tool_dir}" "${key}" "${source}" "${description}")
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

# Whether EPM_EXECUTABLE is one the user gave, not the one an earlier configure
# picked.
function(get_epm_user_executable out_var)
    if (EPM_EXECUTABLE AND NOT EPM_EXECUTABLE STREQUAL "${EPM_SELECTED_EXECUTABLE}")
        set(${out_var} TRUE PARENT_SCOPE)
    else ()
        set(${out_var} FALSE PARENT_SCOPE)
    endif ()
endfunction()

# The epm checkout to build, empty for none: the EPM_SOURCE_DIR CMake variable,
# else the environment variable, else the checkout this file belongs to (<checkout>/CMake/get_epm.cmake) when
# nothing else asks for a particular epm. Sets `out_var` and `${out_var}_origin`
# (variable, environment, enclosing or empty).
function(get_epm_source_dir_setting out_var)
    set(setting "")
    set(origin "")

    if (NOT "${EPM_SOURCE_DIR}" STREQUAL "")
        set(setting "${EPM_SOURCE_DIR}")
        set(origin variable)
    elseif (NOT "$ENV{EPM_SOURCE_DIR}" STREQUAL "")
        set(setting "$ENV{EPM_SOURCE_DIR}")
        set(origin environment)
    else ()
        get_epm_user_executable(user_executable)

        # epm itself builds with a release, never with the checkout it builds.
        get_filename_component(checkout "${GET_EPM_DIR}/.." REALPATH)
        get_filename_component(project_root "${CMAKE_SOURCE_DIR}" REALPATH)

        if (NOT user_executable AND NOT EPM_SOURCE_REF AND NOT EPM_SOURCE_URL
                AND NOT checkout STREQUAL project_root)
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

# The consumer's generator, make program and compilers, for building a checkout
# (the same toolchain, and Ninja's speed when the consumer uses it). Sets
# toolchain_args and build_name (the build directory: one per generator, since a
# build directory cannot change generator). The generator always runs on the
# host; the compilers are left out when they may be a cross toolchain's: with a
# toolchain file or a foreign CMAKE_SYSTEM_NAME. get_epm, as a top-level include,
# runs before languages are enabled, so the compilers are often not known yet.
function(get_epm_consumer_toolchain)
    set(args "")
    set(name "build")

    if (NOT "${CMAKE_GENERATOR}" STREQUAL "")
        list(APPEND args -G "${CMAKE_GENERATOR}")
        string(TOLOWER "${CMAKE_GENERATOR}" generator)
        string(REGEX REPLACE "[^a-z0-9]+" "-" generator "${generator}")
        string(APPEND name "-${generator}")

        if (CMAKE_GENERATOR_TOOLSET)
            list(APPEND args -T "${CMAKE_GENERATOR_TOOLSET}")
        endif ()

        if (CMAKE_MAKE_PROGRAM)
            list(APPEND args "-DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}")
        endif ()
    endif ()

    if (NOT CMAKE_CROSSCOMPILING AND NOT CMAKE_TOOLCHAIN_FILE
            AND (NOT CMAKE_SYSTEM_NAME OR CMAKE_SYSTEM_NAME STREQUAL CMAKE_HOST_SYSTEM_NAME))
        foreach (variable CMAKE_C_COMPILER CMAKE_CXX_COMPILER)
            if (${variable})
                list(APPEND args "-D${variable}=${${variable}}")
            endif ()
        endforeach ()
    endif ()

    set(toolchain_args "${args}" PARENT_SCOPE)
    set(build_name "${name}" PARENT_SCOPE)
endfunction()

# The EPM_SOURCE_DIR step: builds the checkout `source_dir` and installs it into
# its entry, under the entry's lock. The build directory stays in the entry, so
# a later configure is an incremental build; it is configured only the first
# time, so the compilers of that configure stay (delete the entry to change
# them). A failed build is fatal. Sets checkout_dir (the entry) and
# checkout_version.
function(get_epm_build_checkout cache source_dir)
    get_epm_source_entry("${cache}" "${source_dir}" tool_dir)
    get_filename_component(key "${tool_dir}" NAME)
    get_epm_consumer_toolchain()
    set(build_dir "${tool_dir}/${build_name}")
    set(log "${build_dir}/epm-bootstrap.log")
    file(MAKE_DIRECTORY "${cache}/tool")
    file(LOCK "${tool_dir}.lock" GUARD FUNCTION TIMEOUT 3600 RESULT_VARIABLE locked)

    if (NOT locked EQUAL 0)
        message(FATAL_ERROR "epm: cannot lock ${tool_dir}.lock: ${locked}")
    endif ()

    set(configure "${CMAKE_COMMAND}" -S "${source_dir}" -B "${build_dir}"
            ${toolchain_args} -DCMAKE_BUILD_TYPE=Release -DEPM_ENABLE_TESTS=OFF
            "-DCMAKE_INSTALL_PREFIX=${tool_dir}")
    set(build "${CMAKE_COMMAND}" --build "${build_dir}" --config Release --parallel)
    set(install "${CMAKE_COMMAND}" --install "${build_dir}" --config Release
            --prefix "${tool_dir}")

    # Written after a configure succeeds: a cache left by a failed one is redone.
    set(configured "${build_dir}/epm-configured")

    if (EXISTS "${configured}")
        get_epm_note("updating epm from ${source_dir} in ${build_dir}")
        file(WRITE "${log}" "epm build of ${source_dir}\n")
    else ()
        message(STATUS "epm: building epm from ${source_dir} into ${tool_dir}")
        file(REMOVE_RECURSE "${build_dir}")
        file(MAKE_DIRECTORY "${build_dir}")
        file(WRITE "${log}" "epm build of ${source_dir}\n")
        get_epm_run_step(configure "${log}" ${configure})
        file(WRITE "${configured}" "")
    endif ()

    get_epm_run_step(build "${log}" ${build})
    get_epm_run_step(install "${log}" ${install})
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
    set(cached_dir "")
    set(manifest "")

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

    if (source_setting_origin STREQUAL "enclosing")
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

    if (NOT EPM_SOURCE_REF AND NOT EPM_SOURCE_URL)
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

        # 3. A cached entry, unless the latest release is newer and in range.
        get_epm_find_cached("${cache}")
        get_epm_latest_manifest("${cache}" latest)
        set(latest_version "")

        if (NOT latest STREQUAL "")
            string(JSON latest_version GET "${latest}" version)
        endif ()

        epm_version_in_range("${latest_version}" "${EPM_VERSION}" latest_in_range)

        if (NOT cached_dir STREQUAL "" AND NOT (latest_in_range
                AND latest_version VERSION_GREATER cached_version))
            get_epm_found("${cached_dir}/bin/epm${GET_EPM_SUFFIX}"
                    "${cached_dir}/share/epm/cmake/epm.cmake" ON
                    "using epm ${cached_version} from ${cached_dir}/bin/epm${GET_EPM_SUFFIX}")
            return()
        endif ()

        # 4. The latest release, else the exact lower bound's.
        if (latest_in_range)
            set(manifest "${latest}")
        elseif (NOT exact_min STREQUAL "" AND NOT exact_min STREQUAL latest_version)
            get_epm_fetch_manifest(
                    "${GET_EPM_REPOSITORY}/releases/download/v${exact_min}/manifest.json"
                    "${cache}" manifest)
        endif ()

        if (NOT manifest STREQUAL "")
            string(JSON tag GET "${manifest}" tag)

            if (EPM_BOOTSTRAP_NO_DOWNLOAD)
                get_epm_note("EPM_BOOTSTRAP_NO_DOWNLOAD is ON; building ${tag} from source")
            else ()
                get_epm_install("${cache}" "${tag}" release "${manifest}" "")

                if (get_epm_error STREQUAL "")
                    get_epm_found("${cache}/tool/${tag}/bin/epm${GET_EPM_SUFFIX}"
                            "${cache}/tool/${tag}/share/epm/cmake/epm.cmake" ON
                            "${get_epm_message}")
                    return()
                endif ()

                message(STATUS "epm: release download failed (${get_epm_error}), "
                        "building from source")
            endif ()
        endif ()
    endif ()

    # 5. A source build: the forced ref or URL, else the release chosen above,
    # else the lower bound, else main.
    if (EPM_SOURCE_REF)
        set(ref "${EPM_SOURCE_REF}")
    elseif (EPM_SOURCE_URL)
        set(ref "url")
    elseif (NOT manifest STREQUAL "")
        string(JSON ref GET "${manifest}" tag)
    elseif (NOT exact_min STREQUAL "")
        set(ref "v${exact_min}")
    else ()
        set(ref "main")
    endif ()

    string(REGEX REPLACE "[^A-Za-z0-9._-]" "_" key "${ref}")
    set(source "${GET_EPM_REPOSITORY}/archive/${ref}.tar.gz")

    if (EPM_SOURCE_URL)
        set(source "${EPM_SOURCE_URL}")
        string(SHA256 url_hash "${EPM_SOURCE_URL}")
        string(SUBSTRING "${url_hash}" 0 12 url_hash)
        string(APPEND key "-${url_hash}")
    endif ()

    get_epm_install("${cache}" "${key}" source "${source}" "epm ${ref} from source")
    set(executable "${cache}/tool/${key}/bin/epm${GET_EPM_SUFFIX}")

    if (NOT get_epm_error STREQUAL "")
        if (NOT cached_dir STREQUAL "")
            get_epm_found("${cached_dir}/bin/epm${GET_EPM_SUFFIX}"
                    "${cached_dir}/share/epm/cmake/epm.cmake" ON
                    "${get_epm_error}; using epm ${cached_version} from the cache")
            return()
        endif ()

        message(FATAL_ERROR "epm: no epm binary: ${get_epm_error}\n"
                "Configure with -DEPM_EXECUTABLE=/path/to/epm or put epm on PATH.")
    endif ()

    get_epm_found("${executable}" "${cache}/tool/${key}/share/epm/cmake/epm.cmake" ON
            "${get_epm_message}")
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
# has the provider injected by its parent and needs no
# binary: bootstrapping one there could find no module beside an EPM_EXECUTABLE
# in a build tree and go on to download a release.
if (GET_EPM_FUNCTIONS_ONLY OR EPM_NESTED)
    return()
endif ()

option(EPM_BOOTSTRAP_NO_DOWNLOAD
        "Never download a release epm binary; build epm from source instead" OFF)

get_epm_select()

if (get_epm_remember)
    # Remembered as picked here, so the next configure picks afresh instead of
    # treating it as a binary the user gave.
    set(EPM_SELECTED_EXECUTABLE "${get_epm_executable}" CACHE INTERNAL "")
    set(EPM_EXECUTABLE "${get_epm_executable}" CACHE INTERNAL "Path to the epm binary")
    set(EPM_EXECUTABLE "${get_epm_executable}")
endif ()

# -DEPM_UPDATE=ON is a one-off request, not a setting.
unset(EPM_UPDATE CACHE)

include("${get_epm_module}")

# The manifest flow. A project without epm.json may still
# call epm_add_package.
if (NOT EXISTS "${CMAKE_SOURCE_DIR}/epm.json")
    return()
endif ()

get_epm_is_top_level_include(get_epm_top_level)

if (NOT get_epm_top_level)
    message(FATAL_ERROR "epm: ${CMAKE_SOURCE_DIR}/epm.json needs ${GET_EPM_FILE} "
            "listed in CMAKE_PROJECT_TOP_LEVEL_INCLUDES before project(), not "
            "include()d:\n  list(APPEND CMAKE_PROJECT_TOP_LEVEL_INCLUDES "
            "path/to/get_epm.cmake)")
endif ()

if (NOT COMMAND epm_use_manifest)
    message(FATAL_ERROR "epm: the epm at ${EPM_EXECUTABLE} predates epm.json "
            "manifests (its module ${get_epm_module} has no epm_use_manifest). Use "
            "a newer one: -DEPM_EXECUTABLE=/path/to/epm, -DEPM_SOURCE_DIR=/path/to/"
            "epm-checkout (built from source), or EPM_VERSION once a release with "
            "manifests exists.")
endif ()

epm_use_manifest("${CMAKE_SOURCE_DIR}/epm.json")
