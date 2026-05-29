# engine/render/post_composite — cd::post_composite (Day 28+)
#
# Forward composite pass — single fullscreen FS that consumes the
# scene HDR target + bloom mip0 + depth + G-Buffer normal + TAA
# history and emits a tonemapped LDR frame plus updated history.
#
# Bundles AO / SSR / DOF / atmospheric fog / aerial perspective /
# light shafts / motion blur / TAA / vignette / film grain /
# chromatic aberration into one render-pass — the canonical
# "no separate per-effect render targets" composite for forward
# renderers. Engines that want per-effect passes use the individual
# post_* libs directly.
#
# Header-only — Settings struct + push-constant layout + GLSL VS/FS
# source strings + binding-slot enum.
cd_add_library(post_composite
  TYPE INTERFACE
  PUBLIC_DEPS
    cd::core
    cd::math
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
