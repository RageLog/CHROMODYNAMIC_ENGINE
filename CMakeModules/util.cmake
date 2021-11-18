# cmake version to be used
cmake_minimum_required(VERSION 3.18)

MACRO(SUBDIRLIST result curdir)
    FILE(GLOB children RELATIVE ${curdir} ${curdir}/*)
    SET(dirlist "")

    FOREACH(child ${children})
        IF(IS_DIRECTORY ${curdir}/${child})
        LIST(APPEND dirlist ${child})
        ENDIF()
    ENDFOREACH()
    
    SET(${result} ${dirlist})
ENDMACRO()


#example .cmake file and function description
function(print_dir_value)
    message(STATUS "CMake Source dir ${CMAKE_SOURCE_DIR}")
    message(STATUS "CMake Binary dir ${CMAKE_BINARY_DIR}")
    message(STATUS "${PROJECT_NAME} Source dir ${PROJECT_SOURCE_DIR}")
    message(STATUS "${PROJECT_NAME} Binary dir ${PROJECT_BINARY_DIR}")
    message(STATUS "${PROJECT_NAME} version ${PROJECT_VERSION}")
    message(STATUS "Current Source dir  ${CMAKE_CURRENT_SOURCE_DIR}")
    message(STATUS "Current Binary dir  ${CMAKE_CURRENT_BINARY_DIR}")
    message(STATUS "Current script file ${CMAKE_CURRENT_LIST_FILE}")
    message(STATUS "Current script dir ${CMAKE_CURRENT_LIST_DIR}")
endfunction()
