# Self-test of the EPM_VERSION range handling in CMake/get_epm.cmake, and of how
# it picks the release tag to build.
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

# The exact lower bound that get_epm.cmake builds when it cannot list the
# release tags.
expect_lower_bound(0.1.2 0.1.2)
expect_lower_bound(0.1.2...<0.2 0.1.2)
expect_lower_bound(0.1...<0.2 "")
expect_lower_bound(1 "")
expect_lower_bound("" "")

# Release versions from `git ls-remote --tags --refs` output: v<numbers> only,
# whatever the line endings.
set(ls_remote "2dad116dcd36c64cd97c621d29fd6e44f030cc75\trefs/tags/nightly
f4dfd78293a855e2256ecd2ec3b3883f04fdc2d4\trefs/tags/v0.1.0
052086b09dc05be8e6c0f6caa122834f36f62d66\trefs/tags/v0.1.1\r
b68b3143d7709f22c54496f79bc88f29e81b60d3\trefs/tags/v0.1.10
1111111111111111111111111111111111111111\trefs/tags/v0.2.0-rc1
2222222222222222222222222222222222222222\trefs/tags/v0.1.2^{}
3333333333333333333333333333333333333333\trefs/tags/0.3.0
4444444444444444444444444444444444444444\trefs/tags/v0.1.9
")
get_epm_parse_tags("${ls_remote}" versions)

if (NOT versions STREQUAL "0.1.0;0.1.1;0.1.10;0.1.9")
    message(SEND_ERROR "get_epm_parse_tags gave '${versions}', expected "
            "'0.1.0;0.1.1;0.1.10;0.1.9'")
    math(EXPR failures "${failures} + 1")
endif ()

get_epm_parse_tags("" no_versions)

if (NOT no_versions STREQUAL "")
    message(SEND_ERROR "get_epm_parse_tags of nothing gave '${no_versions}'")
    math(EXPR failures "${failures} + 1")
endif ()

# The tag get_epm.cmake builds: the newest version in range, compared
# component-wise.
macro(expect_newest range expected)
    get_epm_newest_in_range("${versions}" "${range}" actual)

    if (NOT actual STREQUAL "${expected}")
        message(SEND_ERROR "get_epm_newest_in_range('${versions}', '${range}') is "
                "'${actual}', expected '${expected}'")
        math(EXPR failures "${failures} + 1")
    endif ()
endmacro()

expect_newest("" 0.1.10)
expect_newest(0.1.1 0.1.10)
expect_newest(0.1.0...<0.1.5 0.1.1)
expect_newest(0.1.0...0.1.9 0.1.9)
expect_newest(0.1.1...0.1.1 0.1.1)
expect_newest(0.2 "")
expect_newest(0.0.1...<0.1 "")

# Which generators build Debug and Release from one build directory.
macro(expect_multi_config generator expected)
    get_epm_is_multi_config("${generator}" actual)

    if (NOT actual STREQUAL "${expected}")
        message(SEND_ERROR "get_epm_is_multi_config('${generator}') is ${actual}, "
                "expected ${expected}")
        math(EXPR failures "${failures} + 1")
    endif ()
endmacro()

expect_multi_config("Ninja Multi-Config" TRUE)
expect_multi_config("Visual Studio 17 2022" TRUE)
expect_multi_config("Xcode" TRUE)
expect_multi_config("Ninja" FALSE)
expect_multi_config("Unix Makefiles" FALSE)
expect_multi_config("NMake Makefiles" FALSE)
expect_multi_config("" FALSE)

if (failures GREATER 0)
    message(FATAL_ERROR "${failures} range check(s) failed")
endif ()

message(STATUS "all range checks passed")
