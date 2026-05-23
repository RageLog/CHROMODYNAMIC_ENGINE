# =============================================================================
# CDGolden — `smoke-golden-capture` + `smoke-golden-compare` build targets.
#
# Phase 11 Track A — Axis A of the v1.0 maturity gate.
#
# Usage:
#   include(CDGolden)
#   cd_setup_golden()
#   # then:
#   #   cmake --build . --target smoke-golden-capture   (rewrites references)
#   #   cmake --build . --target smoke-golden-compare   (CI gate)
#
# Behaviour: dispatches to scripts/run_golden.ps1 (Windows) or
# scripts/run_golden.sh (POSIX). The script enumerates the wired
# samples in one place; this CMake module just wires the dispatch.
#
# Exit code propagates so CI can gate on smoke-golden-compare.
# =============================================================================
include_guard(GLOBAL)

function(cd_setup_golden)
    set(_script_ps1 "${CMAKE_SOURCE_DIR}/scripts/run_golden.ps1")
    set(_script_sh  "${CMAKE_SOURCE_DIR}/scripts/run_golden.sh")

    get_property(_cd_samples GLOBAL PROPERTY CD_ALL_SAMPLES)

    if(WIN32)
        if(NOT EXISTS "${_script_ps1}")
            return()
        endif()
        find_program(_pwsh NAMES pwsh powershell DOC "PowerShell host for golden harness")
        if(NOT _pwsh)
            message(STATUS "[cd-golden] No PowerShell host found; golden targets disabled.")
            return()
        endif()
        add_custom_target(smoke-golden-capture
            COMMAND ${_pwsh} -NoProfile -File "${_script_ps1}"
                -Mode capture
                -BuildDir "${CMAKE_BINARY_DIR}"
                -Config "$<CONFIG>"
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            COMMENT "[cd-golden] Re-capturing golden references (overwrites tests/golden/*.png)"
            VERBATIM
            USES_TERMINAL
        )
        add_custom_target(smoke-golden-compare
            COMMAND ${_pwsh} -NoProfile -File "${_script_ps1}"
                -Mode compare
                -BuildDir "${CMAKE_BINARY_DIR}"
                -Config "$<CONFIG>"
                -DiffDir "${CMAKE_BINARY_DIR}/golden-diff"
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            COMMENT "[cd-golden] Comparing rendered output against tests/golden/*.png"
            VERBATIM
            USES_TERMINAL
        )
        if(_cd_samples)
            add_dependencies(smoke-golden-capture ${_cd_samples})
            add_dependencies(smoke-golden-compare ${_cd_samples})
        endif()
        message(STATUS "[cd-golden] golden targets installed: smoke-golden-capture / smoke-golden-compare")
    else()
        # POSIX: the .sh wrapper is a follow-up. Track A Part 1 ships the
        # Windows driver where the human reviewer captures references;
        # Track B Part 1 will add Linux SwiftShader and the .sh wrapper
        # together so the same script works in both runners.
        if(NOT EXISTS "${_script_sh}")
            message(STATUS "[cd-golden] scripts/run_golden.sh missing; targets disabled on POSIX (queued in Track B).")
            return()
        endif()
    endif()
endfunction()
