# A fake source package. Its old minimum leaves CMP0077 and CMP0126 to the
# CMAKE_POLICY_DEFAULT_* variables epm sets, as many real packages do.
cmake_minimum_required(VERSION 3.10)
project(Leaf NONE)

# Before Leaf's own find_package(Pre): Pre is in the order first only if the
# provider loaded it, as Leaf's dependency, before adding Leaf.
set_property(GLOBAL APPEND PROPERTY EPM_TEST_ORDER Leaf)

option(LEAF_FLAG "A test option" OFF)
set(LEAF_LEVEL "low" CACHE STRING "A test cache entry")

# Re-enters the provider; Pre, a dependency, is already loaded.
find_package(Pre REQUIRED)

if (NOT TARGET Pre::pre)
    message(FATAL_ERROR "Leaf: Pre::pre is missing")
endif ()

set_property(GLOBAL PROPERTY EPM_TEST_LEAF_FLAG "${LEAF_FLAG}")
set_property(GLOBAL PROPERTY EPM_TEST_LEAF_LEVEL "${LEAF_LEVEL}")
set_property(GLOBAL PROPERTY EPM_TEST_LEAF_NAME "${LEAF_NAME}")

add_library(leaf INTERFACE)
target_link_libraries(leaf INTERFACE Pre::pre)
add_library(Leaf::leaf ALIAS leaf)
