cmake_minimum_required(VERSION 3.25)
set(CMAKE_PROJECT_TOP_LEVEL_INCLUDES ${EPM_MODULE})
project(SymlinksSample LANGUAGES NONE)

# shine 3.1.1's archive holds three symlinks under js/test/browser.
epm_add_package(NAME shine DOWNLOAD_ONLY
        URL https://github.com/toots/shine/archive/refs/tags/3.1.1.tar.gz)
epm_add_package(NAME linked GIT_REPOSITORY "${FIXTURE_URL}/linked.git" GIT_TAG main
        DOWNLOAD_ONLY)
