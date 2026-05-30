# samples/ui/hello_ui — Phase 1.5 + partial Phase 2 of ADR-20260530-ui-widget-library.
#
# Ties together every Phase-1 + Phase-2 cd::ui library produced so far:
#   * cd::ui_layout       (Flex layout solver)
#   * cd::ui_font         (stb_truetype glyph atlas)
#   * cd::ui_renderer     (CPU draw batcher)
#   * cd::ui_renderer_rhi (RHI bridge — ring vb/ib + descriptor set)
#   * cd::ui_input        (hit-test + focus chain + tab nav + modal capture)
#   * cd::ui_widgets      (Button / Slider / ... concrete catalog)
#
# Boots a platform window + Vulkan device on real hardware; falls back
# to NullDevice on hosts without a GPU (CI smoke). End-to-end Vulkan
# validation deferred to the GPU-validation session per ADR.

cd_add_sample(hello_ui
  SOURCES main.cpp
  DEPS
    cd::platform
    cd::rhi
    cd::rhi_vulkan
    cd::ui_layout
    cd::ui_font
    cd::ui_renderer
    cd::ui_renderer_rhi
    cd::ui_input
    cd::ui_widgets
    cd::core
    cd::sample_common
)
