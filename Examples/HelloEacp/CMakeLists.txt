cmake_minimum_required(VERSION 3.31)
project(HelloEacp CXX)

# Uses -DEPM_EXECUTABLE or an epm on PATH when given, else the latest epm
# release, downloaded into the cache on first use (built from source where no
# binary is available). See Docs/PoC.md §8. Optional, before the include:
#
#   set(EPM_VERSION "0.1...<0.2")                      # accepted epm versions
#   set(EPM_BOOTSTRAP_SOURCE_DIR /path/to/epm)         # build a local epm checkout
include(${CMAKE_CURRENT_LIST_DIR}/../../CMake/get_epm.cmake)

epm_add_package(
        NAME eacp
        GITHUB_REPOSITORY eyalamirmusic/eacp
        GIT_TAG main
        MODE auto
        OPTIONS "EACP_BUILD_GRAPHICS OFF" "EACP_BUILD_WEBVIEW OFF"
        "EACP_ENABLE_TESTS OFF" "EACP_ENABLE_EXAMPLES OFF")

add_executable(hello Main.cpp)
target_compile_features(hello PRIVATE cxx_std_20)

# The same target name whether eacp came prebuilt or from source.
target_link_libraries(hello PRIVATE eacp-core)
