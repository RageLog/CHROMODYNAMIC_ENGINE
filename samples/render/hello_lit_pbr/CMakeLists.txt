cd_add_sample(hello_lit_pbr
  SOURCES main.cpp
  DEPS    cd::core cd::math cd::rhi cd::rhi_vulkan cd::shader
          cd::material cd::light cd::camera cd::platform
          cd::sample_common
)
