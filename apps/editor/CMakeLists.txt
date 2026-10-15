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
# Uses cd_add_app(...) so the binary picks up the same warnings /
# sanitizer / smoke-target wiring every cd_* app target gets. The
# resulting executable name is "editor" (cd_add_app sets OUTPUT_NAME
# to the short_name argument).

cd_add_app(editor
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
    cd::editor_panel_material_editor
    cd::editor_panel_animator
    cd::editor_panel_behavior_designer
    cd::editor_panel_asset_drop_target
    cd::profile_cpu_marker_overlay
    cd::profile_frame_graph_timeline
    cd::framegraph
    cd::profile_gpu_marker
    cd::asset_validator
    cd::editor_panel_light_editor
    cd::editor_panel_input_recorder
    cd::editor_panel_dialog_tree_editor
    cd::editor_panel_cutscene_player
    cd::editor_panel_vehicle_editor
    cd::editor_panel_pathfinding_viz
    cd::editor_panel_material_preview
    cd::editor_panel_debug_viz
    cd::editor_panel_scene_navigator
    cd::editor_panel_keyboard_shortcut_overlay
    cd::editor_panel_settings
    cd::editor_panel_build
    cd::editor_panel_perf_profiler
    cd::editor_panel_asset_pipeline_status
    cd::editor_panel_ik_chain_editor
    cd::editor_panel_scene_palette
    cd::editor_panel_lobby_browser
    cd::editor_panel_auto_save_indicator
    cd::editor_panel_status_bar_fps_sparkline
    cd::net_lobby
    cd::particle_system
    cd::audio
    cd::editor
    cd::editor_ui
    cd::editor_cdproj
    cd::element_core
    cd::elements_domain
    cd::gameplay_framework
    cd::scene_renderer
    cd::runtime
    cd::material
    cd::frame_timing
    cd::platform
    cd::rhi
    cd::rhi_vulkan
    cd::render
    cd::asset
    cd::asset_gltf
    cd::scene
    cd::ecs
    cd::core
    cd::app_common
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
  add_test(
    NAME cd_editor_headless_pie_smoke
    COMMAND cd_app_editor --headless 5 --pie-play --showcase 01_pbr_materials_sss_hair
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  )
endif()

option(CD_USE_MATERIAL_UI_ROUTE_A
       "Editor prefers Route A (cd::material UI variant) at runtime, with safety fallback to Route B (inline GLSL). Default ON (ship path)."
       ON)
if(CD_USE_MATERIAL_UI_ROUTE_A)
  target_compile_definitions(cd_app_editor PRIVATE CD_USE_MATERIAL_UI_ROUTE_A=1)
endif()
