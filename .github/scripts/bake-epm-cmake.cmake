# Writes the epm.cmake release asset: CMake/epm.cmake with EPM_VERSION set to the
# released version, EPM_RELEASE_TAG set to the release's tag (left as
# v${EPM_VERSION} for a v* release) and the five EPM_RELEASE_SHA256_<platform>
# values taken from the release's SHA256SUMS.
#
#   cmake -DINPUT=CMake/epm.cmake -DOUTPUT=dist/epm.cmake -DSUMS=dist/SHA256SUMS
#         -DVERSION=0.1.1 -DTAG=v0.1.1 -P .github/scripts/bake-epm-cmake.cmake

cmake_minimum_required(VERSION 3.20)

foreach (var INPUT OUTPUT SUMS VERSION TAG)
    if (NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
        message(FATAL_ERROR "bake-epm-cmake: -D${var}=... is required")
    endif ()
endforeach ()

file(READ "${INPUT}" text)
file(STRINGS "${SUMS}" sums)

# Replaces the single match of `pattern` in `text` with `replacement`; anything
# but exactly one match means CMake/epm.cmake changed shape, so stop.
function(bake_replace pattern replacement)
    string(REGEX MATCHALL "${pattern}" matches "${text}")
    list(LENGTH matches count)

    if (NOT count EQUAL 1)
        message(FATAL_ERROR
                "bake-epm-cmake: expected one match of '${pattern}' in ${INPUT}, "
                "found ${count}")
    endif ()

    string(REGEX REPLACE "${pattern}" "${replacement}" text "${text}")
    set(text "${text}" PARENT_SCOPE)
endfunction()

bake_replace("set\\(EPM_VERSION [^)\n]*\\)" "set(EPM_VERSION ${VERSION})")

if (NOT TAG STREQUAL "v${VERSION}")
    bake_replace("set\\(EPM_RELEASE_TAG [^)\n]*\\)" "set(EPM_RELEASE_TAG \"${TAG}\")")
endif ()

foreach (platform macos-universal linux-x64 linux-arm64 windows-x64 windows-arm64)
    if (platform MATCHES "^windows-")
        set(asset "epm-${VERSION}-${platform}.zip")
    else ()
        set(asset "epm-${VERSION}-${platform}.tar.gz")
    endif ()

    set(hash "")

    foreach (line IN LISTS sums)
        # sha256sum: "<hash>  <name>" (or "<hash> *<name>" in binary mode)
        if (line MATCHES "^([0-9a-fA-F]+) [ *](.+)$" AND CMAKE_MATCH_2 STREQUAL asset)
            string(TOLOWER "${CMAKE_MATCH_1}" hash)
        endif ()
    endforeach ()

    string(LENGTH "${hash}" hash_length)

    if (NOT hash_length EQUAL 64)
        message(FATAL_ERROR "bake-epm-cmake: no SHA-256 for ${asset} in ${SUMS}")
    endif ()

    bake_replace("set\\(EPM_RELEASE_SHA256_${platform} \"[^\"\n]*\"\\)"
            "set(EPM_RELEASE_SHA256_${platform} \"${hash}\")")
    message(STATUS "${platform}: ${hash}")
endforeach ()

file(WRITE "${OUTPUT}" "${text}")
message(STATUS "wrote ${OUTPUT} (epm ${VERSION}, release ${TAG})")
