# A fake prebuilt entry's facade, shaped as Build/Harvest writes one: a
# per-config imported target (local to the including directory, as export()
# output is), a GLOBAL facade over it and the package's alias.
set_property(GLOBAL APPEND PROPERTY EPM_TEST_ORDER Pre)

add_library(epm_release::pre INTERFACE IMPORTED)
set_property(TARGET epm_release::pre PROPERTY
        INTERFACE_COMPILE_DEFINITIONS PRE_FROM_CACHE=1)

if (NOT TARGET pre)
    add_library(pre INTERFACE IMPORTED GLOBAL)
    target_link_libraries(pre INTERFACE epm_release::pre)
endif ()

if (NOT TARGET Pre::pre)
    add_library(Pre::pre ALIAS pre)
endif ()
