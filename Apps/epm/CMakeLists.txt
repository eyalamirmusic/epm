add_executable(epm Main.cpp)
target_link_libraries(epm PRIVATE epm-core)
target_compile_definitions(epm PRIVATE EPM_VERSION="${PROJECT_VERSION}")

set_default_target_setting(epm)
