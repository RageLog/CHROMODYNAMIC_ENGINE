# CDCoverage.cmake — gcov / llvm-cov code-coverage instrumentation
# CLAUDE.md §3 (Doğruluk / Evidence-Based) + §11 (Q11.3 CI)
#
# Driven by CD_ENABLE_COVERAGE cache variable (OFF by default — coverage
# build is a CI-only opt-in because it slows compile/run by 2-3x).
#
# Compiler support:
#   * GCC / Clang on Linux/macOS → --coverage (compiles with -fprofile-arcs
#     -ftest-coverage, links to libgcov) producing .gcno + .gcda artefacts
#     next to the .o files. `gcovr` aggregates them into HTML / Cobertura.
#   * MSVC → no native support. CI uses OpenCppCoverage as a separate job
#     post-test (out of scope for this module — the CI yaml drives it).
#
# Targets installed when enabled + `gcovr` is on PATH:
#   coverage-clean   delete every .gcda so a re-run starts from zero
#   coverage-report  run gcovr → coverage.html + coverage.xml (Cobertura)
#   coverage-summary  text summary on stdout (fast sanity check)

include_guard(GLOBAL)

option(CD_ENABLE_COVERAGE "Enable gcov / llvm-cov code-coverage instrumentation" OFF)

function(cd_apply_coverage target)
  if(NOT CD_ENABLE_COVERAGE)
    return()
  endif()
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${target} PRIVATE --coverage -O0 -g)
    target_link_options(${target} PRIVATE --coverage)
  elseif(MSVC)
    # Silent on MSVC — OpenCppCoverage handles instrumentation externally.
  endif()
endfunction()

# Install the coverage-* custom targets exactly once, at project scope.
# Idempotent via the include_guard above.
function(cd_install_coverage_targets)
  if(NOT CD_ENABLE_COVERAGE)
    return()
  endif()
  if(NOT (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"))
    return()
  endif()
  find_program(CD_GCOVR_PATH gcovr)
  if(NOT CD_GCOVR_PATH)
    message(STATUS "[cd] CD_ENABLE_COVERAGE=ON but gcovr not on PATH — coverage targets skipped")
    return()
  endif()

  # Clean: delete the .gcda files (the .gcno are build artefacts, never
  # touched here) so the next test pass starts with no recorded data.
  add_custom_target(coverage-clean
    COMMAND ${CMAKE_COMMAND} -E echo "[cd] coverage: clearing .gcda files"
    COMMAND ${CMAKE_COMMAND}
            -DSEARCH_DIR=${CMAKE_BINARY_DIR}
            -P ${CMAKE_SOURCE_DIR}/cmake/CDCoverageClean.cmake
    COMMENT "Clearing .gcda files for a fresh coverage run"
  )

  # Aggregation: scope to engine/ + samples/, exclude tests + vendored deps.
  set(_cd_gcovr_filters
      --root ${CMAKE_SOURCE_DIR}
      --filter "${CMAKE_SOURCE_DIR}/engine/.*"
      --filter "${CMAKE_SOURCE_DIR}/samples/.*"
      --exclude "${CMAKE_SOURCE_DIR}/.*/tests/.*"
      --exclude "${CMAKE_BINARY_DIR}/.*/_deps/.*"
      --exclude "${CMAKE_SOURCE_DIR}/_legacy/.*"
  )

  add_custom_target(coverage-report
    COMMAND ${CD_GCOVR_PATH} ${_cd_gcovr_filters} --html --html-details
            -o coverage.html
    COMMAND ${CD_GCOVR_PATH} ${_cd_gcovr_filters} --xml -o coverage.xml
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "[cd] coverage: HTML + Cobertura XML reports in ${CMAKE_BINARY_DIR}"
  )

  add_custom_target(coverage-summary
    COMMAND ${CD_GCOVR_PATH} ${_cd_gcovr_filters}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "[cd] coverage: line-by-line summary on stdout"
  )

  message(STATUS "[cd] coverage targets registered: coverage-clean / coverage-report / coverage-summary")
endfunction()
