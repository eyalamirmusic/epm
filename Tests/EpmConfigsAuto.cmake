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

# auto, single-config generator: the build type as it is built: Debug, Release
# or RelWithDebInfo; MinSizeRel, an empty or an unknown type as Release.
expect_configs(auto FALSE Debug "Debug")
expect_configs(auto FALSE Release "Release")
expect_configs(auto FALSE "" "Release")
expect_configs(auto FALSE RelWithDebInfo "RelWithDebInfo")
expect_configs(auto FALSE relwithdebinfo "RelWithDebInfo")
expect_configs(auto FALSE MinSizeRel "Release")
expect_configs(auto FALSE debug "Debug")
expect_configs(auto FALSE Profile "Release")
expect_configs(AUTO FALSE Debug "Debug")
expect_configs("" FALSE Debug "Debug")

# auto, multi-config generator: Debug and Release, whatever the build type.
expect_configs(auto TRUE "" "Debug;Release")
expect_configs(auto TRUE Debug "Debug;Release")
expect_configs(auto TRUE Release "Debug;Release")

# Explicit lists: mapped, deduplicated, Debug, Release, RelWithDebInfo, any case.
expect_configs(Release FALSE Debug "Release")
expect_configs(Debug TRUE "" "Debug")
expect_configs("Release;Debug" FALSE "" "Debug;Release")
expect_configs("Debug;Release" TRUE "" "Debug;Release")
expect_configs("RelWithDebInfo" FALSE Debug "RelWithDebInfo")
expect_configs("MinSizeRel;Release;RelWithDebInfo" FALSE "" "Release;RelWithDebInfo")
expect_configs("RelWithDebInfo;Debug;MinSizeRel" FALSE "" "Debug;Release;RelWithDebInfo")
expect_configs("debug;RELEASE" FALSE "" "Debug;Release")

# Unknown names are refused.
expect_error("Profile")
expect_error("Debug;Coverage")
expect_error(";")

# Which configurations of epm's recipe libraries epm::recipe maps a build
# configuration to, best first.
macro(expect_recipe_configs config available expected)
    epm_recipe_config_preference("${config}" "${available}" actual)

    if (NOT "${actual}" STREQUAL "${expected}")
        message(SEND_ERROR "epm_recipe_config_preference('${config}', "
                "'${available}') is '${actual}', expected '${expected}'")
        math(EXPR failures "${failures} + 1")
    endif ()
endmacro()

expect_recipe_configs(Debug "DEBUG;RELEASE" "DEBUG;RELEASE")
expect_recipe_configs(debug "RELEASE;DEBUG" "DEBUG;RELEASE")
expect_recipe_configs(Release "DEBUG;RELEASE" "RELEASE;DEBUG")
expect_recipe_configs(RelWithDebInfo "DEBUG;RELEASE" "RELEASE;DEBUG")
expect_recipe_configs(MinSizeRel "DEBUG;RELEASE" "RELEASE;DEBUG")
expect_recipe_configs(NOCONFIG "DEBUG;RELEASE" "RELEASE;DEBUG")
expect_recipe_configs("" "DEBUG;RELEASE" "RELEASE;DEBUG")
expect_recipe_configs(Profile "DEBUG;RELEASE" "RELEASE;DEBUG")

# A configuration epm installed under the same name comes first; one that is
# missing falls back to the other.
expect_recipe_configs(RelWithDebInfo "DEBUG;RELEASE;RELWITHDEBINFO"
        "RELWITHDEBINFO;RELEASE;DEBUG")
expect_recipe_configs(Debug "RELEASE" "RELEASE")
expect_recipe_configs(Release "DEBUG" "DEBUG")
expect_recipe_configs("" "DEBUG" "DEBUG")
expect_recipe_configs(Debug "MINSIZEREL;RELEASE" "RELEASE;MINSIZEREL")

if (failures GREATER 0)
    message(FATAL_ERROR "${failures} EPM_CONFIGS resolution check(s) failed")
endif ()

message(STATUS "EPM_CONFIGS resolution: all checks passed")
