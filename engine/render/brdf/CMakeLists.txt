# engine/render/brdf — cd::brdf + sub-libraries (phase420)
#
# Layout:
#   include/cd/brdf/         — umbrella header + sub-lib headers
#     ltc/ sheen_clearcoat/ sss/
#   tests/<sub>/             — per-sub-library tests
#   tests/test_brdf.cpp      — umbrella tests
#
# Targets:
#   cd::brdf_ltc             — Heitz 2016 LTC area lights (INTERFACE)
#   cd::brdf_sheen_clearcoat — Charlie sheen + Filament 2-lobe clearcoat (INTERFACE)
#   cd::brdf_sss             — Burley diffusion + Jimenez separable blur (INTERFACE)
#   cd::brdf                 — umbrella aggregating all three (INTERFACE)

# ── cd::brdf_ltc ───────────────────────────────────────────────────────────
cd_add_library(brdf_ltc
  TYPE INTERFACE
  PUBLIC_DEPS
    cd::core
    cd::math
)

# ── cd::brdf_sheen_clearcoat ───────────────────────────────────────────────
cd_add_library(brdf_sheen_clearcoat
  TYPE INTERFACE
  PUBLIC_DEPS cd::core cd::math
)

# ── cd::brdf_sss ───────────────────────────────────────────────────────────
cd_add_library(brdf_sss
  TYPE INTERFACE
  PUBLIC_DEPS
    cd::core
    cd::math
)

# ── cd::brdf (umbrella) ────────────────────────────────────────────────────
cd_add_library(brdf
  TYPE INTERFACE
  PUBLIC_DEPS
    cd::brdf_ltc
    cd::brdf_sheen_clearcoat
    cd::brdf_sss
)

# ── Tests ──────────────────────────────────────────────────────────────────
if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
  add_subdirectory(tests/ltc)
  add_subdirectory(tests/sheen_clearcoat)
  add_subdirectory(tests/sss)
endif()
