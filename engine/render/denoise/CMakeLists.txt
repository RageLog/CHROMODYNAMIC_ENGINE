# engine/render/denoise — cd::denoise (Day 30/J)
# Dammertz 2010 edge-aware a-trous wavelet denoiser + OIDN API slot.
# A-trous is the bundled default (no external deps); OIDN integration
# is opt-in via CD_ENABLE_OIDN once the dep is wired through vcpkg /
# FetchContent.
cd_add_library(denoise
  TYPE INTERFACE
  PUBLIC_DEPS
    cd::core
    cd::math
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
