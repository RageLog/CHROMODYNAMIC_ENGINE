cd_add_test(material
  SOURCES test_material.cpp
  DEPS    cd::material cd::rhi cd::shader cd::core $<$<BOOL:${CD_ENABLE_VULKAN}>:cd::rhi_vulkan>
)

# M3 W1B Sprint-1: UI variant factory + spec tests.
cd_add_test(ui_variant
  SOURCES test_ui_variant.cpp
  DEPS    cd::material cd::rhi cd::shader cd::core $<$<BOOL:${CD_ENABLE_VULKAN}>:cd::rhi_vulkan>
)
