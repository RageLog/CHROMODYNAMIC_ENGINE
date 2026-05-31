# engine/ui/editor — cd::editor + sub-libraries (phase421)
#
# Layout:
#   include/cd/editor/         — public editor headers
#     panel/                   — editor_panel headers
#     ui/                      — editor_ui headers
#   src/                       — Editor.cpp
#     ui/                      — EditorWidgets.cpp
#   tests/                     — editor tests
#     panel/                   — editor_panel tests
#     ui/                      — editor_ui tests
#
# Targets:
#   cd::editor_panel  — composite-pass preset library (INTERFACE)
#   cd::editor_ui     — editor-specific widgets (STATIC)
#   cd::editor        — editor application shell (STATIC)

# ── cd::editor_panel (INTERFACE — header-only) ─────────────────────────────
# Composite-pass post-fx preset library. Marathon Run 23 phase N5A+N5B.
cd_add_library(editor_panel
  TYPE INTERFACE
  PUBLIC_DEPS cd::core
)

# ── cd::editor_ui (STATIC) ─────────────────────────────────────────────────
# Phase 5 / Sprint S5.2 — editor-specific widgets.
cd_add_library(editor_ui
  TYPE STATIC
  SOURCES src/ui/EditorWidgets.cpp
  PUBLIC_DEPS cd::ui cd::scene cd::ecs cd::math cd::core
)

# ── cd::editor (STATIC) ────────────────────────────────────────────────────
# Phase 5 / Sprint S5.3 — editor application shell.
cd_add_library(editor
  TYPE STATIC
  SOURCES src/Editor.cpp
  PUBLIC_DEPS
    cd::editor_ui
    cd::ui
    cd::scene
    cd::ecs
    cd::input
    cd::camera   # for cd/editor/Bookmark.hpp (Camera struct)
    cd::math
    cd::core
)

# ── cd::editor_panel_inspector (STATIC) ────────────────────────────────────
# phase543 — Inspector panel (DrawBatcher + ImGui paths).
add_subdirectory(panel_inspector)

# ── cd::editor_panel_console (STATIC) ──────────────────────────────────────
# phase544 — Console panel (DrawBatcher path; last 32 log messages).
add_subdirectory(panel_console)

# ── cd::editor_panel_asset_browser (STATIC) ────────────────────────────────
# phase545 — Asset Browser panel (DrawBatcher path; file entry list).
add_subdirectory(panel_asset_browser)

# ── cd::editor_panel_viewport (STATIC) ─────────────────────────────────────
# phase546 — Viewport panel (DrawBatcher path; textured scene colour target).
add_subdirectory(panel_viewport)

# ── cd::editor_panel_material_editor (STATIC) ──────────────────────────────
# phase556-557-558 — Material Editor panel (DrawBatcher path; base color /
# metallic / roughness sliders + live preview rectangle).
add_subdirectory(panel_material_editor)

# ── cd::editor_cdproj (STATIC) ──────────────────────────────────────────────
# phase547 — .cdproj project-file read/write (JSON, standalone, cd::core only).
add_subdirectory(cdproj)

# ── Tests ──────────────────────────────────────────────────────────────────
if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
  add_subdirectory(tests/panel)
  add_subdirectory(tests/ui)
endif()
