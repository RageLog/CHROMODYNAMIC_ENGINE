# =============================================================================
# CDBench — `smoke-bench` CMake target
#
# Phase 12.A — performance + profile sweep. Adds a make-style target so
# `cmake --build . --target smoke-bench` rebuilds hello_bench, runs it,
# pipes the JSON output through cd_bench_compare against the committed
# baseline in tests/bench/, and propagates the exit code so CI can
# gate on it.
#
# Usage:
#   include(CDBench)
#   cd_setup_bench()
#
#   then:  cmake --build . --target smoke-bench
#                                       (uses tests/bench/baseline-debug-nvidia.json)
#          cmake --build . --target smoke-bench-capture
#                                       (re-writes the baseline)
#
# Exit code propagates from cd_bench_compare so the bench-regression
# workflow can rely on this target as a single gate.
# =============================================================================
include_guard(GLOBAL)

function(cd_setup_bench)
    if(NOT TARGET cd_sample_hello_bench OR NOT TARGET cd_bench_compare)
        # Either samples or the bench-compare tool is disabled; quietly
        # skip — same pattern CDSmoke / CDGolden use.
        return()
    endif()

    set(_baseline "${CMAKE_SOURCE_DIR}/tests/bench/baseline-debug-nvidia.json")

    add_custom_target(smoke-bench-capture
        COMMAND $<TARGET_FILE:cd_sample_hello_bench> --json=${_baseline}
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "[cd-bench] Capturing fresh baseline → ${_baseline}"
        DEPENDS cd_sample_hello_bench
        VERBATIM
        USES_TERMINAL
    )

    add_custom_target(smoke-bench
        # Two-step: 1) run bench, 2) compare against committed baseline.
        COMMAND ${CMAKE_COMMAND} -E echo "[cd-bench] Running hello_bench → current.json"
        COMMAND $<TARGET_FILE:cd_sample_hello_bench> --json=${CMAKE_BINARY_DIR}/bench-current.json
        COMMAND ${CMAKE_COMMAND} -E echo "[cd-bench] Comparing against ${_baseline}"
        # Threshold is 25% for Debug builds. Debug allocators + ASan-style
        # instrumentation make Debug run-to-run variance noisy; observed
        # noise on this surface peaks around 15-20% for low-ns benches.
        # A tighter threshold would false-trip on noise. Release builds
        # land a second baseline + a tighter gate as a follow-up wave.
        COMMAND $<TARGET_FILE:cd_bench_compare>
                ${_baseline}
                ${CMAKE_BINARY_DIR}/bench-current.json
                --threshold=25
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "[cd-bench] hello_bench + regression gate against committed baseline"
        DEPENDS cd_sample_hello_bench cd_bench_compare
        VERBATIM
        USES_TERMINAL
    )

    message(STATUS "[cd-bench] smoke-bench / smoke-bench-capture targets installed")
endfunction()
