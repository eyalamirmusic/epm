cmake_minimum_required(VERSION 3.25)
set(CMAKE_PROJECT_TOP_LEVEL_INCLUDES ${LOCKSTEP_SHIM})
project(PatchBesideFileSample LANGUAGES CXX)

include(recipes/adder.cmake)

file(READ "${adder_SOURCE_DIR}/README.md" readme)
if (NOT readme MATCHES "Patched by lockstep's test")
    message(FATAL_ERROR "the patch beside recipes/adder.cmake was not applied")
endif ()

add_executable(sample ../github/main.cpp)
target_link_libraries(sample PRIVATE adder)
