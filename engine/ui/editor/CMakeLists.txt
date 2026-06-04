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

# ── cd::editor_panel_animator (STATIC) ─────────────────────────────────────
# phase556-557-558 — Animator panel (DrawBatcher path; animation clip browser +
# timeline scrubber + play/pause/loop toggles).
add_subdirectory(panel_animator)

# ── cd::editor_panel_behavior_designer (STATIC) ────────────────────────────
# phase556-557-558 — Behavior Designer panel (DrawBatcher path; behavior tree
# graph view: placeholder grid + 3 demo nodes; Sprint-2 = full BT graph).
add_subdirectory(panel_behavior_designer)

# ── cd::editor_panel_asset_drop_target (STATIC) ────────────────────────────
# phase594 — Asset Drop Target panel (DrawBatcher path; dashed-border drop zone
# for asset import; extension filter + consume_dropped_path API).
add_subdirectory(panel_asset_drop_target)

# ── cd::editor_panel_cutscene_player (STATIC) ──────────────────────────────
# phase617 — Cutscene Player panel (DrawBatcher path; timeline scrubber +
# per-phase block layout + event marker dots + Play/Pause/Stop/Skip strips).
# Drives cd::game::cutscene_player::CutscenePlayer.
add_subdirectory(panel_cutscene_player)

# ── cd::editor_panel_input_recorder (STATIC) ───────────────────────────────
# phase665 — Input Recorder panel (DrawBatcher path; Record/Stop/Replay button
# strips + timestamp/progress display). Drives cd::game::input_recorder.
add_subdirectory(panel_input_recorder)

# ── cd::editor_panel_light_editor (STATIC) ─────────────────────────────────
# phase664 — Light Editor panel (DrawBatcher path; point-light list +
# cluster grid stats; click-to-select; Sprint-2 = edit fields).
add_subdirectory(panel_light_editor)

# ── cd::editor_panel_dialog_tree_editor (STATIC) ──────────────────────────
# phase666 — Dialog Tree Editor panel (DrawBatcher path; BG3-style dialog
# graph view: colour-coded node rectangles + edge connectors + click-to-select).
# Drives cd::game::dialog_tree::DialogTree.
add_subdirectory(panel_dialog_tree_editor)

# ── cd::editor_panel_vehicle_editor (STATIC) ──────────────────────────────
# phase676 — Vehicle Editor panel (DrawBatcher path; engine/wheels/chassis
# sections + live state readout). Drives cd::physics::vehicle::VehicleConfig
# and VehicleState. MOMENT: racing dev tweaks gear ratios, sees next-frame
# acceleration response without recompile.
add_subdirectory(panel_vehicle_editor)

# ── cd::editor_panel_pathfinding_viz (STATIC) ─────────────────────────────
# phase677 — Pathfinding Viz panel (DrawBatcher path; top-down 2D navmesh
# projection + A* last-path overlay + stats strip).
# Drives cd::ai::pathfinding::NavMesh + PathResult.
# MOMENT: designer drops a navmesh + clicks start/goal, sees A* path painted.
add_subdirectory(panel_pathfinding_viz)

# ── cd::editor_panel_material_preview (STATIC) ────────────────────────────
# phase678 — Material Preview panel (DrawBatcher path; 2D sphere swatch tinted
# by base_color + metallic/roughness bars + alpha cutoff bar (kMask) + texture
# path pills). Sprint-2: real PBR sphere via cd::material UI variant pipeline.
# Drives cd::asset::material_authoring::AuthoredMaterial (M10 phase644).
add_subdirectory(panel_material_preview)

# ── cd::editor_panel_debug_viz (STATIC) ───────────────────────────────────────
# phase684 — Debug Visualization overlay (DrawBatcher path; 3 toggleable
# thumbnails: depth / normal / alpha-bucket). Sprint-1 placeholder gradient
# tiles; Sprint-2 = real G-buffer sampling via cd::render::framegraph.
add_subdirectory(panel_debug_viz)

# ── cd::editor_panel_keyboard_shortcut_overlay (STATIC) ───────────────────────
# phase688 — Keyboard Shortcut Overlay (DrawBatcher path; full-screen semi-
# transparent cheatsheet; shortcuts grouped by category in horizontal columns).
# Toggle with '?' (Source 2 / Hammer SDK convention).
# MOMENT: new user presses '?', sees the entire keyboard map instantly.
add_subdirectory(panel_keyboard_shortcut_overlay)

# ── cd::editor_panel_scene_navigator (STATIC) ─────────────────────────────────
# phase687 — Scene Navigator panel (DrawBatcher path; search box + live-filtered
# scrollable entity list; case-insensitive substring match; Esc clears filter).
# Sibling to scene_tree (different responsibility: search/filter, not hierarchy).
# MOMENT: 500-entity scene, type 'enem', see 12 enemy spawners instantly.
add_subdirectory(panel_scene_navigator)

# ── cd::editor_panel_settings (STATIC) ───────────────────────────────────────
# phase698 — Settings panel (DrawBatcher path; 5-category tab strip +
# key/value entry registry). MOMENT: user edits vsync=on → off, engine
# respects it next frame.
add_subdirectory(panel_settings)

# ── cd::editor_panel_build (STATIC) ───────────────────────────────────────────
# phase699 — Build Panel (DrawBatcher path; colour-coded Status badge +
# scrollable BuildEvent log with timestamp/message/severity tinting;
# click on event with source_file emits selected_event_index).
# MOMENT: dev presses Build, sees live progress + errors — no terminal needed.
add_subdirectory(panel_build)

# ── cd::editor_cdproj (STATIC) ──────────────────────────────────────────────
# phase547 — .cdproj project-file read/write (JSON, standalone, cd::core only).
add_subdirectory(cdproj)

# ── Tests ──────────────────────────────────────────────────────────────────
if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
  add_subdirectory(tests/panel)
  add_subdirectory(tests/ui)
endif()
