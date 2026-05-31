# =============================================================================
# apps/editor -- cd::editor binary application (Phase 524 / T0.2).
#
# Boots the cd::editor stack on the Vulkan + cd::render swapchain. The
# binary is gated on CD_ENABLE_VULKAN (same gate used by samples/editor)
# because it depends on cd::rhi_vulkan + cd::render.
#
# Linked libraries (T0.2 brief):
#   cd::ui                 -- retained-mode widget tree
#   cd::ui_widgets         -- DockSpace + concrete widget catalog
#   cd::ui_theme           -- design token theme (V2)
#   cd::ui_renderer        -- CPU draw batcher
#   cd::ui_renderer_rhi    -- RHI submitter bridge
#   cd::ui_input           -- hit-test + focus chain (transitive via widgets)
#   cd::ui_a11y            -- accessibility focus chain
#   cd::ui_font            -- TTF glyph atlas
#   cd::editor_panel       -- composite-pass preset library
#   cd::editor             -- editor application shell
#   cd::editor_ui          -- editor-specific widgets (transitive via editor)
#   cd::platform           -- OS window
#   cd::rhi                -- RHI abstraction
#   cd::rhi_vulkan         -- Vulkan backend
#   cd::render             -- swapchain Renderer
#   cd::asset              -- asset core (transitive)
#   cd::asset_gltf         -- gltf loader (transitive; T0.3 plumbs imports)
#   cd::scene              -- scene graph (transitive via editor)
#   cd::ecs                -- ECS world (transitive via editor)
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
    cd::editor
    cd::editor_ui
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
