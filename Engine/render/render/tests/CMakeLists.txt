cd_add_test(render
  SOURCES test_render.cpp
  DEPS    cd::render cd::rhi cd::core $<$<BOOL:${CD_ENABLE_VULKAN}>:cd::rhi_vulkan>
)
