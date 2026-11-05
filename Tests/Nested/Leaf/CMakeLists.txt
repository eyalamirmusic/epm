# A package without dependencies, committed into a local git repository by
# Tests/GraphNested.cmake.
cmake_minimum_required(VERSION 3.25)
project(Leaf CXX)

# Set only by the build steps Mid's epm.cpp declares for Leaf.
option(LEAF_EXTRA "" OFF)

# What Tests/GraphIos.cmake reads from a child build's output (EPM_VERBOSE),
# whose build tree epm deletes after the harvest.
message(STATUS "Leaf: system ${CMAKE_SYSTEM_NAME}, sysroot ${CMAKE_OSX_SYSROOT}")

# And Tests/GraphAndroid.cmake: the triple names the ABI and API level. The
# NDK's legacy toolchain file sets ANDROID_STL, CMake's own support the other.
if (ANDROID)
    set(leaf_stl "${CMAKE_ANDROID_STL_TYPE}")

    if (NOT leaf_stl)
        set(leaf_stl "${ANDROID_STL}")
    endif ()

    message(STATUS "Leaf: android target ${CMAKE_CXX_COMPILER_TARGET}, "
            "stl ${leaf_stl}")
endif ()

add_library(Leaf STATIC Leaf.cpp)
add_library(Leaf::Leaf ALIAS Leaf)
target_include_directories(Leaf PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
