# Self-test of how CMake/get_epm.cmake reads <cache>/tool/latest.json, the saved
# listing of the release tags, when the tags cannot be listed afresh: a stale
# listing is used, a corrupt or foreign one counts as no listing, and neither
# stops the configure. Nothing touches the network: the repository is a missing
# local path and git, where there is /usr/bin/false, is that.
#
#   cmake -DWORK_DIR=<scratch dir> -P Tests/GetEpmLatest.cmake

set(GET_EPM_FUNCTIONS_ONLY ON)
include("${CMAKE_CURRENT_LIST_DIR}/../CMake/get_epm.cmake")

if (NOT WORK_DIR)
    set(WORK_DIR "${CMAKE_CURRENT_BINARY_DIR}/get-epm-latest")
endif ()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}/cache/tool")
set(EPM_REPOSITORY "${WORK_DIR}/no-such-repository")
unset(EPM_UPDATE)

if (EXISTS /usr/bin/false)
    set(GIT_EXECUTABLE /usr/bin/false)
endif ()

set(saved "${WORK_DIR}/cache/tool/latest.json")
string(TIMESTAMP now "%s" UTC)
math(EXPR fresh "${now} - 60")
math(EXPR stale "${now} - 3 * 86400")
set(failures 0)

# Writes latest.json with `content`, reads it back and checks the versions and
# whether they count as known.
macro(expect_listing name content expected_versions expected_known)
    file(WRITE "${saved}" "${content}")
    get_epm_release_versions("${WORK_DIR}/cache" actual)

    if (NOT "${actual}" STREQUAL "${expected_versions}"
            OR NOT "${actual_known}" STREQUAL "${expected_known}")
        message(SEND_ERROR "${name}: versions '${actual}', known '${actual_known}'; "
                "expected '${expected_versions}', ${expected_known}")
        math(EXPR failures "${failures} + 1")
    endif ()
endmacro()

set(repository "\"repository\": \"${EPM_REPOSITORY}\"")
set(versions "\"versions\": [\"0.1.0\", \"0.1.3\"]")

expect_listing("fresh listing"
        "{\"schema\": 2, \"checkedAt\": ${fresh}, ${repository}, ${versions}}"
        "0.1.0;0.1.3" TRUE)
expect_listing("stale listing, offline"
        "{\"schema\": 2, \"checkedAt\": ${stale}, ${repository}, ${versions}}"
        "0.1.0;0.1.3" TRUE)
expect_listing("listing from the future, offline"
        "{\"schema\": 2, \"checkedAt\": 99999999999, ${repository}, ${versions}}"
        "0.1.0;0.1.3" TRUE)
expect_listing("non-version entries dropped"
        "{\"schema\": 2, \"checkedAt\": ${fresh}, ${repository}, \"versions\": [\"0.2.0\", \"v0.3.0\", \"nightly\", 7, {}]}"
        "0.2.0;7" TRUE)
expect_listing("empty listing"
        "{\"schema\": 2, \"checkedAt\": ${fresh}, ${repository}, \"versions\": []}"
        "" TRUE)

expect_listing("foreign repository"
        "{\"schema\": 2, \"checkedAt\": ${fresh}, \"repository\": \"https://example.com/fork\", ${versions}}"
        "" FALSE)
expect_listing("empty checkedAt"
        "{\"schema\": 2, \"checkedAt\": \"\", ${repository}, ${versions}}"
        "" FALSE)
expect_listing("non-numeric checkedAt"
        "{\"schema\": 2, \"checkedAt\": \"yesterday\", ${repository}, ${versions}}"
        "" FALSE)
expect_listing("negative checkedAt"
        "{\"schema\": 2, \"checkedAt\": -5, ${repository}, ${versions}}"
        "" FALSE)
expect_listing("huge checkedAt"
        "{\"schema\": 2, \"checkedAt\": 99999999999999999999, ${repository}, ${versions}}"
        "" FALSE)
expect_listing("no checkedAt"
        "{\"schema\": 2, ${repository}, ${versions}}"
        "" FALSE)
expect_listing("versions not a list"
        "{\"schema\": 2, \"checkedAt\": ${fresh}, ${repository}, \"versions\": \"0.1.0\"}"
        "" FALSE)
expect_listing("other schema"
        "{\"schema\": 1, \"checkedAt\": ${fresh}, ${repository}, ${versions}}"
        "" FALSE)
expect_listing("empty file" "" "" FALSE)
expect_listing("not JSON" "{\"schema\": 2, \"checkedAt\": " "" FALSE)
expect_listing("not an object" "[1, 2]" "" FALSE)

# Nothing listed, so nothing is written back: the corrupt copy stays.
file(READ "${saved}" left)

if (NOT left STREQUAL "[1, 2]")
    message(SEND_ERROR "latest.json was rewritten offline: '${left}'")
    math(EXPR failures "${failures} + 1")
endif ()

if (failures GREATER 0)
    message(FATAL_ERROR "${failures} latest.json check(s) failed")
endif ()

message(STATUS "all latest.json checks passed")
