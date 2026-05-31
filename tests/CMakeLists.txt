# tests/ — global integration-level tests.
# These tests span multiple engine libraries and live outside the per-library
# tests/ folders so they can import from several cd:: namespaces at once.
#
# Add add_subdirectory(tests) to the root CMakeLists.txt under CD_ENABLE_TESTING.

# Phase 527: ReSTIR DI + GI WRS math cross-library test.
cd_add_test(restir_math
  SOURCES test_restir_math.cpp
  DEPS
    cd::restir_di
    cd::restir_gi
)
