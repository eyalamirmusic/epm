# Self-test of the EPM_CONFIGS resolution in CMake/epm.cmake.
#
#   cmake -P Tests/EpmConfigsAuto.cmake

set(EPM_FUNCTIONS_ONLY ON)
include("${CMAKE_CURRENT_LIST_DIR}/../CMake/epm.cmake")

set(failures 0)

macro(expect_configs spec multi_config build_type expected)
    epm_resolve_configs("${spec}" ${multi_config} "${build_type}" actual error)

    if (error OR NOT "${actual}" STREQUAL "${expected}")
        message(SEND_ERROR "epm_resolve_configs('${spec}', ${multi_config}, "
                "'${build_type}') is '${actual}' (error '${error}'), "
                "expected '${expected}'")
        math(EXPR failures "${failures} + 1")
    endif ()
endmacro()

macro(expect_error spec)
    epm_resolve_configs("${spec}" FALSE "" actual error)

    if (NOT error)
        message(SEND_ERROR "epm_resolve_configs('${spec}') gave '${actual}', "
                "expected an error")
        math(EXPR failures "${failures} + 1")
    endif ()
endmacro()

# auto, single-config generator: the build type, mapped to Debug or Release.
expect_configs(auto FALSE Debug "Debug")
expect_configs(auto FALSE Release "Release")
expect_configs(auto FALSE "" "Release")
expect_configs(auto FALSE RelWithDebInfo "Release")
expect_configs(auto FALSE MinSizeRel "Release")
expect_configs(auto FALSE debug "Debug")
expect_configs(auto FALSE Profile "Release")
expect_configs(AUTO FALSE Debug "Debug")
expect_configs("" FALSE Debug "Debug")

# auto, multi-config generator: Debug and Release, whatever the build type.
expect_configs(auto TRUE "" "Debug;Release")
expect_configs(auto TRUE Debug "Debug;Release")
expect_configs(auto TRUE Release "Debug;Release")

# Explicit lists: mapped, deduplicated, Debug first, any case.
expect_configs(Release FALSE Debug "Release")
expect_configs(Debug TRUE "" "Debug")
expect_configs("Release;Debug" FALSE "" "Debug;Release")
expect_configs("Debug;Release" TRUE "" "Debug;Release")
expect_configs("RelWithDebInfo" FALSE Debug "Release")
expect_configs("MinSizeRel;Release;RelWithDebInfo" FALSE "" "Release")
expect_configs("debug;RELEASE" FALSE "" "Debug;Release")

# Unknown names are refused.
expect_error("Profile")
expect_error("Debug;Coverage")
expect_error(";")

if (failures GREATER 0)
    message(FATAL_ERROR "${failures} EPM_CONFIGS resolution check(s) failed")
endif ()

message(STATUS "EPM_CONFIGS resolution: all checks passed")
