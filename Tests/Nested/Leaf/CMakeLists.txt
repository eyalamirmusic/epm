# A package without dependencies, committed into a local git repository by
# Tests/GraphNested.cmake.
cmake_minimum_required(VERSION 3.25)
project(Leaf CXX)

# Set only by the build steps Mid's epm.cpp declares for Leaf.
option(LEAF_EXTRA "" OFF)

# What Tests/GraphIos.cmake reads from a child build's output (EPM_VERBOSE),
# whose build tree epm deletes after the harvest.
message(STATUS "Leaf: system ${CMAKE_SYSTEM_NAME}, sysroot ${CMAKE_OSX_SYSROOT}")

add_library(Leaf STATIC Leaf.cpp)
add_library(Leaf::Leaf ALIAS Leaf)
target_include_directories(Leaf PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
