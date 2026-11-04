# A CMAKE_PROJECT_TOP_LEVEL_INCLUDES file that registers epm's provider over a
# ready graph.cmake, as a nested child build does.
include("${CMAKE_CURRENT_LIST_DIR}/../../CMake/epm-provider.cmake")
epm_provider_register("${EPM_TEST_GRAPH}")
