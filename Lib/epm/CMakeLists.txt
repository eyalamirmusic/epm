add_library(epm-core STATIC
        Build/ChildBuild.cpp
        Build/ChildBuild.h
        Build/Configs.cpp
        Build/Configs.h
        Build/Harvest.cpp
        Build/Harvest.h
        Cache/BinaryCache.cpp
        Cache/BinaryCache.h
        Cache/CacheListing.cpp
        Cache/CacheListing.h
        Cache/CacheRoot.cpp
        Cache/CacheRoot.h
        Cache/Fingerprint.cpp
        Cache/Fingerprint.h
        Cache/PackageLock.cpp
        Cache/PackageLock.h
        Cache/SourceCache.cpp
        Cache/SourceCache.h
        Emit/GraphResponse.cpp
        Emit/GraphResponse.h
        Emit/Response.cpp
        Emit/Response.h
        Graph/Graph.cpp
        Graph/Graph.h
        Graph/Merge.cpp
        Graph/Merge.h
        Graph/ResolvedGraph.h
        Lock/LockFile.cpp
        Lock/LockFile.h
        Lock/MerkleHash.cpp
        Lock/MerkleHash.h
        Lock/Messages.cpp
        Lock/Messages.h
        Lock/Requirements.cpp
        Lock/Requirements.h
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
        Spec/Manifest.cpp
        Spec/Manifest.h
        Spec/Overrides.cpp
        Spec/Overrides.h
        Spec/PackageArguments.cpp
        Spec/PackageArguments.h
        Spec/PackageKind.h
        Spec/Platform.cpp
        Spec/Platform.h
        Spec/Request.h
        Util/Command.cpp
        Util/Command.h
        Util/Git.cpp
        Util/Git.h
        Util/Log.cpp
        Util/Log.h
        Util/LongPath.cpp
        Util/LongPath.h
        Util/Status.cpp
        Util/Status.h
        Util/Text.cpp
        Util/Text.h
        Util/Timestamp.cpp
        Util/Timestamp.h)

# epm-export.cmake, epm.cmake and epm-provider.cmake are compiled into the
# binary, so an installed epm writes them into child builds without needing
# this repository at runtime. MSVC rejects a string literal over 16380 bytes
# (C2026), so each script becomes adjacent raw literals of at most 12000.
set(epm_script_chunk_length 12000)

foreach (script IN ITEMS export module provider)
    if (script STREQUAL "module")
        set(script_file "${PROJECT_SOURCE_DIR}/CMake/epm.cmake")
    else ()
        set(script_file "${PROJECT_SOURCE_DIR}/CMake/epm-${script}.cmake")
    endif ()

    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${script_file}")
    file(READ "${script_file}" text)
    string(LENGTH "${text}" length)
    set(literals "")
    set(offset 0)

    while (offset LESS length)
        string(SUBSTRING "${text}" ${offset} ${epm_script_chunk_length} chunk)
        string(APPEND literals "R\"epm_script(${chunk})epm_script\"\n")
        math(EXPR offset "${offset} + ${epm_script_chunk_length}")
    endwhile ()

    string(TOUPPER "${script}" upper)
    set(EPM_${upper}_SCRIPT "${literals}")
endforeach ()

configure_file(Build/EmbeddedScripts.cpp.in
        "${CMAKE_CURRENT_BINARY_DIR}/Generated/EmbeddedScripts.cpp" @ONLY)
target_sources(epm-core PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/Generated/EmbeddedScripts.cpp")

target_include_directories(epm-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

# eacp-network for IPC::Lock, the cross-process package lock.
target_link_libraries(epm-core PUBLIC eacp-core eacp-network)

set_default_target_setting(epm-core)
