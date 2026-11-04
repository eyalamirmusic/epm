cmake_minimum_required(VERSION 3.25)
list(APPEND CMAKE_PROJECT_TOP_LEVEL_INCLUDES ${EPM_MODULE})
project(GraphLockSample LANGUAGES NONE)

find_package(depender REQUIRED)
find_package(shared REQUIRED)
