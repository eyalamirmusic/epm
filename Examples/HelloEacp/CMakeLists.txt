cmake_minimum_required(VERSION 3.31)
project(HelloEacp CXX)

# Uses -DEPM_EXECUTABLE when given; otherwise downloads the pinned epm release
# binary into the cache on first use, or builds it from source where no binary
# is available (see Docs/PoC.md §8).
include(${CMAKE_CURRENT_LIST_DIR}/../../CMake/epm.cmake)

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
