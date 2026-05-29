# engine/ui — Phase 5 UI tier
# Sub-libraries (dependency order):
#   ui         — Sprint S5.1: retained-mode widget tree (cd::ui).
#   editor_ui  — S5.2 (planned): editor-specific widgets (gizmos, inspectors).
#   editor     — S5.3 (planned): editor application shell.
#
# Cross-library rule: editor_ui depends on ui, editor depends on both — no
# back-edges.

add_subdirectory(ui)
add_subdirectory(editor)  # phase421: editor/CMakeLists.txt drives editor + editor_ui + editor_panel
