option(ENABLE_COVERAGE "Enable code coverage instrumentation" OFF)

function(enable_coverage target_name)
    if(ENABLE_COVERAGE)
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(${target_name} PRIVATE --coverage -O0 -g)
            target_link_libraries(${target_name} PRIVATE --coverage)
            
            message(STATUS "[Coverage] Instrumentation enabled for: ${target_name}")
            
        elseif(MSVC)
            message(WARNING "[Coverage] MSVC support requires external tools (OpenCppCoverage). Flags skipped.")
        endif()
    endif()
endfunction()

function(add_coverage_report)
    if(ENABLE_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        find_program(GCOVR_PATH gcovr)
        if(GCOVR_PATH)
            add_custom_target(coverage_report
                COMMAND ${GCOVR_PATH} --root ${CMAKE_SOURCE_DIR} --filter "${CMAKE_SOURCE_DIR}/libraries/*" --filter "${CMAKE_SOURCE_DIR}/runtime/*" --exclude "${CMAKE_SOURCE_DIR}/tests/*" --html --html-details -o coverage.html
                WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
                COMMENT "Generating HTML coverage report..."
            )
            message(STATUS "[Coverage] 'coverage_report' target added (requires gcovr).")
        endif()
    endif()
endfunction()