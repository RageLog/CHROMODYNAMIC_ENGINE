cd_add_test(lighting_clusters
  SOURCES test_lighting_clusters.cpp
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
