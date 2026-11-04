find_package(NanoTest REQUIRED)

nano_add_executable(epmTests
        SOURCES
            BinaryCacheTests.cpp
            CacheRootTests.cpp
            ConfigsTests.cpp
            FingerprintTests.cpp
            GraphTests.cpp
            HarvestTests.cpp
            LockFileTests.cpp
            ManifestTests.cpp
            MerkleHashTests.cpp
            MessagesTests.cpp
            PackageArgumentsTests.cpp
            PlatformTests.cpp
            RequirementsTests.cpp
            ResolveTests.cpp
            SourceCacheTests.cpp
            SpecTests.cpp
            TimestampTests.cpp
        TARGETS epm-core)

add_test(NAME get_epm_ranges
        COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/GetEpmRanges.cmake)
add_subdirectory(Consumer)

add_test(NAME get_epm_source_dir
        COMMAND ${CMAKE_COMMAND}
            "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/get-epm-source-dir"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/GetEpmSourceDir.cmake)

add_test(NAME epm_configs_auto
        COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/EpmConfigsAuto.cmake)

add_test(NAME epm_overrides
        COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/EpmOverrides.cmake)

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
