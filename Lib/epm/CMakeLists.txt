# epm's one library: what an epm.cpp recipe links (Recipe/Recipe.h), and the
# driver that epm links. A recipe gets the types its describe() fills, the
# recipeMain its main() returns, and everything a package's build() step may
# call: the specs, the source and binary caches, the child build and the
# harvest. The driver adds the graph walk, `epm resolve`, the graph files and
# compiling and running recipes against the recipe SDK baked in below.
# epm_recipe_headers are the recipe API, the headers the SDK installs.
set(epm_recipe_headers
        Build/ChildBuild.h
        Build/Configs.h
        Build/Harvest.h
        Cache/BinaryCache.h
        Cache/CacheRoot.h
        Cache/Fingerprint.h
        Cache/PackageLock.h
        Cache/SourceCache.h
        Recipe/BuildError.h
        Recipe/BuildProtocol.h
        Recipe/CMakeBuild.h
        Recipe/CMakePackage.h
        Recipe/Package.h
        Recipe/Prefix.h
        Recipe/Project.h
        Recipe/Recipe.h
        Recipe/RecipeMain.h
        Spec/Env.h
        Spec/JsonFile.h
        Spec/Manifest.h
        Spec/Overrides.h
        Spec/Platform.h
        Spec/Request.h
        Util/Command.h
        Util/Git.h
        Util/Log.h
        Util/Paths.h
        Util/Sha256.h
        Util/Text.h
        Util/Timestamp.h)

add_library(epm-recipe STATIC
        ${epm_recipe_headers}
        Build/ChildBuild.cpp
        Build/Configs.cpp
        Build/Harvest.cpp
        Cache/BinaryCache.cpp
        Cache/CacheRoot.cpp
        Cache/Fingerprint.cpp
        Cache/PackageLock.cpp
        Cache/SourceCache.cpp
        Recipe/BuildProtocol.cpp
        Recipe/CMakeBuild.cpp
        Recipe/CMakePackage.cpp
        Recipe/Package.cpp
        Recipe/Prefix.cpp
        Recipe/Project.cpp
        Recipe/Recipe.cpp
        Recipe/RecipeMain.cpp
        Spec/Manifest.cpp
        Spec/Overrides.cpp
        Spec/Platform.cpp
        Spec/Request.cpp
        Util/Command.cpp
        Util/Git.cpp
        Util/Log.cpp
        Util/Paths.cpp
        Util/Sha256.cpp
        Util/Text.cpp
        Util/Timestamp.cpp)

# The driver: the graph, `epm resolve`, and building and running recipes.
target_sources(epm-recipe PRIVATE
        Cache/CacheListing.cpp
        Cache/CacheListing.h
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
        Recipe/RecipeBuild.cpp
        Recipe/RecipeBuild.h
        Recipe/RecipeConfigure.cpp
        Recipe/RecipeConfigure.h
        Recipe/RecipeRun.cpp
        Recipe/RecipeRun.h
        Recipe/RecipeSdk.cpp
        Recipe/RecipeSdk.h
        Resolve/Resolve.cpp
        Resolve/Resolve.h)

target_include_directories(epm-recipe PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

# eacp-network for IPC::Lock, the cross-process package lock.
target_link_libraries(epm-recipe PUBLIC eacp-core eacp-network Miro)

set_default_target_setting(epm-recipe)

# epm-export.cmake and epm-provider.cmake (the core of epm-nested.cmake) are
# compiled into epm-recipe, so an installed epm, and a recipe executable
# running a build step, write them into child builds without needing this
# repository at runtime.
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
target_sources(epm-recipe PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/Generated/EmbeddedScripts.cpp")

# The link options naming the shared library at `path` by name rather than by
# the build machine's path to it (/usr/lib/<triplet>/libcurl.so), which a
# recipe SDK installed elsewhere would carry: -l<name> for lib<name>.so,
# -l:<file> for a versioned lib<name>.so.<N>, after -L<directory> when the
# compiler does not search that directory anyway. Empty for anything else (a
# framework, a .dylib or .tbd, an archive), which keeps its path.
function(epm_shared_library_options path out_var)
    set(options "")
    get_filename_component(file "${path}" NAME)
    get_filename_component(directory "${path}" DIRECTORY)

    if (file MATCHES "^lib(.+)\\.so$")
        set(options "-l${CMAKE_MATCH_1}")
    elseif (file MATCHES "^lib.+\\.so(\\.[0-9]+)+$")
        set(options "-l:${file}")
    endif ()

    if (options AND directory
            AND NOT directory IN_LIST CMAKE_CXX_IMPLICIT_LINK_DIRECTORIES)
        list(PREPEND options "-L${directory}")
    endif ()

    set(${out_var} "${options}" PARENT_SCOPE)
endfunction()

# The file an imported library target stands for: IMPORTED_LOCATION, else that
# of its first imported configuration that has one; empty without either.
function(epm_imported_location target out_var)
    get_target_property(location ${target} IMPORTED_LOCATION)
    get_target_property(configs ${target} IMPORTED_CONFIGURATIONS)

    if (NOT location AND configs)
        foreach (config IN LISTS configs)
            string(TOUPPER "${config}" config)
            get_target_property(location ${target} IMPORTED_LOCATION_${config})

            if (location)
                break()
            endif ()
        endforeach ()
    endif ()

    if (NOT location)
        set(location "")
    endif ()

    set(${out_var} "${location}" PARENT_SCOPE)
endfunction()

# Adds `target` and everything it links, depth first, to the calling scope's
# epm_visited, epm_archives (the static libraries, each after all those it
# depends on: reversed, the order a single-pass linker such as GNU ld needs)
# and epm_link_options (what is not a target built here: frameworks, system
# libraries, imported libraries; a shared one by name, see
# epm_shared_library_options, a static one by path). A static library's
# INTERFACE_LINK_LIBRARIES holds its private dependencies too, as
# $<LINK_ONLY:...>.
function(epm_visit_link_closure target)
    list(APPEND epm_visited ${target})
    get_target_property(links ${target} INTERFACE_LINK_LIBRARIES)
    get_target_property(options ${target} INTERFACE_LINK_OPTIONS)

    if (options)
        list(APPEND epm_link_options ${options})
    endif ()

    if (NOT links)
        set(links)
    endif ()

    foreach (link IN LISTS links)
        string(REGEX REPLACE "^\\$<LINK_ONLY:(.*)>$" "\\1" link "${link}")

        if (NOT TARGET "${link}")
            # An imported target out of this directory's scope (they are
            # directory-scoped) would otherwise reach the linker as -lFoo::Foo.
            if (link MATCHES "::")
                message(FATAL_ERROR "${target} links ${link}, which is no target "
                        "here: find_package it in ${CMAKE_CURRENT_LIST_DIR}"
                        "/CMakeLists.txt before the walk, so the recipe SDK gets "
                        "what it links")
            endif ()

            if (link AND IS_ABSOLUTE "${link}")
                epm_shared_library_options("${link}" library_options)

                if (library_options)
                    set(link "${library_options}")
                endif ()
            endif ()

            if (link)
                list(APPEND epm_link_options ${link})
            endif ()

            continue()
        endif ()

        get_target_property(aliased ${link} ALIASED_TARGET)

        if (aliased)
            set(link ${aliased})
        endif ()

        if (NOT link IN_LIST epm_visited)
            epm_visit_link_closure(${link})
        endif ()
    endforeach ()

    get_target_property(type ${target} TYPE)
    get_target_property(imported ${target} IMPORTED)

    if (imported)
        set(library_options "")

        if (type STREQUAL "SHARED_LIBRARY" OR type STREQUAL "UNKNOWN_LIBRARY")
            epm_imported_location(${target} location)

            if (location)
                epm_shared_library_options("${location}" library_options)
            endif ()
        endif ()

        if (library_options)
            list(APPEND epm_link_options ${library_options})
        elseif (NOT type STREQUAL "INTERFACE_LIBRARY")
            list(APPEND epm_link_options "$<TARGET_LINKER_FILE:${target}>")
        endif ()
    elseif (type STREQUAL "STATIC_LIBRARY")
        list(APPEND epm_archives ${target})
    elseif (NOT type STREQUAL "INTERFACE_LIBRARY")
        message(FATAL_ERROR "epm-recipe links ${target}, a ${type}: a recipe "
                "executable links static libraries only")
    endif ()

    set(epm_visited ${epm_visited} PARENT_SCOPE)
    set(epm_archives ${epm_archives} PARENT_SCOPE)
    set(epm_link_options ${epm_link_options} PARENT_SCOPE)
endfunction()

# Adds the targets whose headers `target`'s public headers may include (its
# public dependencies, transitively, without imported ones) to the calling
# scope's epm_header_targets. Private dependencies are $<LINK_ONLY:...>, which
# is no target name.
function(epm_visit_header_closure target)
    get_target_property(links ${target} INTERFACE_LINK_LIBRARIES)

    if (NOT links)
        return()
    endif ()

    foreach (link IN LISTS links)
        if (NOT TARGET "${link}")
            continue()
        endif ()

        get_target_property(aliased ${link} ALIASED_TARGET)

        if (aliased)
            set(link ${aliased})
        endif ()

        get_target_property(imported ${link} IMPORTED)

        if (imported OR link IN_LIST epm_header_targets)
            continue()
        endif ()

        list(APPEND epm_header_targets ${link})
        epm_visit_header_closure(${link})
    endforeach ()

    set(epm_header_targets ${epm_header_targets} PARENT_SCOPE)
endfunction()

# The recipe SDK: what a recipe executable compiles against and links, in this
# build tree and, after `cmake --install --config <config>`, in the prefix:
#
#   include/          epm-recipe's headers (<epm/Recipe/Recipe.h>, ...) and
#                     those of the libraries in its public interface
#   lib/<config>/     epm-recipe's archive and every static library under it,
#                     each named after its target (libfmt.a, never libfmtd.a),
#                     so that every configuration holds the same file names
#
# Installs of several configurations share one prefix. epm finds them at run
# time (Recipe/RecipeSdk.cpp), so nothing here lists the configurations.
set(EPM_RECIPE_INCLUDE_DESTINATION include)
set(EPM_RECIPE_LIBRARY_DESTINATION lib)

# eacp-core links Threads::Threads privately on Linux, and the imported target
# that find_package made there is not visible here: this one is, so the walk
# takes what it stands for (-pthread, or nothing) instead of its name.
find_package(Threads)

# The same for eacp-network's CURL::libcurl on Linux (HTTP, which a recipe
# never calls, but the archive is linked whole by name).
if (UNIX AND NOT APPLE AND NOT ANDROID)
    find_package(CURL REQUIRED)
endif ()

set(epm_visited)
set(epm_archives)
set(epm_link_options)
epm_visit_link_closure(epm-recipe)
list(REVERSE epm_archives)

# What CMake adds to every link here (on Windows kernel32.lib, user32.lib,
# gdi32.lib and the rest), which the archives rely on as much as on what they
# name themselves; empty elsewhere.
separate_arguments(standard_libraries NATIVE_COMMAND
        "${CMAKE_CXX_STANDARD_LIBRARIES}")
list(APPEND epm_link_options ${standard_libraries})
list(REMOVE_DUPLICATES epm_link_options)

# What the recipe SDK links beyond its archives, for CI logs (--log-level=VERBOSE).
message(VERBOSE "epm: recipe SDK link options: ${epm_link_options}")

set(EPM_SDK_LIBRARIES)
set(EPM_SDK_INSTALLED_LIBRARIES)

foreach (archive IN LISTS epm_archives)
    # Plain object code rather than the bitcode of eacp's Release LTO, which
    # only a linker of the very same compiler version reads, and only on an
    # LTO link: these archives are linked by recipes and other projects.
    set_target_properties(${archive} PROPERTIES INTERPROCEDURAL_OPTIMIZATION_RELEASE OFF)

    set(installed "${CMAKE_STATIC_LIBRARY_PREFIX}${archive}${CMAKE_STATIC_LIBRARY_SUFFIX}")
    list(APPEND EPM_SDK_LIBRARIES "$<TARGET_LINKER_FILE:${archive}>")
    list(APPEND EPM_SDK_INSTALLED_LIBRARIES "${installed}")

    install(FILES "$<TARGET_LINKER_FILE:${archive}>"
            DESTINATION "${EPM_RECIPE_LIBRARY_DESTINATION}/$<CONFIG>"
            RENAME "${installed}")
endforeach ()

set(EPM_SDK_LINK_OPTIONS "${epm_link_options}")

foreach (header IN LISTS epm_recipe_headers)
    get_filename_component(directory "${header}" DIRECTORY)
    install(FILES "${header}"
            DESTINATION "${EPM_RECIPE_INCLUDE_DESTINATION}/epm/${directory}")
endforeach ()

# eacp-core's include directory is the root of every eacp module; a recipe
# gets the modules epm-recipe links: eacp/Core, and eacp/Network, which has no
# include directory of its own (PackageLock.h includes its IPC/Lock.h).
set(epm_header_subdirectories_eacp-core eacp/Core eacp/Network)

function(epm_install_headers from to)
    install(DIRECTORY "${from}/"
            DESTINATION "${to}"
            FILES_MATCHING
            PATTERN "*.h"
            PATTERN "*.hpp"
            PATTERN "*.inl"
            PATTERN "Tests" EXCLUDE
            PATTERN "CMakeFiles" EXCLUDE
            REGEX "/\\.[^/]*$" EXCLUDE)
endfunction()

set(epm_header_targets)
epm_visit_header_closure(epm-recipe)

foreach (target IN LISTS epm_header_targets)
    get_target_property(directories ${target} INTERFACE_INCLUDE_DIRECTORIES)

    if (NOT directories)
        continue()
    endif ()

    foreach (directory IN LISTS directories)
        string(REGEX REPLACE "^\\$<BUILD_INTERFACE:(.*)>$" "\\1" directory
                "${directory}")

        if (directory MATCHES "^\\$<")
            continue()
        endif ()

        if (NOT DEFINED epm_header_subdirectories_${target})
            epm_install_headers("${directory}" "${EPM_RECIPE_INCLUDE_DESTINATION}")
            continue()
        endif ()

        foreach (subdirectory IN LISTS epm_header_subdirectories_${target})
            epm_install_headers("${directory}/${subdirectory}"
                    "${EPM_RECIPE_INCLUDE_DESTINATION}/${subdirectory}")
        endforeach ()
    endforeach ()
endforeach ()

# The SDK as epm-recipe bakes it in (Recipe/RecipeSdk.cpp.in): configure_file
# fills in the variables, file(GENERATE) the generator expressions, which it
# evaluates over epm-recipe's usage requirements, transitively over eacp-core,
# Miro and the rest, one file per configuration. It names epm-recipe's own
# archive among the others, as a string: no build dependency.
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

# The runtime library epm-recipe's objects were compiled against, which a
# recipe's must match (LNK2038 otherwise): the target's MSVC_RUNTIME_LIBRARY,
# else CMake's default for it.
if (MSVC)
    set(EPM_SDK_MSVC_RUNTIME
            "$<IF:$<BOOL:$<TARGET_PROPERTY:epm-recipe,MSVC_RUNTIME_LIBRARY>>,$<TARGET_PROPERTY:epm-recipe,MSVC_RUNTIME_LIBRARY>,MultiThreaded$<$<CONFIG:Debug>:Debug>DLL>")
else ()
    set(EPM_SDK_MSVC_RUNTIME "")
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
        TARGET epm-recipe)
target_sources(epm-recipe PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/Generated/RecipeSdk-$<CONFIG>.cpp")
