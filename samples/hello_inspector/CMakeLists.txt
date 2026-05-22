cd_add_sample(hello_inspector
  SOURCES main.cpp
  DEPS    cd::imgui_backend cd::scene cd::ecs cd::math cd::asset_json
          cd::render cd::rhi cd::rhi_vulkan
          cd::platform cd::core cd::sample_common
)
