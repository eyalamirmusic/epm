cmake_minimum_required(VERSION 3.24)
set(CMAKE_PROJECT_TOP_LEVEL_INCLUDES ${LOCKSTEP_SHIM})
project(FetchContentSample LANGUAGES CXX)

include(FetchContent)
FetchContent_Declare(adder
        GIT_REPOSITORY https://github.com/cpm-cmake/testpack-adder.git
        GIT_TAG v1.0.1)
FetchContent_MakeAvailable(adder)

add_executable(sample ../github/main.cpp)
target_link_libraries(sample PRIVATE adder)
