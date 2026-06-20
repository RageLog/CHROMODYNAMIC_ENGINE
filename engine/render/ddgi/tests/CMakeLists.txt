cd_add_test(ddgi
  SOURCES test_ddgi.cpp
  DEPS    cd::ddgi
)

# 80->100 marathon -- host-side probe-grid / atlas / octahedral / fallback
# edge + negative coverage (probe_count 0/1/asymmetric, flat_index <-> shader
# decode round-trip, partial out-of-bounds world->probe mapping, trilinear
# edge-clamp, atlas dims + zero-atlas guard + -Z fold UV, dense octahedral
# round-trip, ambient_fallback branches, TraceSettings defaults). CPU-only
# header math -- runs everywhere (no Vulkan ICD needed). Golden-safe.
cd_add_test(ddgi_probe_math
  SOURCES test_ddgi_probe_math.cpp
  DEPS    cd::ddgi
)

# 80->100 marathon -- host-side DispatchPass / push-constant / UBO / GLSL
# validation that needs no Vulkan device (PC + UBO POD sizes, GLSL source
# contract, CPU-stub execute_* validators + counters). CPU-only.
cd_add_test(ddgi_pass_host
  SOURCES test_ddgi_pass_host.cpp
  DEPS    cd::ddgi
)

# phase549 — standalone library test for the GPU dispatch path. Skips at
# runtime on hosts without a Vulkan ICD; only registered when the Vulkan
# backend is enabled in this build.
if(CD_ENABLE_VULKAN AND TARGET cd_rhi_vulkan)
  cd_add_test(ddgi_dispatch
    SOURCES test_ddgi_dispatch.cpp
    DEPS    cd::ddgi cd::rhi cd::rhi_vulkan
  )

  # phase680 — Sprint-5: full DDGI pipeline compose smoke test
  #            (trace -> blend_irradiance + blend_visibility -> sample).
  cd_add_test(ddgi_full_pipeline
    SOURCES test_ddgi_full_pipeline.cpp
    DEPS    cd::ddgi cd::rhi cd::rhi_vulkan
  )

  # 80->100 marathon -- GPU-gated edge / negative path coverage (set_inv_vp /
  # set_sun_light pre-init + smoke-variant rejection, bind_sample_resources
  # null-view + pre-init rejection, single-probe minimum grid, shutdown
  # idempotence, FullPipeline set-forwarder error channel). Same Vulkan gating
  # -- GTEST_SKIPs without a GPU. Golden-safe (no rendered-output change).
  cd_add_test(ddgi_edge_cases
    SOURCES test_ddgi_edge_cases.cpp
    DEPS    cd::ddgi cd::rhi cd::rhi_vulkan
  )
endif()
