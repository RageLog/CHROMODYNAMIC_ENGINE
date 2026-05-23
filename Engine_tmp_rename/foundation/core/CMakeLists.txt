# engine/foundation/core/CMakeLists.txt — cd::core
# Phase 2 Sprint S2.0 — minimal library (Defines + ErrorCode + Result + Version)
# Full P0 implementation (Handle, HandleStore, CVarRegistry) lands Sprint S2.1.

cd_add_library(core
  SOURCES
    src/CorePlaceholder.cpp
    src/ErrorFormat.cpp
)

# Stamp the top-level CMake `project(... VERSION X.Y.Z)` into cd_core as
# PUBLIC compile definitions so cd::core::kEngineVersion (Version.hpp)
# reflects the actual project version end-to-end. Pre-v0.25.0 the
# constant was hard-coded "0.1.0" in the header and drifted from
# PROJECT_VERSION every release.
target_compile_definitions(cd_core
  PUBLIC
    CD_ENGINE_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    CD_ENGINE_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    CD_ENGINE_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
