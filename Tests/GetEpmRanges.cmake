# Self-test of the EPM_VERSION range handling in CMake/get_epm.cmake.
#
#   cmake -P Tests/GetEpmRanges.cmake

set(GET_EPM_FUNCTIONS_ONLY ON)
include("${CMAKE_CURRENT_LIST_DIR}/../CMake/get_epm.cmake")

set(failures 0)

macro(expect_in_range version range expected)
    epm_version_in_range("${version}" "${range}" actual)

    if (NOT actual STREQUAL "${expected}")
        message(SEND_ERROR "epm_version_in_range('${version}', '${range}') is "
                "${actual}, expected ${expected}")
        math(EXPR failures "${failures} + 1")
    endif ()
endmacro()

macro(expect_lower_bound range expected)
    get_epm_exact_lower_bound("${range}" actual)

    if (NOT actual STREQUAL "${expected}")
        message(SEND_ERROR "get_epm_exact_lower_bound('${range}') is '${actual}', "
                "expected '${expected}'")
        math(EXPR failures "${failures} + 1")
    endif ()
endmacro()

# Unset: any version.
expect_in_range(0.1.2 "" TRUE)
expect_in_range(10.0 "" TRUE)

# A bare version is a minimum, as in find_package.
expect_in_range(0.1.2 0.1.2 TRUE)
expect_in_range(0.1.3 0.1.2 TRUE)
expect_in_range(1.0.0 0.1.2 TRUE)
expect_in_range(0.1.1 0.1.2 FALSE)
expect_in_range(0.1.10 0.1.9 TRUE)
expect_in_range(0.1.2 " 0.1.2 " TRUE)

# min...<max: max excluded.
expect_in_range(0.1.0 0.1...<0.2 TRUE)
expect_in_range(0.1.9 0.1...<0.2 TRUE)
expect_in_range(0.2.0 0.1...<0.2 FALSE)
expect_in_range(0.0.9 0.1...<0.2 FALSE)
expect_in_range(0.1.2 0.1.2...<0.1.3 TRUE)
expect_in_range(0.1.3 0.1.2...<0.1.3 FALSE)

# min...max: max included, compared component-wise (0.2 is 0.2.0).
expect_in_range(0.1 0.1...0.2 TRUE)
expect_in_range(0.2 0.1...0.2 TRUE)
expect_in_range(0.2.0 0.1...0.2 TRUE)
expect_in_range(0.2.1 0.1...0.2 FALSE)
expect_in_range(0.0.1 0.1...0.2 FALSE)

# Only plain numeric versions qualify: no prerelease or tag spellings, and an
# empty version (a binary that did not run) never does.
expect_in_range(0.1.2-rc1 "" FALSE)
expect_in_range(v0.1.2 "" FALSE)
expect_in_range("" "" FALSE)
expect_in_range("" 0.1 FALSE)

# The exact lower bound that get_epm.cmake tries as a release of its own.
expect_lower_bound(0.1.2 0.1.2)
expect_lower_bound(0.1.2...<0.2 0.1.2)
expect_lower_bound(0.1...<0.2 "")
expect_lower_bound(1 "")
expect_lower_bound("" "")

if (failures GREATER 0)
    message(FATAL_ERROR "${failures} range check(s) failed")
endif ()

message(STATUS "all range checks passed")
