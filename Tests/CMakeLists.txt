find_package(NanoTest REQUIRED)

nano_add_executable(epmTests
        SOURCES
            BinaryCacheTests.cpp
            CacheRootTests.cpp
            GitTests.cpp
            HarvestTests.cpp
            ResolveTests.cpp
            Sha256Tests.cpp
            SourceCacheTests.cpp
            SpecTests.cpp
        TARGETS epm-core)

add_test(NAME get_epm_ranges
        COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/GetEpmRanges.cmake)
