find_package(NanoTest REQUIRED)

nano_add_executable(epmTests
        SOURCES
            BinaryCacheTests.cpp
            CacheRootTests.cpp
            HarvestTests.cpp
            LockFileTests.cpp
            MerkleHashTests.cpp
            MessagesTests.cpp
            PackageArgumentsTests.cpp
            RequirementsTests.cpp
            ResolveTests.cpp
            SourceCacheTests.cpp
            SpecTests.cpp
        TARGETS epm-core)

add_test(NAME get_epm_ranges
        COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/GetEpmRanges.cmake)
add_subdirectory(Consumer)
