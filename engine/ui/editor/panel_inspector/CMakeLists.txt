# engine/ui/editor/panel_inspector — cd::editor_panel_inspector
#
# phase543 — Inspector panel library.
# Provides cd::editor::panel::inspector::Inspector (DrawBatcher + ImGui paths).
#
# Targets:
#   cd_editor_panel_inspector   (real target)
#   cd::editor_panel_inspector  (alias)

cd_add_library(editor_panel_inspector
  TYPE STATIC
  EXCLUDE_FROM_INSTALL       # cd::imgui_backend leaks vendored headers
  SOURCES
    src/Inspector.cpp
  PUBLIC_DEPS
    cd::core
    cd::ecs
    cd::scene
    cd::math
    cd::material        # M12 W2 — MaterialInstance + AlphaMode round-trip surface
    cd::editor          # TransformCommands, EditHistory, ICommand
    cd::ui_renderer     # DrawBatcher
    cd::ui_widgets      # Theme + Rect
  PRIVATE_DEPS
    cd::imgui_backend   # imgui.h used only in Inspector.cpp (draw_imgui path)
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
