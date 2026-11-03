# A package depending on Leaf through its epm.json, committed into a local git
# repository by Tests/GraphNested.cmake with a copy of get_epm.cmake, as a
# real package would vendor it. Built prebuilt, it is a nested child build:
# the parent injects the provider and get_epm.cmake does nothing.
cmake_minimum_required(VERSION 3.25)
list(APPEND CMAKE_PROJECT_TOP_LEVEL_INCLUDES
        "${CMAKE_CURRENT_LIST_DIR}/cmake/get_epm.cmake")
project(Mid CXX)

find_package(Leaf REQUIRED)

add_library(Mid STATIC Mid.cpp)
add_library(Mid::Mid ALIAS Mid)
target_include_directories(Mid PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(Mid PUBLIC Leaf::Leaf)
