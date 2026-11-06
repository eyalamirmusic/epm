# A package depending on Leaf through its epm.cpp, committed into a local git
# repository by Tests/GraphNested.cmake with a copy of get_epm.cmake, as a
# real package would vendor it. Built prebuilt, it is a nested child build:
# the parent injects the provider and get_epm.cmake does nothing. Its
# epm_add_recipe is then epm-export.cmake's stub, and from source (not the
# top-level project) a no-op: no MidRecipe either way.
cmake_minimum_required(VERSION 3.25)
list(APPEND CMAKE_PROJECT_TOP_LEVEL_INCLUDES
        "${CMAKE_CURRENT_LIST_DIR}/cmake/get_epm.cmake")
project(Mid CXX)

find_package(Leaf REQUIRED)

add_library(Mid STATIC Mid.cpp)
add_library(Mid::Mid ALIAS Mid)
target_include_directories(Mid PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(Mid PUBLIC Leaf::Leaf)

# Leaf's headers by its source directory too, as packages do: in Mid's child
# build Leaf_SOURCE_DIR is Leaf's snapshot in the cache, which the harvest
# copies into Mid's entry, and built from source over a prebuilt Leaf, the
# snapshot epm fetched for it. Tests/GraphNested.cmake checks both; an empty
# Leaf_SOURCE_DIR must fail here, not pass unnoticed.
if (NOT EXISTS "${Leaf_SOURCE_DIR}/include/Leaf.h")
    message(FATAL_ERROR "Mid: Leaf_SOURCE_DIR '${Leaf_SOURCE_DIR}' holds no "
            "include/Leaf.h")
endif ()

target_include_directories(Mid PUBLIC "${Leaf_SOURCE_DIR}/include")

epm_add_recipe(MidRecipe)
