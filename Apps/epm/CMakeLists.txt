configure_file(Version.h.in "${CMAKE_CURRENT_BINARY_DIR}/Generated/Version.h" @ONLY)

add_executable(epm Main.cpp)
target_link_libraries(epm PRIVATE epm-core)
target_include_directories(epm PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/Generated")

set_default_target_setting(epm)
epm_export_recipe_api(epm)

install(TARGETS epm RUNTIME DESTINATION bin)
