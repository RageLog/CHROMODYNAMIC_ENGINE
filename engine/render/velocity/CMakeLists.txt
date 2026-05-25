# engine/render/velocity — cd::velocity (Day 24)
# Per-pixel motion vector pass; prereq for TAA, motion blur, ReSTIR.
cd_add_library(velocity
  TYPE INTERFACE
  PUBLIC_DEPS
    cd::core
    cd::math
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
