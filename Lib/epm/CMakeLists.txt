add_library(epm-core STATIC
        Build/ChildBuild.cpp
        Build/ChildBuild.h
        Build/Harvest.cpp
        Build/Harvest.h
        Cache/BinaryCache.cpp
        Cache/BinaryCache.h
        Cache/CacheRoot.cpp
        Cache/CacheRoot.h
        Cache/PackageLock.cpp
        Cache/PackageLock.h
        Cache/SourceCache.cpp
        Cache/SourceCache.h
        Emit/Response.cpp
        Emit/Response.h
        Lock/LockFile.cpp
        Lock/LockFile.h
        Lock/MerkleHash.cpp
        Lock/MerkleHash.h
        Lock/Messages.cpp
        Lock/Messages.h
        Lock/Session.cpp
        Lock/Session.h
        Resolve/Commands.cpp
        Resolve/Commands.h
        Resolve/Resolve.cpp
        Resolve/Resolve.h
        Source/Download.cpp
        Source/Download.h
        Source/Mirror.cpp
        Source/Mirror.h
        Source/PackageCache.cpp
        Source/PackageCache.h
        Source/Submodules.cpp
        Source/Submodules.h
        Spec/Env.h
        Spec/JsonFile.h
        Spec/PackageArguments.cpp
        Spec/PackageArguments.h
        Spec/PackageKind.h
        Spec/Request.h
        Util/Command.cpp
        Util/Command.h
        Util/Git.cpp
        Util/Git.h
        Util/Log.cpp
        Util/Log.h
        Util/LongPath.cpp
        Util/LongPath.h
        Util/Paths.h
        Util/Sha256.cpp
        Util/Sha256.h
        Util/Status.cpp
        Util/Status.h
        Util/Text.cpp
        Util/Text.h
        Util/Timestamp.cpp
        Util/Timestamp.h)

# epm-export.cmake is compiled into the binary, so an installed epm writes the
# script into child builds without needing this repository at runtime.
set(epm_export_script "${CMAKE_CURRENT_SOURCE_DIR}/../../CMake/epm-export.cmake")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${epm_export_script}")
file(READ "${epm_export_script}" EPM_EXPORT_SCRIPT)
configure_file(Build/EmbeddedExport.cpp.in
        "${CMAKE_CURRENT_BINARY_DIR}/Generated/EmbeddedExport.cpp" @ONLY)
target_sources(epm-core PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/Generated/EmbeddedExport.cpp")

target_include_directories(epm-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

# eacp-network for IPC::Lock, the cross-process package lock.
target_link_libraries(epm-core PUBLIC eacp-core eacp-network)

set_default_target_setting(epm-core)
