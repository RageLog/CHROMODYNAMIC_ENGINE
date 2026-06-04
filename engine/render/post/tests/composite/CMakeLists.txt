cd_add_test(post_composite
  SOURCES test_post_composite.cpp
  DEPS    cd::post_composite
)

# phase691 — M14 W4 — Vulkan-gated integration smoke for the composite
# DDGI + ReSTIR hook. Forces both flags ON locally for the test (does
# NOT touch the user's project-wide CD_COMPOSITE_USE_DDGI / _USE_RESTIR
# options) so we can verify the hook FS compiles + pipeline-creates +
# dispatches without Vulkan validation errors regardless of the global
# default-OFF policy. Skipped at runtime when no Vulkan ICD is present.
if(CD_ENABLE_VULKAN AND TARGET cd_rhi_vulkan AND TARGET cd_shader)
  cd_add_test(composite_ddgi_restir_hook
    SOURCES test_composite_ddgi_restir_hook.cpp
    DEPS    cd::post_composite cd::rhi cd::rhi_vulkan cd::shader
  )
  target_compile_definitions(cd_test_composite_ddgi_restir_hook PRIVATE
    CD_COMPOSITE_USE_DDGI=1
    CD_COMPOSITE_USE_RESTIR=1
  )
endif()
