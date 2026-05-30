# engine/render/post/exposure — Reinhard log-avg auto-exposure
#
# cd::post_exposure — CPU kernel (phase 454: log-avg luminance + EMA
# smoothing + EV-to-multiplier conversion) PLUS the GPU log-luminance
# reduction skeleton (phase 461). The CPU kernel is unit-testable
# headless; the GpuReduction header-only helper uses cd::rhi so the
# library now exposes the RHI surface to consumers.

cd_add_library(post_exposure
  TYPE STATIC
  SOURCES
    src/Exposure.cpp
  PUBLIC_DEPS
    cd::core
    cd::rhi
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
