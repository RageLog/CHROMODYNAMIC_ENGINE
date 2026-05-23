cd_add_test(material
  SOURCES test_material.cpp
  DEPS    cd::material cd::rhi cd::shader cd::core $<$<BOOL:${CD_ENABLE_VULKAN}>:cd::rhi_vulkan>
)
