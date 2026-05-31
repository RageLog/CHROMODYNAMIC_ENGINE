cd_add_test(restir_di
  SOURCES test_restir_di.cpp
  DEPS    cd::restir_di
)

# Phase 550 / Sprint-1 — GPU dispatch smoke test for cd::restir_di. Skips
# at runtime when no Vulkan ICD is installed (mirrors the gating in
# tests/vulkan/test_rhi_vulkan.cpp) so CI without a GPU stays green.
if(CD_ENABLE_VULKAN AND TARGET cd_rhi_vulkan)
  cd_add_test(restir_di_dispatch
    SOURCES test_restir_di_dispatch.cpp
    DEPS    cd::restir_di cd::rhi_vulkan
  )

  # Phase 571 / Sprint-3 -- A-trous wavelet denoiser smoke. Same Vulkan
  # gating as the Sprint-1 / Sprint-2 dispatch test so CI without a GPU
  # stays green (GTEST_SKIPs at runtime).
  cd_add_test(restir_di_denoiser
    SOURCES test_restir_di_denoiser.cpp
    DEPS    cd::restir_di cd::rhi_vulkan
  )
endif()
