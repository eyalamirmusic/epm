cmake_minimum_required(VERSION 3.25)
set(CMAKE_PROJECT_TOP_LEVEL_INCLUDES ${EPM_MODULE})
project(CpmStubSample LANGUAGES CXX)

epm_add_package(NAME dep SOURCE_DIR dep)

add_executable(sample ../github/main.cpp)
target_link_libraries(sample PRIVATE adder)
