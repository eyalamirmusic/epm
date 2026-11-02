find_package(NanoTest REQUIRED)

nano_add_executable(epmTests
        SOURCES
            CacheRootTests.cpp
            GitTests.cpp
            Sha256Tests.cpp
            SpecTests.cpp
        TARGETS epm-core)
