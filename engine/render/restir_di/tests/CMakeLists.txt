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

  # Phase 616 / Sprint-4 -- SVGF skeleton smoke. Same Vulkan gating as the
  # Sprint-3 A-trous denoiser test -- GTEST_SKIPs when no Vulkan ICD is
  # installed so CI without a GPU stays green.
  cd_add_test(restir_di_svgf
    SOURCES test_restir_di_svgf.cpp
    DEPS    cd::restir_di cd::rhi_vulkan
  )

  # Phase 669 / Sprint-5 -- SVGF full pipeline chain smoke. Same Vulkan
  # gating as Sprint-4. Verifies the chained moment + variance + 3x A-trous
  # filter actually reduces per-pixel luminance variance on a noisy input.
  cd_add_test(restir_di_full_svgf
    SOURCES test_restir_di_full_svgf.cpp
    DEPS    cd::restir_di cd::rhi_vulkan
  )

  # Phase 681 / Sprint-6 -- Full denoised pipeline smoke. Same Vulkan
  # gating as Sprint-5. Verifies the chained sample -> temporal_reuse ->
  # spatial_reuse -> SVGF facade dispatches without validation errors and
  # advances frame_index_ after each successful execute().
  cd_add_test(restir_di_full_pipeline_denoised
    SOURCES test_restir_di_full_pipeline_denoised.cpp
    DEPS    cd::restir_di cd::rhi_vulkan
  )
endif()
