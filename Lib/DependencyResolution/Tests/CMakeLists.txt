# Drives DependencyResolution with plain request lists over local git
# repositories; no recipe, graph or CMake project is involved.
nano_add_executable(DependencyResolutionTests
        SOURCES
            SourceResolverTests.cpp
            ErrorMessagesTests.cpp
            LockFileTests.cpp
            MerkleHashTests.cpp
            RequirementsTests.cpp
            SourceSnapshotTests.cpp
        TARGETS DependencyResolution)

target_compile_definitions(DependencyResolutionTests PRIVATE
        DEPENDENCY_RESOLUTION_TEST_CMAKE="${CMAKE_COMMAND}")
