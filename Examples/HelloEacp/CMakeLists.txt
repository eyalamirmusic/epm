cmake_minimum_required(VERSION 3.31)
project(HelloEacp CXX)

set(EPM_CONFIGS Release) # only Release archives of eacp; Debug builds of hello link them on multi-config generators (missing configs map to Release)
include(../../CMake/get_epm.cmake)

epm_add_package(
        NAME eacp
        GITHUB_REPOSITORY eyalamirmusic/eacp
        GIT_TAG main
        MODE auto
        OPTIONS "EACP_BUILD_GRAPHICS OFF" "EACP_BUILD_WEBVIEW OFF"
        "EACP_ENABLE_TESTS OFF" "EACP_ENABLE_EXAMPLES OFF")

add_executable(hello Main.cpp)
target_compile_features(hello PRIVATE cxx_std_20)
target_link_libraries(hello PRIVATE eacp-core)
