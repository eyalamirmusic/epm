# Names its patch relative to this file, as CPM resolves PATCHES.
epm_add_package(NAME adder GITHUB_REPOSITORY cpm-cmake/testpack-adder GIT_TAG v1.0.1
        PATCHES readme.patch)
