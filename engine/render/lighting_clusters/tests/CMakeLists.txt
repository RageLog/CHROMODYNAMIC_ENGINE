cd_add_test(lighting_clusters
  SOURCES test_lighting_clusters.cpp
  DEPS    cd::lighting_clusters cd::core
)

# 75->100 marathon — ADD-ONLY host-math boundary coverage. Pins the existing
# clip-space sphere->cluster mapping, AABB-vs-sphere tight test, prefix-sum
# (empty/single/dense/monotone), grid-dim linearisation, light-count
# boundaries, out-of-frustum rejection, multi-cluster spanning, and the
# DispatchPass host-side static helpers (group_count / buffer sizes / std430
# POD layout). No device required — runs everywhere; no binning/cull/GLSL
# change (golden-safe).
cd_add_test(lighting_clusters_hostmath
  SOURCES test_lighting_clusters_hostmath.cpp
  DEPS    cd::lighting_clusters cd::core
)

# Phase 672 / Sprint-2 — GPU compute light-culling smoke test for
# cd::render::lighting_clusters::DispatchPass. Skips at runtime when no
# Vulkan ICD is installed (mirrors the gating in tests/vulkan/test_rhi_vulkan.cpp
# and cd::restir_di / cd::ddgi dispatch tests) so CI without a GPU stays green.
if(CD_ENABLE_VULKAN AND TARGET cd_rhi_vulkan)
  cd_add_test(lighting_clusters_dispatch
    SOURCES test_lighting_clusters_dispatch.cpp
    DEPS    cd::lighting_clusters cd::rhi_vulkan
  )
endif()
