# find_package from a subdirectory: Leaf and its dependency Pre load here.
find_package(Leaf REQUIRED)

if (NOT Leaf_FOUND OR NOT Leaf_MODE STREQUAL "source"
        OR NOT Leaf_SOURCE_DIR STREQUAL "${packages}/Leaf")
    message(SEND_ERROR "find_package(Leaf): FOUND '${Leaf_FOUND}', MODE "
            "'${Leaf_MODE}', SOURCE_DIR '${Leaf_SOURCE_DIR}'")
endif ()
