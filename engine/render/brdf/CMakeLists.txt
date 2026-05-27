# engine/render/brdf — cd::brdf (Phase 234 umbrella)
#
# Aggregates the three BRDF lobe libs. Each preserves its own namespace +
# header; this lib gives consumers a single dep target for advanced
# material setups.
#
# Member libs:
#   cd::brdf_ltc              (Heitz 2016 area-light analytic integration)
#   cd::brdf_sheen_clearcoat  (Charlie sheen + Filament 2-lobe clearcoat)
#   cd::brdf_sss              (Burley diffusion + Jimenez separable blur)
cd_add_library(brdf
  TYPE INTERFACE
  PUBLIC_DEPS
    cd::brdf_ltc
    cd::brdf_sheen_clearcoat
    cd::brdf_sss
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
