# A header-only dependency Reloc takes through CPM, so a child build of Reloc
# finds it in the CPM cache: committed into a local git repository by
# Tests/Relocate.cmake.
cmake_minimum_required(VERSION 3.25)
project(RelocDep CXX)

add_library(RelocDep INTERFACE)
target_include_directories(RelocDep INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
