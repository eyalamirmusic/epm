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
        Emit/RecipeSdkCMake.cpp
        Emit/RecipeSdkCMake.h
        Emit/Response.cpp
        Emit/Response.h
        Graph/Graph.cpp
        Graph/Graph.h
        Graph/ManifestLoader.cpp
        Graph/ManifestLoader.h
        Graph/Merge.cpp
        Graph/Merge.h
        Graph/ResolvedGraph.h
        Recipe/Project.cpp
        Recipe/Project.h
        Recipe/Recipe.h
        Recipe/RecipeBuild.cpp
        Recipe/RecipeBuild.h
        Recipe/RecipeLoader.cpp
        Recipe/RecipeLoader.h
        Recipe/RecipeSdk.h
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

# The recipe SDK (Recipe/RecipeSdk.h): how this binary compiles an epm.cpp so
# that it loads into it. configure_file fills in the variables, file(GENERATE)
# epm-core's usage requirements, which it evaluates transitively over eacp,
# Miro and the rest, one file per configuration.
separate_arguments(EPM_SDK_CXX_FLAGS UNIX_COMMAND "${CMAKE_CXX_FLAGS}")

foreach (config IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${config}" upper)
    separate_arguments(config_flags UNIX_COMMAND "${CMAKE_CXX_FLAGS_${upper}}")
    string(APPEND EPM_SDK_CXX_FLAGS ";$<$<CONFIG:${config}>:${config_flags}>")
endforeach ()

if (DEFINED CMAKE_CXX_EXTENSIONS AND NOT CMAKE_CXX_EXTENSIONS)
    set(EPM_SDK_CXX_EXTENSIONS OFF)
else ()
    set(EPM_SDK_CXX_EXTENSIONS ON)
endif ()

set(EPM_SDK_OSX_ARCHITECTURES "${CMAKE_OSX_ARCHITECTURES}")

# With none set, CMake still passes -arch for the host's default
# (Platform/Darwin-Initialize.cmake), which a Rosetta-run CMake makes x86_64.
if (APPLE AND NOT EPM_SDK_OSX_ARCHITECTURES)
    set(EPM_SDK_OSX_ARCHITECTURES "${_CMAKE_APPLE_ARCHS_DEFAULT}")
endif ()

configure_file(Recipe/RecipeSdk.cpp.in
        "${CMAKE_CURRENT_BINARY_DIR}/Generated/RecipeSdk.cpp.in" @ONLY)
file(GENERATE
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/Generated/RecipeSdk-$<CONFIG>.cpp"
        INPUT "${CMAKE_CURRENT_BINARY_DIR}/Generated/RecipeSdk.cpp.in"
        TARGET epm-core)
target_sources(epm-core PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/Generated/RecipeSdk-$<CONFIG>.cpp")

# A recipe module links nothing: its undefined symbols resolve at load time
# against the executable that loads it, as eacp's hot reload modules do
# (eacp's Docs/HotReload.md). So that executable exports epm-core and every
# static library under it, each linked whole and compiled at default
# visibility. The helpers are eacp's (EacpPlugin.cmake). Windows has no
# recipes: the target links as before.
function(epm_export_recipe_api target)
    if (WIN32)
        return()
    endif ()

    set(archives epm-core)
    eacp_collect_link_targets(epm-core archives)

    foreach (archive IN LISTS archives)
        get_target_property(type ${archive} TYPE)

        if (type MATCHES "^(STATIC|OBJECT)_LIBRARY$")
            eacp_set_default_visibility(${archive})
        endif ()

        if (type STREQUAL "STATIC_LIBRARY")
            target_link_libraries(${target} PRIVATE
                    "$<LINK_LIBRARY:WHOLE_ARCHIVE,${archive}>")
            set_target_properties(${target} PROPERTIES
                    LINK_LIBRARY_OVERRIDE_${archive} WHOLE_ARCHIVE)
        endif ()
    endforeach ()

    set_target_properties(${target} PROPERTIES
            ENABLE_EXPORTS ON
            INTERPROCEDURAL_OPTIMIZATION_RELEASE OFF)
    eacp_set_default_visibility(${target})

    if (APPLE)
        target_link_options(${target} PRIVATE LINKER:-export_dynamic)
    endif ()
endfunction()
