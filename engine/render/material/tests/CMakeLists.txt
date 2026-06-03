cd_add_test(material
  SOURCES test_material.cpp
  DEPS    cd::material cd::rhi cd::shader cd::core $<$<BOOL:${CD_ENABLE_VULKAN}>:cd::rhi_vulkan>
)

# M3 W1B Sprint-1: UI variant factory + spec tests.
cd_add_test(ui_variant
  SOURCES test_ui_variant.cpp
  DEPS    cd::material cd::rhi cd::shader cd::core $<$<BOOL:${CD_ENABLE_VULKAN}>:cd::rhi_vulkan>
)

# Phase 639 / T1.9: MaterialInstance metallic/roughness CPU accessors
# contract — see engine/render/material/README.md "G-buffer MRT channel
# contract" + docs/AUDIT/learned-lessons-curtain-reflection-2026-06-03.md.
cd_add_test(material_instance_mr_contract
  SOURCES test_material_instance_mr_contract.cpp
  DEPS    cd::material cd::rhi cd::core
)

# Phase 646 / T1.16: MaterialInstance alpha-mode predicate accessors
# (is_blend / is_mask / is_opaque) — prerequisite for T1.15 two-pass alpha
# render order. Downstream call sites branch with is_blend() / is_mask()
# instead of comparing the AlphaMode enum directly.
cd_add_test(material_instance_alpha_predicates
  SOURCES test_material_instance_alpha_predicates.cpp
  DEPS    cd::material cd::rhi cd::core
)
