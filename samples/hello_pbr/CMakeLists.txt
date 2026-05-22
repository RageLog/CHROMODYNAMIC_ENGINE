cd_add_sample(hello_pbr
  SOURCES main.cpp
  DEPS    cd::material cd::shader cd::render cd::rhi cd::rhi_vulkan
          cd::camera cd::math cd::platform cd::core cd::sample_common
)
