configure_file(Version.h.in "${CMAKE_CURRENT_BINARY_DIR}/Generated/Version.h" @ONLY)

add_executable(epm Main.cpp)
target_link_libraries(epm PRIVATE epm-core)
target_include_directories(epm PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/Generated")

set_default_target_setting(epm)

if (WIN32)
    target_sources(epm PRIVATE epm.manifest)
endif ()

install(TARGETS epm RUNTIME DESTINATION bin)
