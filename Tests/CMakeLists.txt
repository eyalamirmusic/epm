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
            PlatformTests.cpp
            ResolveTests.cpp
            Sha256Tests.cpp
            SourceCacheTests.cpp
            SpecTests.cpp
        TARGETS epm-core)

add_test(NAME get_epm_ranges
        COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/GetEpmRanges.cmake)

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
