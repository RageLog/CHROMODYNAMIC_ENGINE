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
add_subdirectory(renderer)      # phase444 — ADR-20260530 Phase 1.2 CPU draw batcher
add_subdirectory(renderer_rhi)  # phase453 — ADR-20260530 Phase 1.2b RHI bridge
add_subdirectory(input)         # phase457 — ADR-20260530 Phase 2.1 hit-test + focus chain + tab nav + modal capture
add_subdirectory(animation)     # phase458 — ADR-20260530 Phase 3.2 easing + Tweener + Timeline
add_subdirectory(widgets)       # phase460 — ADR-20260530 Phase 2.2 concrete widget catalog
add_subdirectory(theme)         # phase474 — cd::ui::theme V2 design token system (Material 3 shape)
add_subdirectory(editor)  # phase421: editor/CMakeLists.txt drives editor + editor_ui + editor_panel
