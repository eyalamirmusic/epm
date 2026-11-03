set(epm_module ${PROJECT_SOURCE_DIR}/CMake/epm.cmake)

function(add_consumer_test name script project)
    add_test(NAME Consumer/${name}
            COMMAND ${CMAKE_COMMAND}
            -DPROJECT_DIR=${CMAKE_CURRENT_SOURCE_DIR}/projects/${project}
            -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/consumer-${name}
            -DEPM_EXECUTABLE=$<TARGET_FILE:epm>
            -DEPM_MODULE=${epm_module}
            ${ARGN}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/${script})
endfunction()

function(add_consumer_project_test name expected_packages)
    add_consumer_test(${name} RunConsumerProject.cmake ${name}
            -DEXPECTED_PACKAGES=${expected_packages})
endfunction()

add_consumer_project_test(github 1)
add_consumer_project_test(cpm 4)
add_consumer_project_test(fetchcontent 1)
add_consumer_project_test(archive 2)
add_consumer_project_test(cpm-stub 1)
add_consumer_project_test(patch-beside-file 1)
add_consumer_project_test(cpm-compat 2)
add_consumer_test(submodules RunSubmoduleProject.cmake submodules)
add_consumer_test(git-edge-cases RunGitEdgeCasesProject.cmake git-edge-cases)
add_consumer_test(archiveWithWrongHash RunBadHashProject.cmake archive-bad-hash)

