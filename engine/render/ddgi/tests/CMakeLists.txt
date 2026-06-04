cd_add_test(ddgi
  SOURCES test_ddgi.cpp
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
endif()
