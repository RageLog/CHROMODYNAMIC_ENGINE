# engine/render/rhi_metal — cd::rhi_metal
#
# macOS / iOS Metal backend for cd::rhi. The .cpp is gated by
# `#if defined(__APPLE__)`; on every other platform the TU compiles
# to a nullptr-returning factory (Wave 79 CoreAudio pattern).
#
# Full Metal implementation lands in Phase 9 Sprint 1 — this wave
# (Wave 97) lands the public header + factory stub so platform-
# agnostic code can compile against the symbol unconditionally.

cd_add_library(rhi_metal
  TYPE STATIC
  SOURCES src/MetalDevice.cpp
  PUBLIC_DEPS cd::core cd::rhi
)

# On Apple, the implementation links the Metal framework. Stub TU
# on non-Apple = no link deps. Frameworks linked PRIVATE so the
# downstream link line stays clean.
if(APPLE)
  find_library(CD_METAL_FRAMEWORK Metal)
  find_library(CD_FOUNDATION_FRAMEWORK Foundation)
  if(CD_METAL_FRAMEWORK AND CD_FOUNDATION_FRAMEWORK)
    target_link_libraries(cd_rhi_metal PRIVATE
      ${CD_METAL_FRAMEWORK}
      ${CD_FOUNDATION_FRAMEWORK})
  endif()
endif()
