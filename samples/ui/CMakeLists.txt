# samples/ui — UI tier showcase samples (Vulkan-gated by the parent
# samples/CMakeLists.txt). Phase 1.5 + partial Phase 2 of
# ADR-20260530-ui-widget-library introduces hello_ui as the first
# end-to-end demo wiring cd::ui_layout / ui_font / ui_renderer /
# ui_renderer_rhi / ui_input / ui_widgets together.
#
# phase913-sample-consolidation: hello_widgets_table headless console
# proof folded into engine/ui/widgets/tests/test_table.cpp (7 cases).
# Source directory deleted. hello_ui is retained because it is the
# canonical Vulkan visual demo for the entire cd::ui stack (category b).
add_subdirectory(hello_ui)
