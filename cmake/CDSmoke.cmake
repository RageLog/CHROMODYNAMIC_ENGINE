# =============================================================================
# CDSmoke — `smoke` custom target that runs the sample smoke harness against
# the current build's bin directory.
#
# Usage:
#   include(CDSmoke)
#   cd_setup_smoke()
#   # then: cmake --build . --target smoke
#
# Behaviour: the target invokes scripts/run_all_samples.{ps1,sh} pointing at
# this build's bin/ directory. Exit code propagates so CI can gate on it.
# =============================================================================
include_guard(GLOBAL)

function(cd_setup_smoke)
    set(_script_ps1 "${CMAKE_SOURCE_DIR}/scripts/run_all_samples.ps1")
    set(_script_sh  "${CMAKE_SOURCE_DIR}/scripts/run_all_samples.sh")

    get_property(_cd_apps GLOBAL PROPERTY CD_ALL_APPS)

    if(WIN32)
        if(NOT EXISTS "${_script_ps1}")
            return()
        endif()
        find_program(_pwsh NAMES pwsh powershell DOC "PowerShell host for smoke harness")
        if(NOT _pwsh)
            message(STATUS "[cd-smoke] No PowerShell host found; `smoke` target disabled.")
            return()
        endif()
        add_custom_target(smoke
            COMMAND ${_pwsh} -NoProfile -File "${_script_ps1}"
                -BuildDir "${CMAKE_BINARY_DIR}"
                -Config "$<CONFIG>"
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            COMMENT "[cd-smoke] Running application smoke harness"
            VERBATIM
            USES_TERMINAL
        )
        if(_cd_apps)
            add_dependencies(smoke ${_cd_apps})
        endif()
    else()
        if(NOT EXISTS "${_script_sh}")
            return()
        endif()
        find_program(_bash NAMES bash DOC "Bash for smoke harness")
        if(NOT _bash)
            message(STATUS "[cd-smoke] bash not found; `smoke` target disabled.")
            return()
        endif()
        add_custom_target(smoke
            COMMAND ${CMAKE_COMMAND} -E env
                    BUILD_DIR=${CMAKE_BINARY_DIR}
                    CONFIG=$<CONFIG>
                    ${_bash} "${_script_sh}"
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            COMMENT "[cd-smoke] Running application smoke harness"
            VERBATIM
            USES_TERMINAL
        )
        if(_cd_apps)
            add_dependencies(smoke ${_cd_apps})
        endif()
    endif()
    message(STATUS "[cd-smoke] `smoke` target installed (cmake --build . --target smoke)")
endfunction()
