# =============================================================================
# apps/editor -- cd::editor binary application (Phase 524 / T0.2; updated phase685).
#
# Boots the cd::editor stack on the Vulkan + cd::render swapchain. The
# binary is gated on CD_ENABLE_VULKAN (same gate used by samples/editor)
# because it depends on cd::rhi_vulkan + cd::render.
#
# Linked libraries (T0.2 brief + phase569 / M4 W1B):
#   cd::ui                              -- retained-mode widget tree
#   cd::ui_widgets                      -- DockSpace + concrete widget catalog
#   cd::ui_theme                        -- design token theme (V2)
#   cd::ui_renderer                     -- CPU draw batcher
#   cd::ui_renderer_rhi                 -- RHI submitter bridge
#   cd::ui_input                        -- hit-test + focus chain (transitive)
#   cd::ui_a11y                         -- accessibility focus chain
#   cd::ui_font                         -- TTF glyph atlas
#   cd::editor_panel                    -- composite-pass preset library
#   cd::editor_panel_inspector          -- inspector panel (phase543)
#   cd::editor_panel_console            -- console panel (phase544)
#   cd::editor_panel_asset_browser      -- asset browser panel (phase545)
#   cd::editor_panel_viewport           -- viewport panel (phase546)
#   cd::editor_panel_material_editor    -- material editor panel (phase556-558)
#   cd::editor_panel_animator           -- animator panel (phase556-558)
#   cd::editor_panel_behavior_designer  -- behavior designer panel (phase556-558)
#   cd::editor_panel_asset_drop_target  -- asset drop target panel (phase594, wired phase598)
#   cd::profile_cpu_marker_overlay      -- CPU marker overlay (phase587, wired phase598)
#   cd::profile_frame_graph_timeline    -- frame graph timeline overlay (phase593, wired phase598)
#   cd::profile_gpu_marker              -- GPU marker overlay (phase606, wired phase631; cpu/gpu sibling)
#   cd::asset_validator                 -- asset validator (phase611, wired into status bar badge phase667)
#   cd::editor_panel_light_editor       -- light editor panel (phase664, wired phase667)
#   cd::editor_panel_input_recorder     -- input recorder panel (phase665, wired phase667)
#   cd::editor_panel_dialog_tree_editor -- dialog tree editor panel (phase666, wired phase667)
#   cd::editor_panel_cutscene_player    -- cutscene player panel (phase617, wired phase667 LEFT col)
#   cd::editor_panel_vehicle_editor     -- vehicle editor panel (phase676, wired phase679)
#   cd::editor_panel_pathfinding_viz    -- pathfinding viz panel (phase677, wired phase679)
#   cd::editor_panel_material_preview   -- material preview panel (phase678, wired phase679)
#   cd::editor_panel_debug_viz          -- debug viz overlays: depth/normal/alpha-bucket (phase684)
#   cd::editor_panel_settings           -- settings panel (phase698, wired phase701)
#   cd::editor_panel_build              -- build status panel (phase699, wired phase701)
#   cd::editor_panel_perf_profiler      -- deep frame-by-frame profiler (phase700, wired phase701)
#   cd::editor_panel_asset_pipeline_status -- StreamerPool 4-bar visualisation (phase709, wired phase711)
#   cd::editor_panel_ik_chain_editor    -- 2D IK chain visualiser (phase710, wired phase711)
#   cd::particle_system                 -- CPU particle system for cinematic boot splash (phase707)
#   cd::editor                          -- editor application shell
#   cd::editor_ui                       -- editor-specific widgets (transitive)
#   cd::platform                        -- OS window
#   cd::rhi                             -- RHI abstraction
#   cd::rhi_vulkan                      -- Vulkan backend
#   cd::render                          -- swapchain Renderer
#   cd::asset                           -- asset core (transitive)
#   cd::asset_gltf                      -- gltf loader (transitive; T0.3)
#   cd::scene                           -- scene graph (transitive via editor)
#   cd::ecs                             -- ECS world (transitive via editor)
#
# Uses cd_add_sample(...) so the binary picks up the same warnings /
# sanitizer / smoke-target wiring every cd_sample_* target gets. The
# resulting executable name is "editor" (cd_add_sample sets OUTPUT_NAME
# to the short_name argument).

cd_add_sample(editor
  SOURCES main.cpp
  DEPS
    cd::ui
    cd::ui_widgets
    cd::ui_theme
    cd::ui_renderer
    cd::ui_renderer_rhi
    cd::ui_input
    cd::ui_a11y
    cd::ui_font
    cd::editor_panel
    cd::editor_panel_inspector
    cd::editor_panel_console
    cd::editor_panel_asset_browser
    cd::editor_panel_viewport
    cd::editor_panel_material_editor    # phase569 / M4 W1B
    cd::editor_panel_animator           # phase569 / M4 W1B
    cd::editor_panel_behavior_designer  # phase569 / M4 W1B
    cd::editor_panel_asset_drop_target  # phase598 / M6 W3
    cd::profile_cpu_marker_overlay      # phase598 / M6 W3
    cd::profile_frame_graph_timeline    # phase598 / M6 W3
    cd::profile_gpu_marker              # phase631 / M9 W1A (cpu/gpu sibling; backfills phase618 slot)
    cd::asset_validator                 # phase631 / M9 W1A — wired into status bar badge in phase667
    # phase667 / M12 W2 — three new panels + cutscene_player sibling for the
    # LEFT-column tab pair (dialog_tree_editor + cutscene_player).
    cd::editor_panel_light_editor
    cd::editor_panel_input_recorder
    cd::editor_panel_dialog_tree_editor
    cd::editor_panel_cutscene_player
    # phase679 / M13 W3 — three new authoring panels:
    #   vehicle_editor (tab-merged with material_editor),
    #   pathfinding_viz (standalone bottom-left split),
    #   material_preview (3-way tab strip with material_editor + vehicle_editor).
    cd::editor_panel_vehicle_editor
    cd::editor_panel_pathfinding_viz
    cd::editor_panel_material_preview
    # phase684 / M13 W6A — 3 debug-viz thumbnails (depth / normal / alpha-bucket).
    cd::editor_panel_debug_viz
    # phase690 / M14 W3 — three NEW M14 surfaces wired into apps/editor:
    #   * scene_navigator           — search-filter sibling to scene_tree (LEFT, tab-merged).
    #   * keyboard_shortcut_overlay — '?' toggle full-screen cheatsheet (15 default shortcuts).
    #   * PopoutDock (in cd::ui_widgets) — state-machine for detach/reattach (Sprint-1 internal,
    #                                       native multi-window = Sprint-2). NOT a separate dep
    #                                       (already pulled in via cd::ui_widgets).
    cd::editor_panel_scene_navigator
    cd::editor_panel_keyboard_shortcut_overlay
    # phase701 / M15 W3 — three NEW M15 W3 surfaces wired into apps/editor:
    #   * settings_panel  — tab-merged with inspector on RIGHT-top.
    #   * build_panel     — tab-merged with console on BOTTOM.
    #   * perf_profiler   — split below light_editor on RIGHT-lower (+1 node).
    # Dock panel count grows 17 -> 20 (settings + build are tab-merges;
    # perf_profiler introduces the only new split this phase).
    cd::editor_panel_settings
    cd::editor_panel_build
    cd::editor_panel_perf_profiler
    # phase711 / M16 W3 — two NEW M16 surfaces wired into apps/editor:
    #   * asset_pipeline_status — tab-merged with perf_profiler on RIGHT-lower.
    #   * ik_chain_editor       — tab-merged with animator on BOTTOM strip.
    # Both are tab-merges so dockspace.node_count() stays at 14; only the panel
    # count grows 20 -> 22. Also drives shortcut-overlay auto-population: each
    # panel registers its focus hotkey (Ctrl+1..Ctrl+22) into the overlay so a
    # first-time user pressing '?' sees every shortcut DAY ONE.
    cd::editor_panel_asset_pipeline_status
    cd::editor_panel_ik_chain_editor
    # phase720 / M17 W3 — three new M17 surfaces wired into apps/editor:
    #   * scene_palette        — tab-merged with settings_panel on RIGHT-top.
    #   * lobby_browser        — standalone split below perf_profiler RIGHT-lower.
    #   * auto_save_indicator  — status-bar widget (NOT a dock panel); driven
    #                            by a 30 s auto-save timer + dirty-flag walker.
    # Dock panel count grows 22 -> 24; node_count grows by +1 (lobby_browser
    # split; scene_palette is a tab-merge; auto_save_indicator is outside the
    # dock entirely).
    cd::editor_panel_scene_palette
    cd::editor_panel_lobby_browser
    cd::editor_panel_auto_save_indicator
    cd::net_lobby                       # phase720 — Lobby instance for the LobbyBrowser seed.
    # phase707 / M15 W6B — particle system for the cinematic boot splash
    # background (20 particles/sec, 4s life, accent_success green glow).
    cd::particle_system
    cd::editor
    cd::editor_ui
    cd::editor_cdproj
    cd::material                        # phase659 / M11 W3B Sprint-4 -- always linked (Route A runtime path)
    cd::frame_timing                    # phase674 / M12 W6B -- header-only FrameTimeRing for real FPS feed
    cd::platform
    cd::rhi
    cd::rhi_vulkan
    cd::render
    cd::asset
    cd::asset_gltf
    cd::scene
    cd::ecs
    cd::core
    cd::sample_common
)

# Override the FOLDER property -- cd_add_sample defaults to "samples".
# The editor lives in its own "apps" IDE folder so VS / Xcode group it
# separately from the demo collection.
set_target_properties(cd_sample_editor PROPERTIES FOLDER "apps")

# Phase 608 / M7 W3 introduced Route A vs Route B as a hard compile-time
# split. Phase 659 / M11 W3B Sprint-4 turns the flag into a PREFERRED-PATH
# selector with graceful runtime fallback:
#
# CD_USE_MATERIAL_UI_ROUTE_A (default ON after Sprint-4):
#   * ON  -- main.cpp tries Route A (cd::material::UiVariant +
#            Submitter::create_with_material_ui_variant) first; on any
#            error it logs a warning and falls back to Route B
#            (Submitter::create_with_inline_shader) so the editor still
#            opens with a live swapchain. Boot prints which route
#            actually succeeded ("Route A success" or "Route A failed ->
#            Route B fallback").
#   * OFF -- main.cpp never attempts Route A; it goes straight to
#            Route B. Boot prints "Route B only".
#
# Both Submitter factories are linked into the binary regardless of the
# flag value (cd::material is now an unconditional dependency above);
# the flag only chooses whether to even try Route A at runtime. Default
# is ON because the runtime fallback makes the strategic path safe to
# ship as the visible default; OFF stays available for hosts where
# attempting Route A is undesirable (e.g. CI bots with no glslang).
#
# Override via:   cmake -B build -DCD_USE_MATERIAL_UI_ROUTE_A=OFF ...
option(CD_USE_MATERIAL_UI_ROUTE_A
       "Editor prefers Route A (cd::material UI variant) at runtime, with safety fallback to Route B (inline GLSL)."
       ON)
if(CD_USE_MATERIAL_UI_ROUTE_A)
  target_compile_definitions(cd_sample_editor PRIVATE CD_USE_MATERIAL_UI_ROUTE_A=1)
endif()
