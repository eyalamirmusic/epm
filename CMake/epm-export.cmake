# epm-export.cmake
#
# Injected into a dependency's child build by the epm binary through
#   -DCMAKE_PROJECT_TOP_LEVEL_INCLUDES=<path to this file>
# so it runs right after the dependency's first project() call, without the
# dependency knowing anything about epm.
#
# Once the whole top-level directory has been processed it exports every library
# target the build defines -- the dependency's own and those of anything it pulled
# in with add_subdirectory / CPM (the _deps directories) -- into one namespaced
# export file:
#
#   export(TARGETS ... NAMESPACE ${EPM_EXPORT_NAMESPACE} FILE ${EPM_EXPORT_FILE})
#
# and writes ${EPM_EXPORT_FILE}.names beside it: one exported target name (without
# the namespace) per line, which is what the harvester reads to generate the
# facade targets.
#
# Inputs (cache variables, both required):
#   EPM_EXPORT_FILE       absolute path of the export file to write
#   EPM_EXPORT_NAMESPACE  namespace prefix for the imported targets, e.g. epm_debug::

include_guard(GLOBAL)

foreach (epm_required_var IN ITEMS EPM_EXPORT_FILE EPM_EXPORT_NAMESPACE)
    if (NOT DEFINED ${epm_required_var} OR "${${epm_required_var}}" STREQUAL "")
        message(FATAL_ERROR
                "epm-export.cmake: ${epm_required_var} is not set. This file is "
                "meant to be injected by epm through CMAKE_PROJECT_TOP_LEVEL_INCLUDES "
                "together with -DEPM_EXPORT_FILE=<file> and "
                "-DEPM_EXPORT_NAMESPACE=<namespace>::")
    endif ()
endforeach ()
unset(epm_required_var)

if (NOT IS_ABSOLUTE "${EPM_EXPORT_FILE}")
    message(FATAL_ERROR
            "epm-export.cmake: EPM_EXPORT_FILE must be an absolute path, "
            "got '${EPM_EXPORT_FILE}'")
endif ()

# Appends every buildsystem target defined in `dir` and, recursively, in all of
# its subdirectories (which include the _deps/<pkg>-build directories CPM and
# FetchContent create) to the list named by `out_var`.
function(epm_collect_directory_targets dir out_var)
    get_property(targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
    get_property(subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)

    foreach (subdir IN LISTS subdirs)
        epm_collect_directory_targets("${subdir}" sub_targets)
        list(APPEND targets ${sub_targets})
    endforeach ()

    set(${out_var} "${targets}" PARENT_SCOPE)
endfunction()

function(epm_is_exportable_library target out_var)
    set(${out_var} FALSE PARENT_SCOPE)

    get_target_property(type ${target} TYPE)
    get_target_property(imported ${target} IMPORTED)
    get_target_property(aliased ${target} ALIASED_TARGET)

    if (imported OR aliased)
        return()
    endif ()

    if (type MATCHES "^(STATIC|OBJECT|INTERFACE|SHARED|MODULE)_LIBRARY$")
        set(${out_var} TRUE PARENT_SCOPE)
    endif ()
endfunction()

function(epm_export_all_targets)
    # Scheduled at project() time, this is the first deferred call in the queue.
    # Re-queue once so it runs after any DEFER calls the project itself makes
    # (which may still create targets).
    get_property(requeued GLOBAL PROPERTY EPM_EXPORT_REQUEUED)

    if (NOT requeued)
        set_property(GLOBAL PROPERTY EPM_EXPORT_REQUEUED TRUE)
        cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}"
                CALL epm_export_all_targets)
        return()
    endif ()

    epm_collect_directory_targets("${CMAKE_SOURCE_DIR}" all_targets)

    set(exported "")

    foreach (target IN LISTS all_targets)
        epm_is_exportable_library(${target} exportable)

        if (exportable)
            list(APPEND exported ${target})
        endif ()
    endforeach ()

    list(REMOVE_DUPLICATES exported)

    if (NOT exported)
        message(FATAL_ERROR
                "epm-export.cmake: the project defines no library targets to export")
    endif ()

    export(TARGETS ${exported}
            NAMESPACE "${EPM_EXPORT_NAMESPACE}"
            FILE "${EPM_EXPORT_FILE}")

    list(JOIN exported "\n" names)
    file(WRITE "${EPM_EXPORT_FILE}.names" "${names}\n")

    list(LENGTH exported count)
    message(STATUS "epm: exporting ${count} targets to ${EPM_EXPORT_FILE}")
endfunction()

cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL epm_export_all_targets)
