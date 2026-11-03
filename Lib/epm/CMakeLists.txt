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
        Resolve/Resolve.cpp
        Resolve/Resolve.h
        Spec/Env.h
        Spec/JsonFile.h
        Spec/Manifest.cpp
        Spec/Manifest.h
        Spec/Overrides.cpp
        Spec/Overrides.h
        Spec/Platform.cpp
        Spec/Platform.h
        Spec/Request.h
        Util/Command.cpp
        Util/Command.h
        Util/Git.cpp
        Util/Git.h
        Util/Log.cpp
        Util/Log.h
        Util/Sha256.cpp
        Util/Sha256.h
        Util/Text.cpp
        Util/Text.h
        Util/Timestamp.cpp
        Util/Timestamp.h)

# epm-export.cmake and epm-provider.cmake (the core of epm-nested.cmake) are
# compiled into the binary, so an installed epm writes them into child builds
# without needing this repository at runtime.
foreach (script IN ITEMS export provider)
    set(script_file "${CMAKE_CURRENT_SOURCE_DIR}/../../CMake/epm-${script}.cmake")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${script_file}")
    string(TOUPPER "${script}" upper)
    file(READ "${script_file}" EPM_${upper}_SCRIPT)

    # MSVC rejects a string literal over 16380 bytes (C2026).
    string(LENGTH "${EPM_${upper}_SCRIPT}" length)

    if (length GREATER 15000)
        message(FATAL_ERROR "${script_file} is ${length} bytes; split it into "
                "several literals in Build/EmbeddedScripts.cpp.in")
    endif ()
endforeach ()

configure_file(Build/EmbeddedScripts.cpp.in
        "${CMAKE_CURRENT_BINARY_DIR}/Generated/EmbeddedScripts.cpp" @ONLY)
target_sources(epm-core PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/Generated/EmbeddedScripts.cpp")

target_include_directories(epm-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

# eacp-network for IPC::Lock, the cross-process package lock.
target_link_libraries(epm-core PUBLIC eacp-core eacp-network)

set_default_target_setting(epm-core)
