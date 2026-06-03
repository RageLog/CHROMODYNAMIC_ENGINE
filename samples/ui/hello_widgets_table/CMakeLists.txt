# samples/ui/hello_widgets_table — Phase 635
#
# Console-only headless proof that cd::ui::widgets::Table is consumable as a
# standalone library without spinning up a GPU or a platform window.
#
# Demonstrates:
#   * 4-column, 10-row Table construction
#   * set_cell_text on the diagonal (row == col for cols 0-3)
#   * simulate_click on row 3 using the same Rect passed to draw
#   * draw() into a cd::ui::renderer::DrawBatcher and inspecting command_count()
#   * Prints selected_row + DrawBatcher cmd count then exits 0

cd_add_sample(hello_widgets_table
  SOURCES main.cpp
  DEPS
    cd::ui_widgets
    cd::ui_renderer
    cd::core
)
