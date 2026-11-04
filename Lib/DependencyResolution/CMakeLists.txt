# DependencyResolution: package requests in, locked source trees out. Minimal
# version selection, root overrides, epm-lock.json (commit and Merkle hash per
# package), the source cache with its bare clones and downloads, and the
# cross-process locks around them. It knows nothing of recipes, the graph or
# CMake; epm's graph walk reaches it through one adapter.
add_library(DependencyResolution STATIC
        Cache/CacheRoot.cpp
        Cache/CrossProcessLock.cpp
        DependencyResolution.cpp
        Lock/ErrorMessages.cpp
        Lock/LockFile.cpp
        Lock/MerkleHash.cpp
        Lock/Requirements.cpp
        Source/BareClone.cpp
        Source/Download.cpp
        Source/SourceCache.cpp
        Source/SourceSnapshot.cpp
        Source/Submodules.cpp
        Util/Command.cpp
        Util/Git.cpp
        Util/Log.cpp
        Util/LongPath.cpp
        Util/Status.cpp
        Util/Text.cpp)

target_include_directories(DependencyResolution PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

# eacp-network for IPC::Lock and the HTTP downloads.
target_link_libraries(DependencyResolution PUBLIC eacp-core eacp-network Miro)

set_default_target_setting(DependencyResolution)
