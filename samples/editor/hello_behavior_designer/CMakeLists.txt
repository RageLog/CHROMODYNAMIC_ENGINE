# phase592-hello-behavior-designer-sample
#
# Visual sample for cd::editor::panel::behavior_designer::BehaviorDesigner.
# Boots a Vulkan window, creates a cd::ui_renderer_rhi::Submitter via the
# M3 W1A inline-shader factory, and renders the BehaviorDesigner panel every
# frame using the cd::ui DrawBatcher path.
#
# Deliberately ImGui-free: built entirely on the cd::ui_* layer
# (cd::ui_renderer_rhi + cd::ui_widgets + cd::ui_font) with no imgui
# dependency — proving cd::editor_panel_behavior_designer is consumable
# from an external sample.
#
# Targets:
#   cd_sample_hello_behavior_designer   (executable)

cd_add_sample(hello_behavior_designer
  SOURCES main.cpp
  DEPS
    cd::editor_panel_behavior_designer
    cd::ui_widgets
    cd::ui_renderer_rhi
    cd::ui_font
    cd::ui_theme
    cd::platform
    cd::rhi
    cd::rhi_vulkan
    cd::render
    cd::core
    cd::sample_common
)
