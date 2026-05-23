cd_add_sample(hello_editor
  SOURCES main.cpp
  DEPS
    cd::core
    cd::math
    cd::ecs
    cd::scene
    cd::editor
    cd::imgui_backend
    cd::platform
    cd::render
    cd::rhi
    cd::rhi_vulkan
    cd::sample_common
)
