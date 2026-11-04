cmake_minimum_required(VERSION 3.25)

# Lib is a sibling directory here, not a fetched repository: epm uses it as is,
# from source, and still walks its epm.cpp. Without this line App's epm.cpp
# fetches Lib from its git URL and tag.
set(EPM_Lib_SOURCE "${CMAKE_CURRENT_LIST_DIR}/../Lib")

list(APPEND CMAKE_PROJECT_TOP_LEVEL_INCLUDES ../../../CMake/get_epm.cmake)
project(App CXX)

find_package(Lib REQUIRED)

add_executable(app Main.cpp)
target_link_libraries(app PRIVATE Lib::Lib)

epm_add_recipe(AppRecipe)
