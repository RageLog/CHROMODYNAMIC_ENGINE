# cmake version to be used
cmake_minimum_required(VERSION 3.18)

get_filename_component(testerName ${CMAKE_CURRENT_SOURCE_DIR} NAME)
string(REPLACE " " "_" testerName ${testerName})
project(${testerName})

add_executable(${testerName} "${testerName}.cpp")

SUBDIRLIST(SUBDIRS "${CMAKE_SOURCE_DIR}/include/${projectName}")
FOREACH(subdir ${SUBDIRS})
   string(REPLACE " " "_" subdir ${subdir})
   if(EXISTS  "${CMAKE_SOURCE_DIR}/src/${subdir}")
       target_link_libraries(${testerName} PUBLIC ${subdir})
   endif()
   ENDFOREACH()
target_link_libraries(${testerName} PUBLIC gtest_main)
target_link_libraries(${testerName} PRIVATE project_options project_warnings)

set_target_properties (${testerName} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${UNIT_TEST_BIN_OUTPUT_DIR}/${testerName})

add_custom_command(TARGET ${testerName} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:gtest_main>
        $<TARGET_FILE_DIR:${testerName}>)
add_custom_command(TARGET ${testerName} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:gtest>
        $<TARGET_FILE_DIR:${testerName}>)

add_custom_command(TARGET ${testerName} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:gmock_main>
        $<TARGET_FILE_DIR:${testerName}>)
add_custom_command(TARGET ${testerName} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:gmock>
        $<TARGET_FILE_DIR:${testerName}>)
add_test(NAME "${testerName}_tester" COMMAND ${testerName} WORKING_DIRECTORY ${UNIT_TEST_BIN_OUTPUT_DIR}/${testerName})

#clang format
if(ENABLE_clangformat)
    target_clangformat_setup(${testerName})
endif()

