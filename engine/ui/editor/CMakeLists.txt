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

# ── Tests ──────────────────────────────────────────────────────────────────
if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
  add_subdirectory(tests/panel)
  add_subdirectory(tests/ui)
endif()
