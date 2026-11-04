cmake_minimum_required(VERSION 3.25)
list(APPEND CMAKE_PROJECT_TOP_LEVEL_INCLUDES ../../CMake/get_epm.cmake)
project(HelloGraph CXX)

find_package(eacp REQUIRED)

add_executable(hello Main.cpp)
target_compile_features(hello PRIVATE cxx_std_20)
target_link_libraries(hello PRIVATE eacp-core)

# epm.cpp as an executable of this build too, so the IDE indexes it and the
# build reports its errors. It can run the whole configure itself, as
# HelloGraphRecipe-configure and ctest do, and so can a debugger, stopping in
# describe() or eacp's hooks.
enable_testing()
epm_add_recipe(HelloGraphRecipe)
