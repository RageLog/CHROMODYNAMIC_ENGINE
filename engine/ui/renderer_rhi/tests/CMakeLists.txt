cd_add_test(submitter
  SOURCES test_submitter.cpp
  DEPS    cd::ui_renderer_rhi cd::ui_renderer cd::rhi cd::core
)

# ≥80→100 marathon -- host-side state + ring vb/ib sizing + scissor-sentinel
# + pre-init/null-input guards + move semantics + Route-A inert-variant
# rejection. Runs against NullDevice ONLY, so it needs NO Vulkan gate and
# executes on every CI host. cd::material is required for the UiVariant /
# UiThemePaletteUbo types used in the no-op-accessor + inert-variant cases.
cd_add_test(submitter_host_state
  SOURCES test_submitter_host_state.cpp
  DEPS    cd::ui_renderer_rhi cd::ui_renderer cd::rhi cd::material cd::shader cd::core
)

# Phase 554 / M3 W1A -- Route B inline GLSL pipeline factory tests.
# Gated on CD_ENABLE_VULKAN because the inline-shader path is best
# exercised against a real backend; the test itself GTEST_SKIPs when
# no Vulkan ICD is present so headless CI keeps passing.
if(CD_ENABLE_VULKAN)
  cd_add_test(submitter_inline
    SOURCES test_submitter_inline.cpp
    DEPS    cd::ui_renderer_rhi cd::ui_renderer cd::rhi cd::rhi_vulkan cd::core
  )

  # Phase 608 / M7 W3 -- Route A cd::material::UiVariant pipeline factory
  # tests. Same gate + GTEST_SKIP fallback as `submitter_inline`. The
  # test additionally needs cd::material (the variant factory) + cd::shader
  # (the glslang compiler probe used in the skip guard).
  cd_add_test(submitter_material_route
    SOURCES test_submitter_material_route.cpp
    DEPS    cd::ui_renderer_rhi cd::ui_renderer cd::rhi cd::rhi_vulkan
            cd::material cd::shader cd::core
  )

  # Phase 648 / M10 W3A Sprint-3 -- Route A END-TO-END coverage. Builds a
  # Sprint-2 variant (theme UBO + SDF sampler), feeds the Submitter, calls
  # set_theme_palette / set_sdf_atlas, pushes 4 quads + 1 glyph through the
  # batcher, records into a real Vulkan command buffer. Same gates as
  # submitter_material_route (Vulkan ICD + glslang).
  cd_add_test(submitter_material_route_end_to_end
    SOURCES test_submitter_material_route_end_to_end.cpp
    DEPS    cd::ui_renderer_rhi cd::ui_renderer cd::rhi cd::rhi_vulkan
            cd::material cd::shader cd::core
  )
endif()
