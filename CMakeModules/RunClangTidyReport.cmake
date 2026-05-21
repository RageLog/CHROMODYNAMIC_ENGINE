# =============================================================================
# Run Clang-Tidy and write output to outputs/reports/clang-tidy
# =============================================================================

if(NOT DEFINED DFH_SOURCE_DIR OR DFH_SOURCE_DIR STREQUAL "")
    message(FATAL_ERROR "DFH_SOURCE_DIR is required")
endif()

if(NOT DEFINED CLANG_TIDY_EXE OR CLANG_TIDY_EXE STREQUAL "")
    message(FATAL_ERROR "CLANG_TIDY_EXE is required")
endif()

set(TIDY_REPORT_DIR "${DFH_SOURCE_DIR}/outputs/reports/clang-tidy")
file(MAKE_DIRECTORY "${TIDY_REPORT_DIR}")

string(TIMESTAMP TIDY_TIMESTAMP "%Y%m%d_%H%M%S")
set(TIDY_REPORT_FILE "${TIDY_REPORT_DIR}/tidy_${TIDY_TIMESTAMP}.log")

message(STATUS "[StaticAnalysis] Writing clang-tidy report: ${TIDY_REPORT_FILE}")

set(TIDY_COMMAND "")
if(DEFINED RUN_CLANG_TIDY_EXE
   AND NOT RUN_CLANG_TIDY_EXE STREQUAL ""
   AND EXISTS "${RUN_CLANG_TIDY_EXE}"
   AND DEFINED TIDY_BUILD_DIR
   AND EXISTS "${TIDY_BUILD_DIR}/compile_commands.json")
    if(DEFINED PYTHON_EXECUTABLE AND NOT PYTHON_EXECUTABLE STREQUAL "")
        set(TIDY_COMMAND "${PYTHON_EXECUTABLE}" "${RUN_CLANG_TIDY_EXE}")
    else()
        set(TIDY_COMMAND "${RUN_CLANG_TIDY_EXE}")
    endif()
    list(APPEND TIDY_COMMAND "-p" "${TIDY_BUILD_DIR}")
    if(DEFINED CLANG_TIDY_CONFIG AND EXISTS "${CLANG_TIDY_CONFIG}")
        list(APPEND TIDY_COMMAND "-config-file" "${CLANG_TIDY_CONFIG}")
    endif()
    list(APPEND TIDY_COMMAND "-quiet")
else()
    file(GLOB_RECURSE TIDY_SOURCES
        "${DFH_SOURCE_DIR}/libraries/*.cpp"
        "${DFH_SOURCE_DIR}/runtime/*.cpp"
    )

    if(NOT TIDY_SOURCES)
        message(FATAL_ERROR "No source files found for clang-tidy")
    endif()

    set(TIDY_COMMAND "${CLANG_TIDY_EXE}")
    if(DEFINED TIDY_BUILD_DIR AND EXISTS "${TIDY_BUILD_DIR}/compile_commands.json")
        list(APPEND TIDY_COMMAND "-p" "${TIDY_BUILD_DIR}")
    endif()
    if(DEFINED CLANG_TIDY_CONFIG AND EXISTS "${CLANG_TIDY_CONFIG}")
        list(APPEND TIDY_COMMAND "--config-file=${CLANG_TIDY_CONFIG}")
    endif()
    list(APPEND TIDY_COMMAND ${TIDY_SOURCES})
endif()

execute_process(
    COMMAND ${TIDY_COMMAND}
    WORKING_DIRECTORY "${DFH_SOURCE_DIR}"
    OUTPUT_FILE "${TIDY_REPORT_FILE}"
    ERROR_FILE "${TIDY_REPORT_FILE}"
    RESULT_VARIABLE TIDY_RESULT
)

if(NOT TIDY_RESULT EQUAL 0)
    message(FATAL_ERROR "[StaticAnalysis] Clang-Tidy failed. See report: ${TIDY_REPORT_FILE}")
endif()

message(STATUS "[StaticAnalysis] Clang-Tidy completed. Report: ${TIDY_REPORT_FILE}")
