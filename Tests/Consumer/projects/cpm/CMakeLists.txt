cmake_minimum_required(VERSION 3.24)
set(CMAKE_PROJECT_TOP_LEVEL_INCLUDES ${LOCKSTEP_SHIM})
project(CpmSample LANGUAGES CXX)

CPMAddPackage("gh:cpm-cmake/testpack-fibadder@1.1.0")

# CPM writes a find_package redirect for every package it adds.
find_package(testpack-fibadder REQUIRED)

add_executable(sample main.cpp)
target_link_libraries(sample PRIVATE fibadder)
