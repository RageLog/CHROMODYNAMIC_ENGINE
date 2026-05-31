# phase567-hello-material-editor-sample
#
# Visual sample for cd::editor::panel::material_editor::MaterialEditor.
# Boots a Vulkan window, creates a cd::ui_renderer_rhi::Submitter via the
# M3 W1A inline-shader factory, and renders the MaterialEditor panel every
# frame using the cd::ui DrawBatcher path.
#
# Deliberately ImGui-free: this is the first samples/editor sample that
# is built entirely on the cd::ui_* layer (cd::ui_renderer_rhi + cd::ui_widgets
# + cd::ui_font + cd::ui_theme) with no imgui_backend dependency.
#
# Targets:
#   cd_sample_hello_material_editor   (executable)

cd_add_sample(hello_material_editor
  SOURCES main.cpp
  DEPS
    cd::editor_panel_material_editor
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
