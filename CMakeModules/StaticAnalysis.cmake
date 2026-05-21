# =============================================================================
# STATIC ANALYSIS - Clang-Tidy & Clang-Format
# =============================================================================

if(ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES "clang-tidy")
    find_program(RUN_CLANG_TIDY_EXE NAMES "run-clang-tidy" "run-clang-tidy.py")
    find_package(Python3 COMPONENTS Interpreter QUIET)

    if(CLANG_TIDY_EXE)
        message(STATUS "[StaticAnalysis] Clang-Tidy found: ${CLANG_TIDY_EXE}")
        if(RUN_CLANG_TIDY_EXE)
            message(STATUS "[StaticAnalysis] run-clang-tidy found: ${RUN_CLANG_TIDY_EXE}")
        endif()

        set(TIDY_BUILD_DIR "${CMAKE_SOURCE_DIR}/build/tidy-db")
        set(TIDY_COMPILE_DB "${TIDY_BUILD_DIR}/compile_commands.json")

        if(NOT EXISTS "${TIDY_COMPILE_DB}")
            message(STATUS "[StaticAnalysis] Generating compile_commands.json...")
            
            execute_process(
                COMMAND ${CMAKE_COMMAND} 
                    -G "Ninja"
                    -DCMAKE_BUILD_TYPE=Debug
                    -DCMAKE_CXX_STANDARD=${CMAKE_CXX_STANDARD}
                    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
                    -DENABLE_CLANG_TIDY=OFF
                    -DENABLE_CLANG_FORMAT=OFF
                    -DENABLE_TESTING=OFF
                    -S "${CMAKE_SOURCE_DIR}"
                    -B "${TIDY_BUILD_DIR}"
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                RESULT_VARIABLE TIDY_CONFIG_RESULT
                OUTPUT_QUIET
                ERROR_QUIET
            )

            if(NOT TIDY_CONFIG_RESULT EQUAL 0)
                message(WARNING "[StaticAnalysis] Failed to generate compile_commands.json. "
                                "Install Ninja generator or run: cmake -G Ninja -B build/tidy-db")
            endif()
        endif()

        file(GLOB_RECURSE TIDY_SOURCES
            "${CMAKE_SOURCE_DIR}/libraries/*.cpp"
            "${CMAKE_SOURCE_DIR}/runtime/*.cpp"
        )

        set(CLANG_TIDY_CONFIG "${CMAKE_SOURCE_DIR}/.clang-tidy")

        if(TIDY_SOURCES)
            if(EXISTS "${TIDY_COMPILE_DB}")
                set(TIDY_DB_ARG "-p" "${TIDY_BUILD_DIR}")
                message(STATUS "[StaticAnalysis] Using compile database: ${TIDY_COMPILE_DB}")
            else()
                set(TIDY_DB_ARG "")
                message(STATUS "[StaticAnalysis] No compile database, using fallback mode")
            endif()

            if(EXISTS "${CLANG_TIDY_CONFIG}")
                set(TIDY_CONFIG_ARG "--config-file=${CLANG_TIDY_CONFIG}")
            else()
                set(TIDY_CONFIG_ARG "")
            endif()

            add_custom_target(tidy
                COMMAND ${CMAKE_COMMAND}
                    -DDFH_SOURCE_DIR=${CMAKE_SOURCE_DIR}
                    -DCLANG_TIDY_EXE=${CLANG_TIDY_EXE}
                    -DRUN_CLANG_TIDY_EXE=${RUN_CLANG_TIDY_EXE}
                    -DPYTHON_EXECUTABLE=${Python3_EXECUTABLE}
                    -DTIDY_BUILD_DIR=${TIDY_BUILD_DIR}
                    -DCLANG_TIDY_CONFIG=${CLANG_TIDY_CONFIG}
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunClangTidyReport.cmake
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                COMMENT "Running Clang-Tidy static analysis (report -> outputs/reports/clang-tidy)..."
                USES_TERMINAL
                VERBATIM
            )

            add_custom_target(tidy-db
                COMMAND ${CMAKE_COMMAND}
                    -G "Ninja"
                    -DCMAKE_BUILD_TYPE=Debug
                    -DCMAKE_CXX_STANDARD=${CMAKE_CXX_STANDARD}
                    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
                    -DENABLE_CLANG_TIDY=OFF
                    -DENABLE_CLANG_FORMAT=OFF
                    -DENABLE_TESTING=OFF
                    -S "${CMAKE_SOURCE_DIR}"
                    -B "${TIDY_BUILD_DIR}"
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                COMMENT "Regenerating compile_commands.json for clang-tidy..."
                USES_TERMINAL
                VERBATIM
            )

            message(STATUS "[StaticAnalysis] Use 'cmake --build <build> --target tidy' for static analysis")
            message(STATUS "[StaticAnalysis] Use 'cmake --build <build> --target tidy-db' to regenerate compile database")
        endif()

        option(CLANG_TIDY_ON_BUILD "Run clang-tidy during compilation (slow)" OFF)

        if(CLANG_TIDY_ON_BUILD)
            if(MSVC)
                set(_driver_mode ";--extra-arg-before=--driver-mode=cl")
            else()
                set(_driver_mode "")
            endif()

            if(EXISTS "${CLANG_TIDY_CONFIG}")
                set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE};--config-file=${CLANG_TIDY_CONFIG}${_driver_mode}")
            else()
                set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE}${_driver_mode}")
            endif()

            message(STATUS "[StaticAnalysis] Clang-Tidy will run on every build (slower)")
        endif()
    else()
        message(WARNING "[StaticAnalysis] Clang-Tidy enabled but not found!")
    endif()
endif()

if(ENABLE_CLANG_FORMAT)
    find_program(CLANG_FORMAT_EXE NAMES "clang-format")

    if(CLANG_FORMAT_EXE)
        if(ENABLE_FORMAT_ON_BUILD)
            message(STATUS "[StaticAnalysis] clang-format will be attached to build targets")
        else()
            message(STATUS "[StaticAnalysis] clang-format target available (ENABLE_FORMAT_ON_BUILD=OFF)")
        endif()

        function(auto_format_targets _dir)
            get_property(_targets DIRECTORY "${_dir}" PROPERTY BUILDSYSTEM_TARGETS)
            get_property(_subdirs DIRECTORY "${_dir}" PROPERTY SUBDIRECTORIES)

            foreach(_subdir ${_subdirs})
                auto_format_targets("${_subdir}")
            endforeach()

            foreach(_target ${_targets})
                get_target_property(_type ${_target} TYPE)
                get_target_property(_source_dir ${_target} SOURCE_DIR)

                if(NOT "${_source_dir}" MATCHES "dependencies")
                    if(_type MATCHES "EXECUTABLE|STATIC_LIBRARY|SHARED_LIBRARY")
                        add_dependencies(${_target} format)
                    endif()
                endif()
            endforeach()
        endfunction()

        file(GLOB_RECURSE ALL_SOURCES
            "${CMAKE_SOURCE_DIR}/libraries/*.cpp" "${CMAKE_SOURCE_DIR}/libraries/*.hpp"
            "${CMAKE_SOURCE_DIR}/runtime/*.cpp" "${CMAKE_SOURCE_DIR}/runtime/*.hpp"
        )
        add_custom_target(format
            COMMAND "${CLANG_FORMAT_EXE}" -i -style=file ${ALL_SOURCES}
            COMMENT "Formatting sources with Clang-Format..."
            USES_TERMINAL
        )
    endif()
endif()
