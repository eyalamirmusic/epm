cmake_minimum_required(VERSION 3.25)
list(APPEND CMAKE_PROJECT_TOP_LEVEL_INCLUDES ../../CMake/get_epm.cmake)
project(HelloGraph CXX)

find_package(eacp REQUIRED)

add_executable(hello Main.cpp)
target_compile_features(hello PRIVATE cxx_std_20)
target_link_libraries(hello PRIVATE eacp-core)

# epm.cpp as a target of this build too, so the IDE indexes it and the build
# reports its errors; ctest (and HelloGraphRecipe-describe) runs that module
# through `epm describe`, which a debugger can step into in Debug.
enable_testing()
epm_add_recipe(HelloGraphRecipe)
