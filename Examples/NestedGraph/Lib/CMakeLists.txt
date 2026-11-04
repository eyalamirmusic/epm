cmake_minimum_required(VERSION 3.25)
list(APPEND CMAKE_PROJECT_TOP_LEVEL_INCLUDES ../../../CMake/get_epm.cmake)
project(Lib CXX)

find_package(eacp REQUIRED)

add_library(Lib STATIC Lib.cpp)
add_library(Lib::Lib ALIAS Lib)
target_compile_features(Lib PUBLIC cxx_std_20)
target_include_directories(Lib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(Lib PUBLIC eacp-core)

# Only when Lib is the top-level project: built from source inside an app,
# epm_add_recipe does nothing.
epm_add_recipe(LibRecipe)
