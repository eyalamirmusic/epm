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
