if(EXISTS "${CMAKE_SOURCE_DIR}/../02_Documents/License")
    install(FILES "${CMAKE_SOURCE_DIR}/../02_Documents/License" DESTINATION .)
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/../02_Documents/License")
endif()

# FAZ 9: System libraries (Windows)
if(WIN32)
    include(InstallRequiredSystemLibraries)
endif()

set(CPACK_PACKAGE_NAME "${PROJECT_NAME}")
set(CPACK_PACKAGE_VENDOR "DtForHil Inc")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Digital Twin HIL Simulation Software")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${PROJECT_VERSION_PATCH}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "DtForHil")
set(CPACK_PACKAGE_DIRECTORY "${DFH_OUTPUT_ROOT}/packages")

if(WIN32)
    find_program(MAKENSIS_EXE NAMES makensis)
    if(MAKENSIS_EXE)
        set(CPACK_GENERATOR "ZIP;NSIS")
    else()
        set(CPACK_GENERATOR "ZIP")
        message(WARNING "[Packaging] NSIS tool 'makensis' not found. CPack NSIS package is disabled.")
    endif()
    set(CPACK_NSIS_DISPLAY_NAME "${PROJECT_NAME} ${PROJECT_VERSION}")
    set(CPACK_NSIS_PACKAGE_NAME "${PROJECT_NAME}")
    set(CPACK_NSIS_MODIFY_PATH OFF)
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
else()
    set(CPACK_GENERATOR "TGZ")
endif()

include(CPack)

add_custom_target(package_cpack
    COMMAND ${CMAKE_CPACK_COMMAND} -C $<CONFIG> --verbose
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Building CPack packages..."
    VERBATIM
)

set(_dfh_package_build_targets
    common
    communication
    json_config
    xml_config
    configuration
    core
    hardware
    integration
    core_logger
    safety_monitor
    lua_engine
    python_engine
    bdd
    twin
    ui_imgui
    ui_qt
    DfhHost
)

get_property(_dfh_package_extra_targets GLOBAL PROPERTY DFH_PACKAGE_EXTRA_TARGETS)
if(_dfh_package_extra_targets)
    list(APPEND _dfh_package_build_targets ${_dfh_package_extra_targets})
endif()

foreach(_target_name IN LISTS _dfh_package_build_targets)
    if(TARGET ${_target_name})
        add_dependencies(package_cpack ${_target_name})
    endif()
endforeach()

if(WIN32)
    set(_dfh_runtime_output_name "DfhHost")
    set(_dfh_runtime_exe_name "${_dfh_runtime_output_name}.exe")
    if(TARGET DfhHost)
        get_target_property(_dfh_runtime_output_name DfhHost OUTPUT_NAME)
        if(_dfh_runtime_output_name)
            set(_dfh_runtime_exe_name "${_dfh_runtime_output_name}.exe")
        endif()
    endif()

    set(_dfh_package_configs "")
    if(CMAKE_CONFIGURATION_TYPES)
        set(_dfh_package_configs ${CMAKE_CONFIGURATION_TYPES})
    elseif(CMAKE_BUILD_TYPE)
        set(_dfh_package_configs ${CMAKE_BUILD_TYPE})
    else()
        set(_dfh_package_configs Release)
    endif()

    set(_dfh_nsis_targets "")
    foreach(_cfg IN LISTS _dfh_package_configs)
        string(TOLOWER "${_cfg}" _cfg_lower)
        string(TOUPPER "${_cfg}" _cfg_upper)

        set(_source_bin_dir "${DFH_OUTPUT_ROOT}/${_cfg_lower}/${_dfh_runtime_output_name}/bin")
        set(_deploy_dir "${DFH_OUTPUT_ROOT}/${_cfg_lower}/deploy/nsis")
        set(_script_out "${CMAKE_BINARY_DIR}/packaging/nsis/DtForHil-${_cfg_lower}.nsi")
        set(_installer_out "${_deploy_dir}/DtForHil-${PROJECT_VERSION}-${_cfg_lower}-setup.exe")
        file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/packaging/nsis")

        file(TO_NATIVE_PATH "${_source_bin_dir}" DFH_NSIS_SOURCE_BIN_DIR)
        file(TO_NATIVE_PATH "${_deploy_dir}" DFH_NSIS_DEPLOY_DIR)
        file(TO_NATIVE_PATH "${_installer_out}" DFH_NSIS_INSTALLER_OUT)

        set(DFH_NSIS_APP_NAME "${PROJECT_NAME}")
        set(DFH_NSIS_APP_VERSION "${PROJECT_VERSION}")
        set(DFH_NSIS_APP_PUBLISHER "DtForHil Inc")
        set(DFH_NSIS_APP_EXE "${_dfh_runtime_exe_name}")
        set(DFH_NSIS_INSTALL_DIR "$PROGRAMFILES64\\DtForHil\\${_cfg}")
        set(DFH_NSIS_CONFIG_NAME "${_cfg}")

        configure_file(
            "${CMAKE_SOURCE_DIR}/packaging/nsis/DtForHilInstaller.nsi.in"
            "${_script_out}"
            @ONLY
        )

        if(MAKENSIS_EXE)
            set(_target_name "nsis_${_cfg_lower}")
            add_custom_target(${_target_name}
                COMMAND ${CMAKE_COMMAND} -E make_directory "${_deploy_dir}"
                COMMAND ${MAKENSIS_EXE} "${_script_out}"
                WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
                COMMENT "Building NSIS installer (${_cfg})..."
                VERBATIM
            )

            if(TARGET DfhHost)
                add_dependencies(${_target_name} DfhHost)
            endif()

            list(APPEND _dfh_nsis_targets ${_target_name})
        endif()
    endforeach()

    if(MAKENSIS_EXE)
        add_custom_target(nsis)
        foreach(_target_name IN LISTS _dfh_nsis_targets)
            add_dependencies(nsis ${_target_name})
        endforeach()
    endif()
endif()
