configure_file(Version.h.in "${CMAKE_CURRENT_BINARY_DIR}/Generated/Version.h" @ONLY)

add_executable(epm Main.cpp)
target_link_libraries(epm PRIVATE epm-recipe)
target_include_directories(epm PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/Generated")

set_default_target_setting(epm)

# A prefix holds the recipe libraries of every configuration installed into it
# (lib/<config>/), but one bin/epm, which compiles its own recipes against its
# own configuration's libraries: the Release one once Release is installed,
# else the last installed. share/epm/epm-config names the configuration of
# the binary there, so a later install of that configuration refreshes it and
# one of another configuration replaces it unless it is Release.
install(CODE [[
    set(epm_prefix "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}")
    set(epm_binary "${epm_prefix}/bin/$<TARGET_FILE_NAME:epm>")
    set(epm_config_file "${epm_prefix}/share/epm/epm-config")
    set(epm_installed_config "")

    if (EXISTS "${epm_binary}" AND EXISTS "${epm_config_file}")
        file(STRINGS "${epm_config_file}" epm_installed_config LIMIT_COUNT 1)
    endif ()

    string(TOLOWER "${CMAKE_INSTALL_CONFIG_NAME}" epm_config)
    string(TOLOWER "${epm_installed_config}" epm_installed_config)

    if (epm_config STREQUAL "release" OR NOT epm_installed_config STREQUAL "release")
        file(INSTALL "$<TARGET_FILE:epm>" DESTINATION "${CMAKE_INSTALL_PREFIX}/bin"
                USE_SOURCE_PERMISSIONS)
        file(WRITE "${epm_config_file}" "${CMAKE_INSTALL_CONFIG_NAME}\n")
    endif ()
]])
