find_package(NanoTest REQUIRED)

nano_add_executable(epmTests
        SOURCES
            BinaryCacheTests.cpp
            CacheRootTests.cpp
            ConfigsTests.cpp
            FingerprintTests.cpp
            GitTests.cpp
            GraphTests.cpp
            HarvestTests.cpp
            ManifestTests.cpp
            PackageTests.cpp
            PlatformTests.cpp
            RecipeTests.cpp
            ResolveTests.cpp
            Sha256Tests.cpp
            SourceCacheTests.cpp
            SpecTests.cpp
            TimestampTests.cpp
        TARGETS epm-recipe)

target_compile_definitions(epmTests PRIVATE
        EPM_TEST_RECIPE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Recipe"
        EPM_TEST_CMAKE="${CMAKE_COMMAND}")

add_test(NAME get_epm_ranges
        COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/GetEpmRanges.cmake)

add_test(NAME get_epm_source_dir
        COMMAND ${CMAKE_COMMAND}
            "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/get-epm-source-dir"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/GetEpmSourceDir.cmake)

add_test(NAME get_epm_latest
        COMMAND ${CMAKE_COMMAND}
            "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/get-epm-latest"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/GetEpmLatest.cmake)

add_test(NAME epm_configs_auto
        COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/EpmConfigsAuto.cmake)

add_test(NAME epm_overrides
        COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/EpmOverrides.cmake)

add_test(NAME epm_env
        COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/EpmEnv.cmake)

add_test(NAME epm_graph_provider
        COMMAND ${CMAKE_COMMAND}
            "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/epm-graph-provider"
            "-DTEST_GENERATOR=${CMAKE_GENERATOR}"
            "-DTEST_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/GraphProvider.cmake)

add_test(NAME epm_graph_nested
        COMMAND ${CMAKE_COMMAND}
            "-DEPM_BINARY=$<TARGET_FILE:epm>"
            "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/epm-graph-nested"
            "-DTEST_GENERATOR=${CMAKE_GENERATOR}"
            "-DTEST_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
            "-DTEST_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/GraphNested.cmake)
set_tests_properties(epm_graph_nested PROPERTIES
        SKIP_REGULAR_EXPRESSION "epm-graph-nested: skipped")

# The same under Xcode, a multi-config generator with its own toolchain.
if (APPLE)
    find_program(EPM_XCODEBUILD xcodebuild)

    if (EPM_XCODEBUILD)
        add_test(NAME epm_graph_nested_xcode
                COMMAND ${CMAKE_COMMAND}
                    "-DEPM_BINARY=$<TARGET_FILE:epm>"
                    "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/epm-graph-nested-xcode"
                    -DTEST_GENERATOR=Xcode
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/GraphNested.cmake)
        set_tests_properties(epm_graph_nested_xcode PROPERTIES
                SKIP_REGULAR_EXPRESSION "epm-graph-nested: skipped")
    endif ()
endif ()

# Prebuilt packages for an iOS simulator consumer, under Xcode and Ninja;
# skipped off macOS and without the simulator SDK.
add_test(NAME epm_graph_ios
        COMMAND ${CMAKE_COMMAND}
            "-DEPM_BINARY=$<TARGET_FILE:epm>"
            "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/epm-graph-ios"
            "-DTEST_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/GraphIos.cmake)
set_tests_properties(epm_graph_ios PROPERTIES
        SKIP_REGULAR_EXPRESSION "epm-graph-ios: skipped")

if (NOT WIN32)
    add_test(NAME epm_install_round_trip
            COMMAND ${CMAKE_COMMAND}
                "-DBUILD_DIR=${CMAKE_BINARY_DIR}"
                "-DCONFIG=$<CONFIG>"
                "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/epm-install-round-trip"
                -P ${CMAKE_CURRENT_SOURCE_DIR}/InstallRoundTrip.cmake)
endif ()
