cmake_minimum_required(VERSION 3.31)
project(HelloEacp CXX)

# Release archives of eacp only, which a Debug build of hello links as well,
# except on the MSVC ABI (cl, clang-cl), where the debug and release runtimes
# do not mix: there the default, auto, builds eacp for this build's
# configuration.
if (NOT MSVC)
    set(EPM_CONFIGS Release)
endif ()

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
