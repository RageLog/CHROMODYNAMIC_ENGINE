# phase591-hello-animator-sample
#
# Visual sample for cd::editor::panel::animator::Animator.
# Boots a Vulkan window, creates a cd::ui_renderer_rhi::Submitter via the
# M3 W1A inline-shader factory, and renders the Animator panel every
# frame using the cd::ui DrawBatcher path.
#
# Deliberately ImGui-free: built entirely on the cd::ui_* layer
# (cd::ui_renderer_rhi + cd::ui_widgets + cd::ui_font) with no imgui_backend
# dependency.
#
# Targets:
#   cd_sample_hello_animator   (executable)

cd_add_sample(hello_animator
  SOURCES main.cpp
  DEPS
    cd::editor_panel_animator
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
