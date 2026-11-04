# Copies every header a recipe can reach -- <epm/Recipe/Recipe.h>, every
# header of epm-core (EPM_HEADERS, paths relative to INCLUDE_ROOT) and
# eacp-core's <eacp/Core/Core.h> -- into
# OUTPUT_DIR, at its path relative to the include directory it was found
# through, so the epm binary can embed them and write them out again on any
# machine. The compiler lists the headers (-M); only those under one of the
# directories in INCLUDE_DIRS_FILE are copied, the standard library's are not.
#
#   cmake -DCOMPILER=<c++> -DSTANDARD=<20> -DINCLUDE_DIRS_FILE=<file>
#         -DEPM_HEADERS=<a;b> -DINCLUDE_ROOT=<dir>
#         -DDEFINITIONS_FILE=<file> -DWORK_DIR=<dir> -DOUTPUT_DIR=<dir>
#         -DSTAMP=<file> -P CollectRecipeHeaders.cmake

file(READ "${INCLUDE_DIRS_FILE}" include_dirs)
file(READ "${DEFINITIONS_FILE}" definitions)
string(STRIP "${include_dirs}" include_dirs)
string(STRIP "${definitions}" definitions)

set(arguments "-std=c++${STANDARD}" -M)
set(roots "")

foreach (directory IN LISTS include_dirs)
    if (directory STREQUAL "")
        continue()
    endif ()

    list(APPEND arguments "-I${directory}")
    file(REAL_PATH "${directory}" root)
    list(APPEND roots "${root}")
endforeach ()

foreach (definition IN LISTS definitions)
    if (NOT definition STREQUAL "")
        list(APPEND arguments "-D${definition}")
    endif ()
endforeach ()

file(MAKE_DIRECTORY "${WORK_DIR}")
set(probe "${WORK_DIR}/probe.cpp")
set(probe_text "#include <epm/Recipe/Recipe.h>\n#include <eacp/Core/Core.h>\n")

foreach (header IN LISTS EPM_HEADERS)
    file(RELATIVE_PATH relative "${INCLUDE_ROOT}" "${header}")
    string(APPEND probe_text "#include <${relative}>\n")
endforeach ()

file(WRITE "${probe}" "${probe_text}")

execute_process(COMMAND "${COMPILER}" ${arguments} "${probe}"
        RESULT_VARIABLE result OUTPUT_VARIABLE dependencies ERROR_VARIABLE error)

if (NOT result EQUAL 0)
    message(FATAL_ERROR "listing the recipe headers failed:\n${error}")
endif ()

string(REPLACE "\\\n" " " dependencies "${dependencies}")
string(REPLACE "\\ " "<space>" dependencies "${dependencies}")
string(REGEX REPLACE "[ \t\r\n]+" ";" dependencies "${dependencies}")

file(REMOVE_RECURSE "${OUTPUT_DIR}")
file(MAKE_DIRECTORY "${OUTPUT_DIR}")

foreach (dependency IN LISTS dependencies)
    string(REPLACE "<space>" " " dependency "${dependency}")

    if (dependency STREQUAL "" OR dependency MATCHES ":$" OR dependency STREQUAL probe)
        continue()
    endif ()

    file(REAL_PATH "${dependency}" header)
    set(best_root "")

    foreach (root IN LISTS roots)
        string(LENGTH "${root}" root_length)
        string(LENGTH "${best_root}" best_length)
        cmake_path(IS_PREFIX root "${header}" NORMALIZE is_under)

        if (is_under AND root_length GREATER best_length)
            set(best_root "${root}")
        endif ()
    endforeach ()

    if (best_root STREQUAL "")
        continue()
    endif ()

    file(RELATIVE_PATH relative "${best_root}" "${header}")
    get_filename_component(destination "${OUTPUT_DIR}/${relative}" DIRECTORY)
    file(MAKE_DIRECTORY "${destination}")
    file(COPY_FILE "${header}" "${OUTPUT_DIR}/${relative}")
endforeach ()

file(TOUCH "${STAMP}")
