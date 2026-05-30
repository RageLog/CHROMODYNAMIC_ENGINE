# engine/ui — Phase 5 UI tier (phase430: ui/ui flattened into umbrella)
#
# cd::ui — Sprint S5.1: retained-mode widget tree. Renderer-agnostic; widgets
#          emit DrawCommands that frontends (editor / app) consume.
#          Depends on cd::core only.
#
# Sub-libraries driven from here:
#   editor  — phase421: editor/CMakeLists.txt drives editor + editor_ui + editor_panel
#
# Cross-library rule: editor_ui depends on cd::ui, editor depends on both — no
# back-edges.

cd_add_library(ui
  TYPE STATIC
  SOURCES src/Widget.cpp
  PUBLIC_DEPS cd::core
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()

add_subdirectory(layout)  # phase442 — ADR-20260530 Phase 1.0 Flex layout solver
add_subdirectory(font)    # phase443 — ADR-20260530 Phase 1.1 stb_truetype atlas
add_subdirectory(editor)  # phase421: editor/CMakeLists.txt drives editor + editor_ui + editor_panel
