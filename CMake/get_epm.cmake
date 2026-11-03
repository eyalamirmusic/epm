# get_epm.cmake -- the one file a project vendors to use epm.
#
#   set(EPM_VERSION "0.1...<0.2")    # optional; default: the latest release
#   include(cmake/get_epm.cmake)
#   epm_add_package(NAME ... GITHUB_REPOSITORY ... GIT_TAG ...)
#
# Finds an epm binary, then includes the epm.cmake installed with that binary, so
# module and binary always match. First in-range match wins:
#
#   1. EPM_EXECUTABLE, when set.
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
# share/epm/cmake/epm.cmake), whether unpacked from a release or built.
#
# Variables read, all optional (set before the include, or with -D):
#   EPM_VERSION               "0.1.2" (meaning >= 0.1.2, as in find_package),
#                             "0.1...<0.2" or "0.1...0.2"; unset: any version
#   EPM_EXECUTABLE            an epm binary to try first
#   EPM_UPDATE                ON: look for a newer release now (a -D value is
#                             cleared from the cache afterwards)
#   EPM_BOOTSTRAP_NO_DOWNLOAD ON: never download a release binary (skips 4)
#   EPM_SOURCE_REF            build this epm tag, branch or commit (skips 2-4)
#   EPM_SOURCE_URL            build this source tarball (skips 2-4)
#   EPM_BOOTSTRAP_SOURCE_DIR  build this local checkout and use its
#                             CMake/epm.cmake (for working on epm; skips 2-4)
#   EPM_CACHE                 cache root (else the EPM_CACHE environment
#                             variable, else the platform cache directory)
#   EPM_VERBOSE               say what is tried; stream the build output
#
# The binary ends up in EPM_EXECUTABLE (cache INTERNAL). See Docs/PoC.md §8.

include_guard(GLOBAL)

set(GET_EPM_REPOSITORY "https://github.com/eyalamirmusic/epm")
set(GET_EPM_DIR "${CMAKE_CURRENT_LIST_DIR}")

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

# Step 5: builds `source` (a tarball URL or a local checkout) on the host and
# installs it into `tool_dir` with epm's own install rules. Sets get_epm_error
# when the source cannot be had; a failed build is fatal.
function(get_epm_build cache tool_dir ref source description)
    message(STATUS "epm: building ${description} into ${tool_dir} (first time only)")
    file(REMOVE_RECURSE "${tool_dir}")
    set(build_dir "${tool_dir}/build")
    set(log "${build_dir}/epm-bootstrap.log")
    file(MAKE_DIRECTORY "${build_dir}")
    set(staging "")

    if (IS_DIRECTORY "${source}")
        set(source_dir "${source}")
    else ()
        string(RANDOM LENGTH 12 ALPHABET "0123456789abcdef" id)
        set(staging "${cache}/tmp/epm-bootstrap-${id}")
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
    endif ()

    # A host build: nothing from the consumer's toolchain, generator or target
    # architectures reaches it, so it works while the consumer targets iOS or
    # Android. CMake reads these variables from the environment too.
    set(clean_env "${CMAKE_COMMAND}" -E env
            --unset=CMAKE_TOOLCHAIN_FILE
            --unset=CMAKE_GENERATOR
            --unset=CMAKE_GENERATOR_PLATFORM
            --unset=CMAKE_GENERATOR_TOOLSET
            --unset=CMAKE_BUILD_TYPE
            --unset=CMAKE_OSX_ARCHITECTURES
            --unset=CMAKE_INSTALL_PREFIX
            --unset=DESTDIR)
    set(configure "${CMAKE_COMMAND}" -S "${source_dir}" -B "${build_dir}"
            -DCMAKE_BUILD_TYPE=Release -DEPM_ENABLE_TESTS=OFF
            "-DCMAKE_INSTALL_PREFIX=${tool_dir}")
    set(build "${CMAKE_COMMAND}" --build "${build_dir}" --config Release --parallel)
    set(install "${CMAKE_COMMAND}" --install "${build_dir}" --config Release)
    set(echo "")

    if (EPM_VERBOSE)
        set(echo ECHO_OUTPUT_VARIABLE ECHO_ERROR_VARIABLE)
    endif ()

    file(WRITE "${log}" "epm bootstrap of ${source}\n")

    foreach (step configure build install)
        string(JOIN " " command_line ${${step}})
        file(APPEND "${log}" "\n$ ${command_line}\n")
        execute_process(COMMAND ${clean_env} ${${step}}
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
    set(local_module "")

    if (EPM_BOOTSTRAP_SOURCE_DIR)
        get_filename_component(local_dir "${EPM_BOOTSTRAP_SOURCE_DIR}" ABSOLUTE)
        set(local_module "${local_dir}/CMake/epm.cmake")

        if (NOT EXISTS "${local_module}")
            message(FATAL_ERROR "epm: EPM_BOOTSTRAP_SOURCE_DIR '${local_dir}' is not an "
                    "epm checkout (no CMake/epm.cmake)")
        endif ()
    endif ()

    # 1. A binary the user gave (not the one an earlier configure picked, nor one
    # the pre-get_epm epm.cmake bootstrapped). Its module: the dev checkout's, the
    # one installed beside it, or the one beside this file (true inside the epm
    # repository).
    if (EPM_EXECUTABLE AND NOT EPM_EXECUTABLE STREQUAL "${EPM_SELECTED_EXECUTABLE}"
            AND NOT EPM_EXECUTABLE STREQUAL "${EPM_BOOTSTRAPPED_EXECUTABLE}")
        get_epm_version("${EPM_EXECUTABLE}" version)
        epm_version_in_range("${version}" "${EPM_VERSION}" in_range)
        set(module "${local_module}")

        if (module STREQUAL "")
            get_epm_module_beside("${EPM_EXECUTABLE}" module)
        endif ()

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

    if (NOT EPM_BOOTSTRAP_SOURCE_DIR AND NOT EPM_SOURCE_REF AND NOT EPM_SOURCE_URL)
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

    # 5. A source build: the local checkout, the forced ref or URL, else the
    # release chosen above, else the lower bound, else main.
    if (EPM_BOOTSTRAP_SOURCE_DIR)
        string(SHA256 key "${local_dir}")
        string(SUBSTRING "${key}" 0 12 key)
        set(key "local-${key}")
        set(source "${local_dir}")
        set(description "epm from ${local_dir}")
    else ()
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

        set(description "epm ${ref} from source")
    endif ()

    get_epm_install("${cache}" "${key}" source "${source}" "${description}")
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

    if (local_module STREQUAL "")
        set(module "${cache}/tool/${key}/share/epm/cmake/epm.cmake")
    else ()
        set(module "${local_module}")
    endif ()

    get_epm_found("${executable}" "${module}" ON "${get_epm_message}")
endfunction()

# Tests/GetEpmRanges.cmake wants the functions only.
if (GET_EPM_FUNCTIONS_ONLY)
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
    unset(EPM_BOOTSTRAPPED_EXECUTABLE CACHE)
endif ()

# -DEPM_UPDATE=ON is a one-off request, not a setting.
unset(EPM_UPDATE CACHE)

include("${get_epm_module}")
