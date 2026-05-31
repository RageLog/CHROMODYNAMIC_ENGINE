# =============================================================================
# apps/editor -- cd::editor binary application (Phase 524 / T0.2).
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
    cd::editor
    cd::editor_ui
    cd::editor_cdproj
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

# Phase 554 / M3 W1A — Route A vs Route B pipeline selection.
#
# CD_HAVE_MATERIAL_UI_VARIANTS (default OFF) -- when the parallel W1B
# agent lands the cd::material UI variants, flip this ON via cache or
# command-line:  -DCD_HAVE_MATERIAL_UI_VARIANTS=ON
#
# OFF: editor boots with cd::ui_renderer_rhi's inline GLSL submitter
#      (Route B). Solid panel quads only -- enough to make the editor
#      window visually alive today.
#
# ON : editor boots with the full cd::material UI variant pipeline
#      (Route A) -- textured quads, glyphs, nine-patch, gradients, blur.
option(CD_HAVE_MATERIAL_UI_VARIANTS
       "Editor uses cd::material UI variants (Route A); OFF = inline GLSL fallback (Route B)"
       OFF)
if(CD_HAVE_MATERIAL_UI_VARIANTS)
  target_compile_definitions(cd_sample_editor PRIVATE CD_HAVE_MATERIAL_UI_VARIANTS=1)
endif()
