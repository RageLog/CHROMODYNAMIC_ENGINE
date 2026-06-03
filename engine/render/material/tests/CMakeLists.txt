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
