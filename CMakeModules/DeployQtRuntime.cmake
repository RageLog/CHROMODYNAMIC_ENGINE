if(NOT WIN32)
    return()
endif()

foreach(_var_name IN ITEMS QT_QMAKE_EXECUTABLE WINDEPLOYQT_EXECUTABLE TARGET_BINARY TARGET_RUNTIME_DIR)
    if(DEFINED ${_var_name})
        string(REPLACE "\"" "" _normalized_value "${${_var_name}}")
        set(${_var_name} "${_normalized_value}")
    endif()
endforeach()

if(NOT DEFINED TARGET_BINARY OR TARGET_BINARY STREQUAL "" OR NOT EXISTS "${TARGET_BINARY}")
    message(STATUS "[QtDeploy] Target binary is missing; skipping deployment.")
    return()
endif()

if(NOT DEFINED TARGET_RUNTIME_DIR OR TARGET_RUNTIME_DIR STREQUAL "")
    message(STATUS "[QtDeploy] Target runtime directory is missing; skipping deployment.")
    return()
endif()

if(NOT DEFINED QT_QMAKE_EXECUTABLE OR QT_QMAKE_EXECUTABLE STREQUAL "" OR NOT EXISTS "${QT_QMAKE_EXECUTABLE}")
    message(STATUS "[QtDeploy] Qt qmake executable is missing; skipping deployment.")
    return()
endif()

if(NOT DEFINED WINDEPLOYQT_EXECUTABLE OR WINDEPLOYQT_EXECUTABLE STREQUAL "" OR NOT EXISTS "${WINDEPLOYQT_EXECUTABLE}")
    message(STATUS "[QtDeploy] windeployqt executable is missing; skipping deployment.")
    return()
endif()

execute_process(
    COMMAND "${QT_QMAKE_EXECUTABLE}" -query QT_INSTALL_PLUGINS
    OUTPUT_VARIABLE _qt_plugins_dir
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _qt_query_result
)
if(NOT _qt_query_result EQUAL 0)
    set(_qt_plugins_dir "")
endif()

execute_process(
    COMMAND "${WINDEPLOYQT_EXECUTABLE}"
        --no-translations
        --no-opengl-sw
        --no-system-d3d-compiler
        --dir "${TARGET_RUNTIME_DIR}"
        "${TARGET_BINARY}"
    RESULT_VARIABLE _deploy_result
    OUTPUT_VARIABLE _deploy_out
    ERROR_VARIABLE _deploy_err
)
if(NOT _deploy_result EQUAL 0)
    message(STATUS "[QtDeploy] windeployqt fallback path engaged (result=${_deploy_result}).")
endif()

set(_platform_candidates)
if(NOT _qt_plugins_dir STREQUAL "")
    list(APPEND _platform_candidates
        "${_qt_plugins_dir}/platforms/qwindowsd.dll"
        "${_qt_plugins_dir}/platforms/qwindows.dll"
    )
endif()

set(_platform_plugin "")
foreach(_candidate IN LISTS _platform_candidates)
    if(EXISTS "${_candidate}")
        set(_platform_plugin "${_candidate}")
        break()
    endif()
endforeach()

if(NOT _platform_plugin STREQUAL "")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E make_directory "${TARGET_RUNTIME_DIR}/platforms")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_platform_plugin}" "${TARGET_RUNTIME_DIR}/platforms"
        RESULT_VARIABLE _copy_result
    )
    if(NOT _copy_result EQUAL 0)
        message(STATUS "[QtDeploy] failed to copy '${_platform_plugin}' to '${TARGET_RUNTIME_DIR}/platforms'.")
    endif()
else()
    message(STATUS "[QtDeploy] qwindows platform plugin was not found in Qt plugin directories.")
endif()
