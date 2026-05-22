# engine/foundation/core/CMakeLists.txt — cd::core
# Phase 2 Sprint S2.0 — minimal library (Defines + ErrorCode + Result + Version)
# Full P0 implementation (Handle, HandleStore, CVarRegistry) lands Sprint S2.1.

cd_add_library(core
  SOURCES
    src/CorePlaceholder.cpp
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
