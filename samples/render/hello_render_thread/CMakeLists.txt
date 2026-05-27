cd_add_sample(hello_render_thread
  SOURCES main.cpp
  DEPS    cd::async_submit cd::material cd::shader cd::render cd::rhi cd::rhi_vulkan
          cd::camera cd::math cd::platform cd::core cd::sample_common
)
