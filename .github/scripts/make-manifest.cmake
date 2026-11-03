# Writes a release's manifest.json from its SHA256SUMS: the file get_epm.cmake
# reads (through releases/latest/download/manifest.json) to find the archive
# for its host and the SHA-256 to check it against.
#
#   cmake -DSUMS=dist/SHA256SUMS -DVERSION=0.1.2 -DTAG=v0.1.2
#         -DOUTPUT=dist/manifest.json -P .github/scripts/make-manifest.cmake
#
#   { "schema": 1, "version": "0.1.2", "tag": "v0.1.2",
#     "assets": { "<platform>": { "file": "epm-0.1.2-<platform>.tar.gz",
#                                 "sha256": "<hex>" }, ... } }

cmake_minimum_required(VERSION 3.20)

foreach (var SUMS VERSION TAG OUTPUT)
    if (NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
        message(FATAL_ERROR "make-manifest: -D${var}=... is required")
    endif ()
endforeach ()

file(STRINGS "${SUMS}" sums)
set(assets "{}")

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
        message(FATAL_ERROR "make-manifest: no SHA-256 for ${asset} in ${SUMS}")
    endif ()

    string(JSON assets SET "${assets}" "${platform}"
            "{ \"file\": \"${asset}\", \"sha256\": \"${hash}\" }")
endforeach ()

set(json "{}")
string(JSON json SET "${json}" schema 1)
string(JSON json SET "${json}" version "\"${VERSION}\"")
string(JSON json SET "${json}" tag "\"${TAG}\"")
string(JSON json SET "${json}" assets "${assets}")

file(WRITE "${OUTPUT}" "${json}\n")
message(STATUS "wrote ${OUTPUT} (epm ${VERSION}, release ${TAG})")
