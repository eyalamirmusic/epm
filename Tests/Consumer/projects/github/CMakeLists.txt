cmake_minimum_required(VERSION 3.24)
set(CMAKE_PROJECT_TOP_LEVEL_INCLUDES ${EPM_MODULE})
project(GithubSample LANGUAGES CXX)

epm_add_package(NAME adder GITHUB_REPOSITORY cpm-cmake/testpack-adder GIT_TAG v1.0.1)

add_executable(sample main.cpp)
target_link_libraries(sample PRIVATE adder)
