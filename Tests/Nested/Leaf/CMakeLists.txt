# A package without dependencies, committed into a local git repository by
# Tests/GraphNested.cmake.
cmake_minimum_required(VERSION 3.25)
project(Leaf CXX)

add_library(Leaf STATIC Leaf.cpp)
add_library(Leaf::Leaf ALIAS Leaf)
target_include_directories(Leaf PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
