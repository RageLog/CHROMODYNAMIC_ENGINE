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

    # Collect every sample target that cd_add_sample() registered. The
    # smoke target depends on these so `cmake --build --target smoke`
    # always rebuilds stale binaries before running the harness.
    get_property(_cd_samples GLOBAL PROPERTY CD_ALL_SAMPLES)

    if(WIN32)
        if(NOT EXISTS "${_script_ps1}")
            return()
        endif()
        # PowerShell 7 (`pwsh`) if available, else Windows PowerShell 5.1.
        find_program(_pwsh NAMES pwsh powershell DOC "PowerShell host for smoke harness")
        if(NOT _pwsh)
            message(STATUS "[cd-smoke] No PowerShell host found; `smoke` target disabled.")
            return()
        endif()
        # Generator-expression for Debug/Release picks the right bin/<cfg>.
        add_custom_target(smoke
            COMMAND ${_pwsh} -NoProfile -File "${_script_ps1}"
                -BuildDir "${CMAKE_BINARY_DIR}"
                -Config "$<CONFIG>"
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            COMMENT "[cd-smoke] Running every hello_*.exe with --headless"
            VERBATIM
            USES_TERMINAL
        )
        if(_cd_samples)
            add_dependencies(smoke ${_cd_samples})
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
            COMMENT "[cd-smoke] Running every hello_* with --headless"
            VERBATIM
            USES_TERMINAL
        )
        if(_cd_samples)
            add_dependencies(smoke ${_cd_samples})
        endif()
    endif()
    message(STATUS "[cd-smoke] `smoke` target installed (cmake --build . --target smoke)")
endfunction()
