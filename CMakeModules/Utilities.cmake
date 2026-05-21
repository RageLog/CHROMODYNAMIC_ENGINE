# cmake/Utilities.cmake

set(DFH_UTILITIES_CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}")

macro(get_subdir_list result curdir)
    file(GLOB children RELATIVE ${curdir} ${curdir}/*)
    set(dirlist "")

    foreach(child ${children})
        if(IS_DIRECTORY ${curdir}/${child})
            list(APPEND dirlist ${child})
        endif()
    endforeach()

    set(${result} ${dirlist})
endmacro()

function(dfh_set_runtime_output_dir target_name subdir_name)
    if(CMAKE_CONFIGURATION_TYPES)
        foreach(CONFIG_TYPE ${CMAKE_CONFIGURATION_TYPES})
            string(TOUPPER ${CONFIG_TYPE} CONFIG_TYPE_UPPER)
            string(TOLOWER ${CONFIG_TYPE} CONFIG_TYPE_LOWER)
            set_target_properties(
                ${target_name}
                PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${CONFIG_TYPE_UPPER}
                "${DFH_OUTPUT_ROOT}/${CONFIG_TYPE_LOWER}/${subdir_name}"
            )
        endforeach()

        set(_fallback_cfg "debug")
        list(FIND CMAKE_CONFIGURATION_TYPES "Debug" _has_debug)
        if(_has_debug EQUAL -1)
            list(GET CMAKE_CONFIGURATION_TYPES 0 _first_cfg)
            string(TOLOWER "${_first_cfg}" _fallback_cfg)
        endif()
        set_target_properties(
            ${target_name}
            PROPERTIES RUNTIME_OUTPUT_DIRECTORY
            "${DFH_OUTPUT_ROOT}/${_fallback_cfg}/${subdir_name}"
        )
    else()
        set_target_properties(${target_name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${OUTPUT_DIR}/${subdir_name}")
    endif()
endfunction()

# --- AUTO CREATE LIBRARY ---
# Automatically detects source files and creates a library target.
# Usage: auto_create_library(<target> <namespace> [SHARED|STATIC] [PCH <headers>])
function(auto_create_library target_name namespace)
    set(options STATIC SHARED)
    set(oneValueArgs)
    set(multiValueArgs PCH EXCLUDE_DIRS)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    file(
        GLOB_RECURSE
        SOURCES
        CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
    )
    file(
        GLOB_RECURSE
        HEADERS
        CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    )

    if(ARG_EXCLUDE_DIRS)
        foreach(exclude_dir ${ARG_EXCLUDE_DIRS})
            file(
                GLOB_RECURSE
                EXCLUDE_SOURCES
                CONFIGURE_DEPENDS
                "${CMAKE_CURRENT_SOURCE_DIR}/${exclude_dir}/*.cpp"
                "${CMAKE_CURRENT_SOURCE_DIR}/${exclude_dir}/*.c"
            )
            file(
                GLOB_RECURSE
                EXCLUDE_HEADERS
                CONFIGURE_DEPENDS
                "${CMAKE_CURRENT_SOURCE_DIR}/${exclude_dir}/*.hpp"
                "${CMAKE_CURRENT_SOURCE_DIR}/${exclude_dir}/*.h"
            )
            list(REMOVE_ITEM SOURCES ${EXCLUDE_SOURCES})
            list(REMOVE_ITEM HEADERS ${EXCLUDE_HEADERS})
        endforeach()
    endif()

    string(TOUPPER "${namespace}" UPPER_NS)
    string(TOUPPER "${target_name}" UPPER_TARGET)
    set(BASE_MACRO_NAME "${UPPER_NS}_${UPPER_TARGET}")

    if(NOT SOURCES)
        add_library(${target_name} INTERFACE)
        set(_scope "INTERFACE")
        set(_is_header_only TRUE)

        if(HEADERS)
            set(_headers_target "${target_name}_headers")
            add_custom_target(${_headers_target} SOURCES ${HEADERS})
            source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "Header Files" FILES ${HEADERS})
            set_target_properties(${_headers_target} PROPERTIES FOLDER "Project/Libraries/${target_name}")
            target_sources(${target_name} INTERFACE
                FILE_SET HEADERS
                BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
                FILES ${HEADERS}
            )
        endif()

        message(STATUS "[Auto-Lib] '${target_name}' detected as INTERFACE (Header-only).")
    else()
        set(_lib_type "STATIC")
        set(_is_header_only FALSE)

        if(ARG_SHARED)
            set(_lib_type "SHARED")
        endif()

        add_library(${target_name} ${_lib_type} ${SOURCES} ${HEADERS})
        set(_scope "PUBLIC")

        source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "Source Files" FILES ${SOURCES})
        source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "Header Files" FILES ${HEADERS})

        if("${_lib_type}" STREQUAL "SHARED")
            target_compile_definitions(${target_name} PRIVATE ${BASE_MACRO_NAME}_BUILD_DLL)
            message(STATUS "[Auto-Lib] '${target_name}' created as SHARED.")
        else()
            target_compile_definitions(${target_name} PUBLIC ${BASE_MACRO_NAME}_STATIC)
            message(STATUS "[Auto-Lib] '${target_name}' created as STATIC.")
        endif()
    endif()

    set_target_properties(${target_name} PROPERTIES FOLDER "Project/Libraries/${target_name}")

    add_library(${namespace}::${target_name} ALIAS ${target_name})

    target_include_directories(${target_name} ${_scope}
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
        $<INSTALL_INTERFACE:include>
    )

    target_compile_features(${target_name} ${_scope} cxx_std_23)

    if(TARGET project_warnings)
        if("${_scope}" STREQUAL "INTERFACE")
            target_link_libraries(${target_name} INTERFACE project_warnings)
        else()
            target_link_libraries(${target_name} PRIVATE project_warnings)
        endif()
    endif()

    if(ENABLE_PCH AND ARG_PCH AND NOT "${_scope}" STREQUAL "INTERFACE")
        target_precompile_headers(${target_name} PRIVATE ${ARG_PCH})
    endif()

    if(_is_header_only)
        install(TARGETS ${target_name}
            EXPORT ${PROJECT_NAME}Targets
            FILE_SET HEADERS DESTINATION "include/${namespace}/${target_name}"
        )
    else()
        install(TARGETS ${target_name}
            EXPORT ${PROJECT_NAME}Targets
            LIBRARY DESTINATION lib
            ARCHIVE DESTINATION lib
            RUNTIME DESTINATION bin
        )
        install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/"
            DESTINATION "include/${namespace}/${target_name}"
            FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h"
        )
    endif()
endfunction()

# --- AUTO CREATE EXECUTABLE ---
# Automatically creates an executable from sources in the directory.
function(auto_create_executable target_name)
    set(options)
    set(oneValueArgs OUTPUT_NAME RUNTIME_SUBDIR INSTALL_SUBDIR)
    set(multiValueArgs DEPENDS PCH EXCLUDE_DIRS)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    file(
        GLOB_RECURSE
        SOURCES
        CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
    )
    file(
        GLOB_RECURSE
        HEADERS
        CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    )

    if(ARG_EXCLUDE_DIRS)
        foreach(exclude_dir ${ARG_EXCLUDE_DIRS})
            file(
                GLOB_RECURSE
                EXCLUDE_SOURCES
                CONFIGURE_DEPENDS
                "${CMAKE_CURRENT_SOURCE_DIR}/${exclude_dir}/*.cpp"
                "${CMAKE_CURRENT_SOURCE_DIR}/${exclude_dir}/*.c"
            )
            file(
                GLOB_RECURSE
                EXCLUDE_HEADERS
                CONFIGURE_DEPENDS
                "${CMAKE_CURRENT_SOURCE_DIR}/${exclude_dir}/*.hpp"
                "${CMAKE_CURRENT_SOURCE_DIR}/${exclude_dir}/*.h"
            )
            list(REMOVE_ITEM SOURCES ${EXCLUDE_SOURCES})
            list(REMOVE_ITEM HEADERS ${EXCLUDE_HEADERS})
        endforeach()
    endif()

    source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES} ${HEADERS})

    add_executable(${target_name} ${SOURCES} ${HEADERS})

    set_target_properties(${target_name} PROPERTIES FOLDER "Project/Applications")

    if(ARG_OUTPUT_NAME)
        set_target_properties(${target_name} PROPERTIES OUTPUT_NAME "${ARG_OUTPUT_NAME}")
    endif()

    set(_exe_folder "${target_name}")
    if(ARG_OUTPUT_NAME)
        set(_exe_folder "${ARG_OUTPUT_NAME}")
    endif()
    string(REGEX REPLACE "[^A-Za-z0-9_.-]" "_" _exe_folder "${_exe_folder}")

    set(_runtime_leaf "bin")
    if(ARG_RUNTIME_SUBDIR)
        set(_runtime_leaf "${ARG_RUNTIME_SUBDIR}")
    endif()
    dfh_set_runtime_output_dir(${target_name} "${_exe_folder}/${_runtime_leaf}")

    target_compile_features(${target_name} PRIVATE cxx_std_23)

    if(TARGET project_warnings)
        target_link_libraries(${target_name} PRIVATE project_warnings)
    endif()

    if(ARG_DEPENDS)
        target_link_libraries(${target_name} PRIVATE ${ARG_DEPENDS})
    endif()

    # Sanitizers (Only if globally enabled)
    if(ENABLE_SANITIZERS)
        include(Sanitizers)
        enable_sanitizers(${target_name})
    endif()

    # Pre-compiled Headers (Only if globally enabled)
    if(ENABLE_PCH AND ARG_PCH)
        target_precompile_headers(${target_name} PRIVATE ${ARG_PCH})
    endif()

    if(WIN32)
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E sleep 1
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:${target_name}>
                $<TARGET_FILE_DIR:${target_name}>
            COMMAND_EXPAND_LISTS
        )

        if(TARGET core_logger)
            add_custom_command(TARGET ${target_name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_RUNTIME_DLLS:core_logger>
                    $<TARGET_FILE_DIR:${target_name}>
                COMMAND_EXPAND_LISTS
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:core_logger>
                    $<TARGET_FILE_DIR:${target_name}>
            )
        endif()
    endif()

    add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:${target_name}>/../logs"
        COMMAND ${CMAKE_COMMAND} -E touch
            "$<TARGET_FILE_DIR:${target_name}>/../logs/${_exe_folder}.log"
    )

    # Installation Rules
    set(_install_subdir "${_exe_folder}/bin")
    if(ARG_INSTALL_SUBDIR)
        set(_install_subdir "${ARG_INSTALL_SUBDIR}")
    endif()
    install(TARGETS ${target_name} DESTINATION ${_install_subdir})

    message(STATUS "[Auto-Exe] Executable '${target_name}' created.")
endfunction()

function(dfh_enable_qt_runtime_deploy target_name)
    if(NOT WIN32)
        return()
    endif()

    if(NOT TARGET ${target_name})
        message(WARNING "[QtDeploy] Target '${target_name}' not found; skipping Qt runtime deploy.")
        return()
    endif()

    if(NOT TARGET Qt6::qmake)
        message(STATUS "[QtDeploy] Qt6::qmake target is unavailable; skipping Qt runtime deploy for '${target_name}'.")
        return()
    endif()

    get_target_property(_qt_qmake_executable Qt6::qmake IMPORTED_LOCATION)
    if(NOT _qt_qmake_executable)
        get_target_property(_qt_qmake_executable Qt6::qmake IMPORTED_LOCATION_RELEASE)
    endif()
    if(NOT _qt_qmake_executable)
        get_target_property(_qt_qmake_executable Qt6::qmake IMPORTED_LOCATION_DEBUG)
    endif()
    if(NOT _qt_qmake_executable)
        message(STATUS "[QtDeploy] Qt6::qmake location is unknown; skipping Qt runtime deploy for '${target_name}'.")
        return()
    endif()

    get_filename_component(_qt_bin_dir "${_qt_qmake_executable}" DIRECTORY)
    set(_windeployqt_executable "${_qt_bin_dir}/windeployqt.exe")
    if(NOT EXISTS "${_windeployqt_executable}")
        message(STATUS "[QtDeploy] windeployqt was not found at '${_windeployqt_executable}'.")
        return()
    endif()

    add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -DQT_QMAKE_EXECUTABLE="${_qt_qmake_executable}"
            -DWINDEPLOYQT_EXECUTABLE="${_windeployqt_executable}"
            -DTARGET_BINARY="$<TARGET_FILE:${target_name}>"
            -DTARGET_RUNTIME_DIR="$<TARGET_FILE_DIR:${target_name}>"
            -P "${DFH_UTILITIES_CMAKE_DIR}/DeployQtRuntime.cmake"
        COMMENT "[QtDeploy] Deploying Qt runtime for ${target_name}"
        VERBATIM
    )
endfunction()

# --- AUTO CREATE TEST ---
# Automatically creates a GTest suite.
function(auto_create_test target_name)
    set(options)
    set(oneValueArgs RUNTIME_SUBDIR)
    set(multiValueArgs LINK PCH)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

    if(NOT SOURCES)
        return()
    endif()

    source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES})

    add_executable(${target_name} ${SOURCES})

    set_target_properties(${target_name} PROPERTIES FOLDER "Project/Tests")

    set(_test_runtime_subdir "tests")
    if(ARG_RUNTIME_SUBDIR)
        set(_test_runtime_subdir "${ARG_RUNTIME_SUBDIR}")
    endif()
    dfh_set_runtime_output_dir(${target_name} "${_test_runtime_subdir}")

    # Link GTest
    target_link_libraries(${target_name} PRIVATE GTest::gtest GTest::gtest_main)

    if(ARG_LINK)
        target_link_libraries(${target_name} PRIVATE ${ARG_LINK})
    endif()

    target_compile_features(${target_name} PRIVATE cxx_std_23)

    if(TARGET project_warnings)
        target_link_libraries(${target_name} PRIVATE project_warnings)
    endif()

    # Register with CTest
    include(GoogleTest)
    gtest_discover_tests(${target_name}
        DISCOVERY_MODE PRE_TEST
    )

    # Coverage (Only if globally enabled)
    if(ENABLE_COVERAGE)
        include(CodeCoverage)
        enable_coverage(${target_name})
    endif()

    # PCH (Only if globally enabled)
    if(ENABLE_PCH AND ARG_PCH)
        target_precompile_headers(${target_name} PRIVATE ${ARG_PCH})
    endif()

    if(WIN32)
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E sleep 1
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:${target_name}>
                $<TARGET_FILE_DIR:${target_name}>
            COMMAND_EXPAND_LISTS
        )
    endif()

    message(STATUS "[Auto-Test] Test suite '${target_name}' registered.")
endfunction()
