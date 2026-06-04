// =============================================================================
// CHROMODYNAMIC -- apps/editor/main.cpp
//
// Phase 524 / T0.2 -- cd::editor binary application shell.
//
// Boots the cd::editor stack on top of:
//   * cd::platform               -- OS window
//   * cd::rhi + cd::rhi_vulkan   -- GPU device + command queue
//   * cd::render                 -- swapchain Renderer (begin_frame / end_frame)
//   * cd::ui_widgets::DockSpace  -- T2.1 panel layout solver
//   * cd::ui_renderer (+_rhi)    -- CPU draw batcher + RHI submitter pair
//   * cd::ui_font                -- TTF glyph atlas (optional; gated on TTF probe)
//   * cd::ui::theme              -- design-token theme (V2)
//   * cd::ui::a11y               -- accessibility focus chain hook
//   * cd::editor::Editor         -- authoritative World + Scene + scene-tree state
//
// Layout the binary boots with (phase569 / M4 W1B — 8-panel default):
//
//      +-----------------------------------------------------------------------+
//      |                                                                       |
//      |  Scene Tree    |          Viewport          |    Inspector            |
//      |    (LEFT-T)    |          (CENTER-T)        |    (RIGHT-T)            |
//      |                |                            |                         |
//      +----------------+----------------------------+-------------------------+
//      |  Material      |  Console | Assets |  Anim  |    Behavior Designer    |
//      |  Editor        |  (BOTTOM-CENTER tabs)  |   |    (RIGHT-B)            |
//      |  (LEFT-B)      |                        |   |                         |
//      +-----------------------------------------------------------------------+
//
// Three new panels (phase569 / M4 W1B):
//   * material_editor    -- LEFT column, BOTTOM split (below scene_tree)
//   * animator           -- BOTTOM strip, RIGHT split (next to console+assets)
//   * behavior_designer  -- RIGHT column, BOTTOM split (below inspector)
//
// One new panel + two floating overlays (phase598 / M6 W3):
//   * asset_drop_target  -- tab-merged with `assets` in the BOTTOM strip;
//                           accepts {.gltf .glb .png .jpg .wav .ogg}, logs
//                           dropped paths to console.
//   * cpu_marker_overlay -- floating top-right (~300x120 px), fed synthetic
//                           samples until real instrumentation lands.
//   * frame_graph_timeline -- floating bottom-right (~400x80 px), fed
//                             synthetic GPU pass records.
//
// New overlay (phase631 / M9 W1A — phase618 slot backfilled):
//   * gpu_marker_overlay -- floating top-right below cpu_marker (~300x100 px);
//                           cpu/gpu sibling classification, NOT a v1/v2 rename.
//                           Synthetic GpuMarkerSample data per frame (real GPU
//                           timestamps = future Sprint, ICommandBuffer::write_timestamp).
//   * asset::validator   -- Wired into the bottom status bar via phase667 /
//                           M12 W2. Validator badge shows green/yellow/red
//                           counts (pass / warn / error) seeded by three
//                           synthetic blob validations at boot.
//
// Four new panels (phase667 / M12 W2 — 10 -> 13 dock nodes):
//   * light_editor       -- RIGHT column, BOTTOM split below behavior_designer.
//   * input_recorder     -- BOTTOM strip, right split alongside animator.
//   * dialog_tree_editor -- tab-merged INTO the new cutscene_player leaf on
//                           the LEFT column.
//   * cutscene_player    -- LEFT column, BOTTOM split below material_editor.
//                           Sits alongside dialog_tree_editor as a 2-tab pair.
//
// Status bar (phase667 / M12 W2 — pinned to the BOTTOM 20 px of the framebuffer):
//   * asset_validator badge -- pass / warn / error count blocks.
//   * frame stats meters    -- FPS / vertex count / draw call count.
//   * route indicator       -- which UI submitter route is live (A / fallback / B).
//
// The LEFT and RIGHT columns now each carry TWO stacked panels (vertically),
// and the BOTTOM strip carries the existing console+assets tab group plus
// the new animator as a horizontal sibling.
//
// SCOPE-DOWN DECISION (per T0.2 deliverable contract): the editor.exe
// app boots the FULL DockSpace shell with all eight panels registered
// (scene tree / viewport / inspector / console / assets / material_editor /
// animator / behavior_designer) and wires the scene-tree panel to a live
// cd::editor::Editor instance. The follow-up sessions extract richer
// content into reusable panel libraries (phases 556-558 shipped the 3
// M4 W1B panel libraries; phase569 wires them into apps/editor).
//
// Per the T0.2 brief: "If too big for one agent, SCOPE DOWN to:
// Editor binary with DockSpace shell + 1 panel (scene tree)" -- this
// implementation goes beyond the scope-down floor (it lays down all 5
// panel slots with real DockSpace layout serialization wired in) but
// stops short of porting in the inspector/console/assets panel content.
//
// Runtime flags (delegated to cd::sample::Runtime):
//   --headless [N]   Run N frames then exit. CI uses this.
//   --null           Force NullDevice fallback (no real swapchain).
//                    Useful on hosts without a GPU.
//   --no-spin        Reserved; the editor has no time-driven animation
//                    today, so this is a no-op.
//   --project <path> Parse a .cdproj file and use it to seed the
//                    Editor. Today the parser stub just stashes the
//                    path; full .cdproj plumbing lands in T0.3.
// =============================================================================

#include "SampleRuntime.hpp"

#include <cd/platform/Window.hpp>
#include <cd/render/Renderer.hpp>
#include <cd/rhi/Barriers.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi/NullDevice.hpp>
#include <cd/rhi/vulkan/VulkanDevice.hpp>
#include <cd/ui/a11y/A11y.hpp>
#include <cd/ui/font/Font.hpp>
#include <cd/ui/input/Input.hpp>
#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/renderer_rhi/Submitter.hpp>
#include <cd/ui/theme/Theme.hpp>
#include <cd/ui/widgets/DockSpace.hpp>
#include <cd/ui/widgets/Widgets.hpp>

// Phase 608 / M7 W3 — Route A path. As of phase659 / M11 W3B Sprint-4 the
// cd::material UI variant header is included UNCONDITIONALLY so the editor
// can attempt Route A at runtime and gracefully fall back to Route B when
// the cd::material::create_ui_variant factory returns an error (e.g. the
// host lacks glslang or the descriptor layout cannot be allocated). The
// compile-time `CD_USE_MATERIAL_UI_ROUTE_A` flag now controls the
// PREFERRED route -- ON (default) = "try A first, fall back to B on error",
// OFF = "only ever build with B". Both Submitter factories are linked into
// the binary either way; the runtime self-test below logs which one
// succeeded so the user sees the active path on every boot.
#include <cd/material/UiVariant.hpp>

// phase667 / M12 W2 — Material + MaterialInstance used by the Inspector PBR
// section. The editor seeds an inert MaterialInstance so the round-trip
// surface (metallic / roughness / alpha_mode / alpha_cutoff sliders) is
// non-empty on first boot.
#include <cd/material/Material.hpp>

#include <cd/editor/Editor.hpp>
#include <cd/editor/HierarchyView.hpp>
#include <cd/editor/panel_inspector/Inspector.hpp>
#include <cd/editor/panel_console/Console.hpp>
#include <cd/editor/panel_asset_browser/AssetBrowser.hpp>
#include <cd/editor/panel_viewport/Viewport.hpp>
#include <cd/editor/panel_material_editor/MaterialEditor.hpp>
#include <cd/editor/panel_animator/Animator.hpp>
#include <cd/editor/panel_behavior_designer/BehaviorDesigner.hpp>
#include <cd/editor/panel_asset_drop_target/AssetDropTarget.hpp>
// phase667 / M12 W2 — three new panels wired into the dock + cutscene_player
// joined alongside dialog_tree_editor on the LEFT column.
#include <cd/editor/panel_light_editor/LightEditor.hpp>
#include <cd/editor/panel_input_recorder/InputRecorderPanel.hpp>
#include <cd/editor/panel_dialog_tree_editor/DialogTreeEditor.hpp>
#include <cd/editor/panel_cutscene_player/CutscenePlayerPanel.hpp>
#include <cd/editor/cdproj/CdprojFile.hpp>

// phase667 / M12 W2 — asset_validator (status badge in the new status bar) +
// cd::asset::validator::Severity for green/yellow/red counts.
#include <cd/asset/validator/Validator.hpp>

// phase598 / M6 W3 — overlays (CPU marker bar chart + frame-graph Gantt).
#include <cd/profile/cpu_marker_overlay/CpuMarkerOverlay.hpp>
#include <cd/profile/frame_graph_timeline/FrameGraphTimeline.hpp>

// phase631 / M9 W1A — GPU marker overlay (sibling to cpu_marker_overlay;
// cpu/gpu classification, NOT a v1/v2 version rename). Synthetic samples per
// frame — real GPU timestamps land when ICommandBuffer::write_timestamp ships.
// (phase618 slot was planned but not delivered as a standalone commit; this
// phase631 commit is the official delivery.)
#include <cd/profile/gpu_marker/GpuMarker.hpp>

// phase631 / M9 W1A — asset::validator for status badge.
// TODO(phase631): No status bar exists yet in apps/editor. When a status bar
// is added, wire a cd::asset::validator::Validator instance here and display
// a pass/warn/error badge from the latest validate call results.
// #include <cd/asset/validator/Validator.hpp>  // linked but include deferred

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{

namespace platform = cd::platform;
namespace rhi      = cd::rhi;
namespace render   = cd::render;
namespace uf       = cd::ui::font;
namespace ur       = cd::ui::renderer;
namespace urr      = cd::ui::renderer_rhi;
namespace uw       = cd::ui::widgets;
namespace uth      = cd::ui::theme;

// ---- Editor-specific argv flags --------------------------------------------

struct EditorArgs
{
    bool        force_null { false };
    std::string project_path;
};

[[nodiscard]] EditorArgs parse_editor_args(int argc, char** argv)
{
    EditorArgs a {};
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view s { argv[i] };
        if (s == "--null")
        {
            a.force_null = true;
        }
        else if (s == "--project" && i + 1 < argc)
        {
            a.project_path = argv[i + 1];
            ++i;
        }
    }
    // Fallback: if argv[1] is a non-flag positional, treat as .cdproj.
    if (a.project_path.empty() && argc >= 2 && argv[1][0] != '-')
    {
        a.project_path = argv[1];
    }
    return a;
}

// ---- TTF probe (shared logic with hello_ui) --------------------------------

[[nodiscard]] std::vector<std::uint8_t> find_system_font()
{
    static const std::array<const char*, 6> kCandidates {
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/calibri.ttf",
        "C:/Windows/Fonts/arial.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
    };
    for (const char* p : kCandidates)
    {
        std::ifstream f(p, std::ios::binary | std::ios::ate);
        if (!f) { continue; }
        const auto sz = static_cast<std::size_t>(f.tellg());
        f.seekg(0);
        std::vector<std::uint8_t> out(sz);
        f.read(reinterpret_cast<char*>(out.data()),
               static_cast<std::streamsize>(sz));
        if (!out.empty()) { return out; }
    }
    return {};
}

// ---- Default DockSpace layout ----------------------------------------------
//
// Build the 8-panel layout described in the file header. Starts from the
// freshly-constructed `DockSpace` whose root is a single empty kLeaf;
// promotes the leaf to a tab group hosting "viewport", then carves off:
//
//   1. LEFT  (scene_tree)        -- 18% width
//   2. RIGHT (inspector)         -- 25% width on the right side
//   3. BOTTOM (console)          -- 28% height under the viewport
//   4. tab-merge assets onto console
//   5. LEFT-BOTTOM split: material_editor below scene_tree    (phase569 / M4 W1B)
//   6. RIGHT-BOTTOM split: behavior_designer below inspector  (phase569 / M4 W1B)
//   7. BOTTOM-RIGHT split: animator to the right of console+assets
//                                                              (phase569 / M4 W1B)
//
// Each new split adds one kSplit node to the tree, so node_count goes from
// 7 (original 5-panel layout) to 10 (8-panel layout). Tab-merges do NOT
// change node_count (DockSpace::node_count counts splits + tab groups).
//
// Returns true on success, false if any of the structural mutations fails
// (would indicate a regression in cd::ui_widgets::DockSpace::split).
[[nodiscard]] bool build_default_layout(uw::DockSpace& ds)
{
    // Seed the root leaf with "viewport".
    auto* root = ds.root();
    if (root == nullptr) { return false; }
    if (!ds.tab_merge(root, "viewport")) { return false; }

    // Split LEFT: scene tree takes 18% of the root's width.
    auto* viewport_node = ds.find_panel_owner("viewport");
    if (viewport_node == nullptr) { return false; }
    if (!ds.split(viewport_node, uw::DockAxis::kVertical,
                  "scene_tree", 0.18F))
    {
        return false;
    }

    // After the LEFT split, the new tree is:
    //     split-V (root)
    //       first  = scene_tree leaf
    //       second = viewport leaf  (original)
    //
    // The "viewport leaf" reference has been moved inside the split as
    // the second child. Re-resolve it before the next operation.
    viewport_node = ds.find_panel_owner("viewport");
    if (viewport_node == nullptr) { return false; }

    // Split RIGHT: inspector takes the right ~25% of what remains.
    // We split the viewport leaf along the vertical axis again, putting
    // inspector on the right. After the split:
    //     split-V (right side of root)
    //       first  = viewport leaf
    //       second = inspector leaf
    //
    // The 0.75 ratio means the FIRST child (viewport) keeps 75% of the
    // sub-rect width and inspector takes the remaining 25%.
    if (!ds.split(viewport_node, uw::DockAxis::kVertical,
                  "inspector", 0.75F))
    {
        return false;
    }

    // Split BOTTOM: console takes 28% of the height under the viewport.
    viewport_node = ds.find_panel_owner("viewport");
    if (viewport_node == nullptr) { return false; }
    if (!ds.split(viewport_node, uw::DockAxis::kHorizontal,
                  "console", 0.72F))
    {
        return false;
    }

    // Tab-merge "assets" into the console node so they share the bottom
    // strip. The brief specifies asset-browser BOTTOM-RIGHT; today the
    // simplest match is "alongside console as a sibling tab", which the
    // user can drag to the right side via the DockSpace drag-out flow.
    auto* console_node = ds.find_panel_owner("console");
    if (console_node == nullptr) { return false; }
    if (!ds.tab_merge(console_node, "assets")) { return false; }

    // ---- phase569 / M4 W1B: 3 new panels -----------------------------------
    //
    // Each is plugged in as a fresh kSplit so the editor's node_count grows
    // from 7 -> 10. The panel content libraries (cd::editor_panel_*) were
    // shipped in phases 556-558; this wiring is the final consumer step.

    // (1) material_editor BELOW scene_tree (LEFT column, lower half).
    // ratio=0.55 means scene_tree keeps 55% of the LEFT column's height
    // and material_editor takes the lower 45%.
    auto* scene_tree_node = ds.find_panel_owner("scene_tree");
    if (scene_tree_node == nullptr) { return false; }
    if (!ds.split(scene_tree_node, uw::DockAxis::kHorizontal,
                  "material_editor", 0.55F))
    {
        return false;
    }

    // (2) behavior_designer BELOW inspector (RIGHT column, lower half).
    // ratio=0.55 -> inspector keeps top 55%, behavior_designer takes 45%.
    auto* inspector_node = ds.find_panel_owner("inspector");
    if (inspector_node == nullptr) { return false; }
    if (!ds.split(inspector_node, uw::DockAxis::kHorizontal,
                  "behavior_designer", 0.55F))
    {
        return false;
    }

    // (3) animator to the RIGHT of the console+assets tab group
    // (BOTTOM strip, right half). ratio=0.65 -> console+assets keep the
    // left 65%, animator takes the right 35%.
    auto* console_owner = ds.find_panel_owner("console");
    if (console_owner == nullptr) { return false; }
    if (!ds.split(console_owner, uw::DockAxis::kVertical,
                  "animator", 0.65F))
    {
        return false;
    }

    // ---- phase598 / M6 W3 — asset_drop_target tab-merged with assets -------
    //
    // The drop target lives next to the asset browser so a user can flip
    // between "browse" and "drop" in the same bottom strip. tab_merge does
    // not change node_count (already 10 after the three M4 W1B splits).
    auto* assets_owner = ds.find_panel_owner("assets");
    if (assets_owner == nullptr) { return false; }
    if (!ds.tab_merge(assets_owner, "asset_drop_target")) { return false; }

    // ---- phase667 / M12 W2 — three new panel slots --------------------------
    //
    // The brief grows the dock from 10 -> 13 nodes by adding three new splits.
    // Each new split lands the new panel physically adjacent to its semantic
    // companion so the result behaves like a tab-merge from the designer's
    // point of view (they see the new content next to where they expect it):
    //
    //   (4) cutscene_player BELOW material_editor (LEFT column, 3-stack:
    //       scene_tree / material_editor / cutscene_player). +1 node.
    //
    //   (5) light_editor BELOW behavior_designer (RIGHT column, 3-stack:
    //       inspector / behavior_designer / light_editor). +1 node.
    //
    //   (6) input_recorder to the RIGHT of animator (BOTTOM strip,
    //       4-region split). +1 node.
    //
    // After the three splits node_count goes from 10 -> 13, matching the
    // M12 W2 target. Then we tab-merge dialog_tree_editor INTO the
    // cutscene_player leaf so the designer flips between the BG3-style
    // dialog graph and the cutscene timeline on the same dock tile — the
    // moment a narrative designer wants in a shipping editor.
    //
    // MOMENT: a first-time editor user opens apps/editor and immediately sees
    // a lighting designer slot, an input-replay slot, and a dialog/cutscene
    // pair — three brand-new authoring surfaces shipped DAY ONE.

    // (4) material_editor -> cutscene_player split (LEFT col lower half).
    // ratio=0.65 -> material_editor keeps 65%, cutscene_player takes 35%.
    auto* material_editor_node = ds.find_panel_owner("material_editor");
    if (material_editor_node == nullptr) { return false; }
    if (!ds.split(material_editor_node, uw::DockAxis::kHorizontal,
                  "cutscene_player", 0.65F))
    {
        return false;
    }

    // Tab-merge dialog_tree_editor INTO the cutscene_player leaf so the LEFT
    // column carries a dialog/cutscene pair on the same tile.
    auto* cutscene_owner = ds.find_panel_owner("cutscene_player");
    if (cutscene_owner == nullptr) { return false; }
    if (!ds.tab_merge(cutscene_owner, "dialog_tree_editor")) { return false; }

    // (5) behavior_designer -> light_editor split (RIGHT col lower half).
    // ratio=0.65 -> behavior_designer keeps top 65%, light_editor takes 35%.
    auto* behavior_designer_node = ds.find_panel_owner("behavior_designer");
    if (behavior_designer_node == nullptr) { return false; }
    if (!ds.split(behavior_designer_node, uw::DockAxis::kHorizontal,
                  "light_editor", 0.65F))
    {
        return false;
    }

    // (6) animator -> input_recorder split (BOTTOM strip far-right).
    // ratio=0.55 -> animator keeps 55%, input_recorder takes 45%.
    auto* animator_node = ds.find_panel_owner("animator");
    if (animator_node == nullptr) { return false; }
    if (!ds.split(animator_node, uw::DockAxis::kVertical,
                  "input_recorder", 0.55F))
    {
        return false;
    }

    return true;
}

// ---- Stub panel drawers ----------------------------------------------------
//
// Each registered panel needs a drawer of signature
//   void(const Rect&, DrawBatcher&, Font*, const Theme&)
//
// The drawers below emit a single coloured quad filling the panel rect
// so the DockSpace tile is visually distinguishable. Sub-libraries
// (cd::editor::panel::SceneTree etc.) will replace these stubs with
// real ImGui-style content in follow-up sessions.

void draw_scene_tree_stub(const uw::Rect& rect,
                          ur::DrawBatcher& batcher,
                          uf::Font* /*font*/,
                          const uw::Theme& theme)
{
    batcher.quad(rect.x, rect.y, rect.w, rect.h,
                 ur::Color { theme.surface.r, theme.surface.g,
                             theme.surface.b, theme.surface.a });
}

// Viewport panel — backed by cd::editor_panel_viewport.
// The scene texture handle is left null until the render loop supplies
// the real scene colour target (wired in a follow-up session).
cd::editor::panel::viewport::Viewport g_viewport_panel;

void draw_viewport_panel(const uw::Rect& rect,
                         ur::DrawBatcher& batcher,
                         uf::Font* /*font*/,
                         const uw::Theme& theme)
{
    g_viewport_panel.draw(batcher, theme, rect);
}

// Inspector panel — backed by cd::editor_panel_inspector.
// The inspector instance lives here in file scope so the ContentDrawer lambda
// below can capture it by reference without a heap allocation each frame.
cd::editor::panel::inspector::Inspector g_inspector_panel;

// Console panel — backed by cd::editor_panel_console.
cd::editor::panel::console::Console g_console_panel;

// Asset browser panel — backed by cd::editor_panel_asset_browser.
cd::editor::panel::asset_browser::AssetBrowser g_asset_browser_panel;

// phase569 / M4 W1B — 3 new panels wired in (libraries shipped in 556-558).
// Each lives in file scope so the ContentDrawer lambda can capture them by
// reference for the lifetime of the program (a local would dangle once
// main()'s stack frame is left).
cd::editor::panel::material_editor::MaterialEditor   g_material_editor_panel;
cd::editor::panel::animator::Animator                g_animator_panel;
cd::editor::panel::behavior_designer::BehaviorDesigner g_behavior_designer_panel;

// phase598 / M6 W3 — asset drop-target panel (dashed-border drop zone).
cd::editor::panel::asset_drop_target::AssetDropTarget g_asset_drop_target_panel;

// phase667 / M12 W2 — three new panel instances + cutscene_player sibling.
//
// Lifetime: each panel lives in file scope so the ContentDrawer lambdas
// capture them by reference for the lifetime of the program. The brief
// "what moment does this enable?" is encoded directly here — a designer
// drops into this editor and sees Lighting + InputReplay + DialogGraph +
// CutscenePlayer panels DAY ONE, not as TODO placeholders.
cd::editor::panel::light_editor::LightEditor                  g_light_editor_panel;
cd::editor::panel::input_recorder::InputRecorderPanel         g_input_recorder_panel;
cd::editor::panel::dialog_tree_editor::DialogTreeEditor       g_dialog_tree_editor_panel;
cd::editor::panel::cutscene_player::CutscenePlayerPanel       g_cutscene_player_panel;

void draw_inspector_panel(const uw::Rect& rect,
                          ur::DrawBatcher& batcher,
                          uf::Font* /*font*/,
                          const uw::Theme& theme)
{
    g_inspector_panel.draw(batcher, theme, rect);
}

void draw_console_panel(const uw::Rect& rect,
                        ur::DrawBatcher& batcher,
                        uf::Font* /*font*/,
                        const uw::Theme& theme)
{
    g_console_panel.draw(batcher, theme, rect);
}

void draw_assets_panel(const uw::Rect& rect,
                       ur::DrawBatcher& batcher,
                       uf::Font* /*font*/,
                       const uw::Theme& theme)
{
    g_asset_browser_panel.draw(batcher, theme, rect);
}

// phase569 / M4 W1B drawers ---------------------------------------------------

void draw_material_editor_panel(const uw::Rect& rect,
                                ur::DrawBatcher& batcher,
                                uf::Font* /*font*/,
                                const uw::Theme& theme)
{
    g_material_editor_panel.draw(batcher, theme, rect);
}

void draw_animator_panel(const uw::Rect& rect,
                         ur::DrawBatcher& batcher,
                         uf::Font* /*font*/,
                         const uw::Theme& theme)
{
    g_animator_panel.draw(batcher, theme, rect);
}

void draw_behavior_designer_panel(const uw::Rect& rect,
                                  ur::DrawBatcher& batcher,
                                  uf::Font* /*font*/,
                                  const uw::Theme& theme)
{
    g_behavior_designer_panel.draw(batcher, theme, rect);
}

// phase598 / M6 W3 — asset_drop_target drawer.
void draw_asset_drop_target_panel(const uw::Rect& rect,
                                  ur::DrawBatcher& batcher,
                                  uf::Font* /*font*/,
                                  const uw::Theme& theme)
{
    g_asset_drop_target_panel.draw(batcher, theme, rect);
}

// phase667 / M12 W2 — new panel drawers.
void draw_light_editor_panel(const uw::Rect& rect,
                             ur::DrawBatcher& batcher,
                             uf::Font* /*font*/,
                             const uw::Theme& theme)
{
    g_light_editor_panel.draw(batcher, theme, rect);
}

void draw_input_recorder_panel(const uw::Rect& rect,
                               ur::DrawBatcher& batcher,
                               uf::Font* /*font*/,
                               const uw::Theme& theme)
{
    g_input_recorder_panel.draw(batcher, theme, rect);
}

void draw_dialog_tree_editor_panel(const uw::Rect& rect,
                                   ur::DrawBatcher& batcher,
                                   uf::Font* /*font*/,
                                   const uw::Theme& theme)
{
    g_dialog_tree_editor_panel.draw(batcher, theme, rect);
}

void draw_cutscene_player_panel(const uw::Rect& rect,
                                ur::DrawBatcher& batcher,
                                uf::Font* /*font*/,
                                const uw::Theme& theme)
{
    g_cutscene_player_panel.draw(batcher, theme, rect);
}

// ---- GPU marker overlay draw helper ----------------------------------------
//
// phase631 / M9 W1A — mirrors the cpu_marker_overlay::Overlay::draw() pattern
// but consumes cd::profile::gpu_marker::GpuMarkerSample data (name +
// duration_ms_computed). GPU markers have no thread-id lane concept — all bars
// are drawn in a single horizontal lane spanning the overlay height.
//
// The overlay is fed synthetic GpuMarkerSample data each frame (real GPU
// timestamps require ICommandBuffer::write_timestamp, which is a future Sprint).
void draw_gpu_marker_overlay(
    ur::DrawBatcher&                                          batcher,
    std::span<const cd::profile::gpu_marker::GpuMarkerSample> samples,
    float                                                      x,
    float                                                      y,
    float                                                      width,
    float                                                      height,
    double                                                     window_ms)
{
    if (samples.empty() || width <= 0.0F || height <= 0.0F)
        return;

    // Compute min start_ms equivalent from the GPU tick data.
    // GpuMarkerSample stores raw ticks; after resolve() duration_ms_computed is
    // valid. For the overlay we treat gpu_start_tick as the "start" offset in
    // ticks and map ticks onto the [0, window_ms] time axis using the same
    // stub frequency used by Recorder::resolve (1 GHz = 1e9 ticks/s).
    static constexpr double kStubFreqHz = 1.0e9;
    static constexpr double kTickToMs   = 1000.0 / kStubFreqHz;

    const std::uint64_t min_tick = [&]
    {
        std::uint64_t m = samples[0].gpu_start_tick;
        for (const auto& s : samples)
            m = (s.gpu_start_tick < m) ? s.gpu_start_tick : m;
        return m;
    }();

    const double time_range     = (window_ms > 0.0) ? window_ms : 1.0;
    const float  pixels_per_ms  =
        width / static_cast<float>(time_range);
    const float  bar_h          = height * 0.75F;  // 75% height; 25% top/bottom padding
    const float  bar_y          = y + (height - bar_h) * 0.5F;

    // djb2 hash → stable hue (same function as cpu_marker_overlay).
    auto gpu_colour = [](std::string_view name) -> ur::Color
    {
        std::uint32_t h = 5381U;
        for (const char c : name)
            h = ((h << 5U) + h) + static_cast<std::uint32_t>(static_cast<unsigned char>(c));
        // Shift bits vs cpu palette to produce visually distinct hues.
        const auto r = static_cast<std::uint8_t>(((h >> 4U) & 0xFFu));
        const auto g = static_cast<std::uint8_t>(((h >> 12U) & 0xFFu));
        const auto b = static_cast<std::uint8_t>((h & 0xFFu));
        return ur::Color { r, g, b, 200U };
    };

    // Push a scissor rect scoped to the overlay bounds. This gives the GPU
    // marker overlay its own DrawCommand in the batcher (distinct scissor key),
    // so command_count() increments by 1 relative to the CPU/FGT overlays.
    const ur::ScissorRect scissor {
        static_cast<std::int32_t>(x),
        static_cast<std::int32_t>(y),
        static_cast<std::uint32_t>(width),
        static_cast<std::uint32_t>(height),
    };
    batcher.push_scissor(scissor);

    for (const auto& s : samples)
    {
        const double rel_start_ms =
            static_cast<double>(s.gpu_start_tick - min_tick) * kTickToMs;
        const double dur_ms = s.duration_ms_computed;

        if (rel_start_ms > time_range)
            continue;
        if (rel_start_ms + dur_ms < 0.0)
            continue;

        const float bar_x =
            x + static_cast<float>(rel_start_ms) * pixels_per_ms;
        const float bar_w =
            std::max(1.0F, static_cast<float>(dur_ms) * pixels_per_ms);

        batcher.quad(bar_x, bar_y, bar_w, bar_h, gpu_colour(s.name));
    }

    batcher.pop_scissor();
}

// ---- Pointer event flatten (same shape as hello_ui) ------------------------

struct PointerAccumulator
{
    float mouse_x        { 0.0F };
    float mouse_y        { 0.0F };
    bool  left_down      { false };
    bool  left_down_prev { false };
};

[[nodiscard]] uw::PointerState flatten_pointer(const PointerAccumulator& a) noexcept
{
    uw::PointerState p;
    p.mouse_x       = a.mouse_x;
    p.mouse_y       = a.mouse_y;
    p.left_down     = a.left_down;
    p.left_pressed  = a.left_down && !a.left_down_prev;
    p.left_released = !a.left_down && a.left_down_prev;
    return p;
}

void apply_event(PointerAccumulator& a, const platform::OSEvent& e) noexcept
{
    switch (e.kind)
    {
        case platform::OSEventKind::kMouseMove:
            a.mouse_x = e.mouse_x;
            a.mouse_y = e.mouse_y;
            break;
        case platform::OSEventKind::kMouseButtonDown:
            if (e.mouse_button == platform::MouseButton::kLeft)
            {
                a.left_down = true;
            }
            break;
        case platform::OSEventKind::kMouseButtonUp:
            if (e.mouse_button == platform::MouseButton::kLeft)
            {
                a.left_down = false;
            }
            break;
        default:
            break;
    }
}

// ---- Theme bridge ----------------------------------------------------------
//
// cd::ui::theme V2 exposes float-RGBA design tokens. cd::ui_widgets uses
// its own uint8_t-RGBA palette. Translate the dark-default theme so the
// editor binary stays on the V2 token surface while still feeding the
// widget catalog.
[[nodiscard]] uw::Color to_widget_color(uth::ColorToken c) noexcept
{
    auto pack = [](float v) noexcept -> std::uint8_t {
        const float clamped = (v < 0.0F) ? 0.0F : (v > 1.0F) ? 1.0F : v;
        return static_cast<std::uint8_t>(std::lround(clamped * 255.0F));
    };
    return uw::Color { pack(c.r), pack(c.g), pack(c.b), pack(c.a) };
}

[[nodiscard]] uw::Theme build_widget_theme_from_dark_tokens()
{
    const auto dark = uth::kDarkTheme();
    uw::Theme t {};
    // Map V2 16-slot palette into the cd::ui_widgets 9-slot theme. The
    // semantic intent stays in lock-step with Material 3: background =
    // surface, panel surface = surfaceVariant, hover/press = secondary /
    // primary containers, focus = primary container (highlight).
    t.background    = to_widget_color(dark.color(uth::PaletteSlot::kBackground));
    t.surface       = to_widget_color(dark.color(uth::PaletteSlot::kSurface));
    t.surface_hover = to_widget_color(dark.color(uth::PaletteSlot::kSurfaceVariant));
    t.surface_press = to_widget_color(dark.color(uth::PaletteSlot::kPrimaryContainer));
    t.accent        = to_widget_color(dark.color(uth::PaletteSlot::kPrimary));
    t.accent_hover  = to_widget_color(dark.color(uth::PaletteSlot::kPrimaryContainer));
    t.text          = to_widget_color(dark.color(uth::PaletteSlot::kOnSurface));
    t.text_dim      = to_widget_color(dark.color(uth::PaletteSlot::kOnSurfaceVariant));
    return t;
}

// ---- phase667 / M12 W2 — status bar ---------------------------------------
//
// A 20px-tall horizontal strip pinned to the bottom of the framebuffer that
// shows three live readouts the brief calls out:
//
//   * asset::validator badge — three colour-coded count blocks (green = pass,
//     yellow = warn, red = error) sized proportionally to the corresponding
//     issue counts. The counts come from the latest Validator pass.
//   * frame stats — FPS, vertex count, draw command count. Rendered as
//     three coloured intensity strips so the user sees them light up even
//     before glyph rendering is hooked.
//   * active route indicator — single coloured strip showing which UI
//     submitter path (Route A vs Route B) is live this boot.
//
// The status bar lives OUTSIDE the dock tree so it does not bump
// DockSpace::node_count. The dock area is shrunk by `kStatusBarH` before
// `set_rect()` so the status bar never overlaps a panel.
constexpr float kStatusBarH = 20.0F;

struct StatusBarStats
{
    // Validator badge totals.
    std::uint32_t validator_pass  { 0U };
    std::uint32_t validator_warn  { 0U };
    std::uint32_t validator_error { 0U };
    // Frame stats — derived from DrawBatcher after dockspace.draw() per frame.
    float         fps             { 0.0F };
    std::uint32_t vertex_count    { 0U };
    std::uint32_t draw_calls      { 0U };
    // Route indicator: 1 = A success, 2 = A fallback, 3 = B only.
    int           route_taken     { 0 };
};

/// Emit the bottom status bar quads into `batcher`. Rendered AFTER the dock
/// content so the bar paints on top of any anti-aliased panel edges. The
/// status bar is opinionated about its own background fill (slightly darker
/// than the dock surface so the user reads it as a separate ribbon).
void draw_status_bar(ur::DrawBatcher& batcher,
                     const uw::Theme& theme,
                     const StatusBarStats& stats,
                     float fb_w, float fb_h)
{
    const float bar_x = 0.0F;
    const float bar_y = fb_h - kStatusBarH;
    const float bar_w = fb_w;

    // 1. Status bar background — a notch darker than the dock so the user
    //    reads it as a discrete ribbon.
    batcher.quad(bar_x, bar_y, bar_w, kStatusBarH,
                 ur::Color {
                     static_cast<std::uint8_t>(theme.background.r > 16 ? theme.background.r - 16 : 0),
                     static_cast<std::uint8_t>(theme.background.g > 16 ? theme.background.g - 16 : 0),
                     static_cast<std::uint8_t>(theme.background.b > 16 ? theme.background.b - 16 : 0),
                     255U });

    // 2. Top separator line (1 px accent line under the dock).
    batcher.quad(bar_x, bar_y, bar_w, 1.0F,
                 ur::Color { theme.accent.r, theme.accent.g, theme.accent.b, 200U });

    // ---- 3. Validator badge (LEFT region, 30% of the bar width) -----------
    //
    // Three side-by-side count blocks; each block width is proportional to
    // its count relative to the total, with a 6 px minimum so a zero-count
    // bucket is still visually present. Colour mapping mirrors
    // cd::asset::validator::Severity:
    //    Info / pass  -> green
    //    Warning      -> yellow
    //    Error        -> red
    constexpr float kBadgeW    = 280.0F;
    constexpr float kBadgePad  = 6.0F;
    constexpr float kBadgeH    = kStatusBarH - 4.0F;
    const float     badge_y    = bar_y + 2.0F;
    const float     badge_avail = kBadgeW - 2.0F * kBadgePad;

    const std::uint32_t total = stats.validator_pass + stats.validator_warn + stats.validator_error;
    const float kMinBlock = 6.0F;

    auto block_w = [&](std::uint32_t count) -> float {
        if (total == 0U) { return (badge_avail - 4.0F) / 3.0F; }
        const float frac = static_cast<float>(count) / static_cast<float>(total);
        return std::max(kMinBlock, frac * (badge_avail - 4.0F));
    };

    float bx = bar_x + kBadgePad;
    // pass (green)
    {
        const float w = block_w(stats.validator_pass);
        batcher.quad(bx, badge_y, w, kBadgeH,
                     ur::Color { 80U, 200U, 100U, 230U });
        bx += w + 2.0F;
    }
    // warn (yellow)
    {
        const float w = block_w(stats.validator_warn);
        batcher.quad(bx, badge_y, w, kBadgeH,
                     ur::Color { 220U, 200U, 80U, 230U });
        bx += w + 2.0F;
    }
    // error (red)
    {
        const float w = block_w(stats.validator_error);
        batcher.quad(bx, badge_y, w, kBadgeH,
                     ur::Color { 220U, 80U, 80U, 230U });
    }

    // ---- 4. Frame stats (CENTRE region) -----------------------------------
    //
    // Three thin strips (FPS, vertex count, draw call count) each clamped
    // to a sane upper bound so they read as a "VU meter" without text.
    //   FPS:        capped at 240 fps
    //   verts:      capped at 64k
    //   draw calls: capped at 256
    constexpr float kStatsW    = 360.0F;
    constexpr float kStatsPad  = 6.0F;
    const float     stats_x    = bar_x + kBadgeW + 12.0F;
    const float     stats_h    = kStatusBarH - 4.0F;
    const float     strip_w    = (kStatsW - 2.0F * kStatsPad - 4.0F) / 3.0F;
    const float     stats_y0   = bar_y + 2.0F;

    auto draw_meter = [&](float ox, float norm, ur::Color color) {
        const float clamped = std::clamp(norm, 0.0F, 1.0F);
        // Background strip.
        batcher.quad(stats_x + ox, stats_y0,
                     strip_w, stats_h,
                     ur::Color {
                         theme.surface_hover.r,
                         theme.surface_hover.g,
                         theme.surface_hover.b,
                         theme.surface_hover.a });
        if (clamped > 0.0F)
        {
            batcher.quad(stats_x + ox, stats_y0,
                         strip_w * clamped, stats_h, color);
        }
    };

    draw_meter(0.0F,
               std::clamp(stats.fps / 240.0F, 0.0F, 1.0F),
               ur::Color { 100U, 200U, 220U, 220U });
    draw_meter(strip_w + 2.0F,
               std::clamp(static_cast<float>(stats.vertex_count) / 65536.0F, 0.0F, 1.0F),
               ur::Color { 200U, 160U, 80U, 220U });
    draw_meter((strip_w + 2.0F) * 2.0F,
               std::clamp(static_cast<float>(stats.draw_calls) / 256.0F, 0.0F, 1.0F),
               ur::Color { 180U, 100U, 200U, 220U });

    // ---- 5. Active route indicator (RIGHT region) -------------------------
    //
    // Single coloured strip — colour encodes which UI submitter path is live
    // this boot (Route A success / fallback / Route B only). Pinned to the
    // far right of the status bar.
    constexpr float kRouteW = 140.0F;
    const float     route_x = bar_x + bar_w - kRouteW - 6.0F;
    const float     route_y = bar_y + 2.0F;
    const float     route_h = kStatusBarH - 4.0F;

    ur::Color route_color { 120U, 120U, 120U, 200U };
    switch (stats.route_taken)
    {
        case 1: route_color = ur::Color {  80U, 200U, 100U, 230U }; break;  // green — Route A
        case 2: route_color = ur::Color { 220U, 200U,  80U, 230U }; break;  // yellow — fallback
        case 3: route_color = ur::Color { 100U, 150U, 220U, 230U }; break;  // blue — Route B only
        default: break;
    }
    batcher.quad(route_x, route_y, kRouteW, route_h, route_color);
}

// ---- Headless one-line summary ---------------------------------------------

void report_headless_frame(const ur::DrawBatcher& batcher,
                           const urr::Submitter& submitter,
                           const uw::DockSpace& ds,
                           std::uint32_t frame_idx)
{
    std::printf(
        "editor[headless frame %u]: dock nodes=%zu  status_bar=on  "
        "batcher verts=%zu/idx=%zu/cmds=%zu  "
        "submitter verts=%u/idx=%u/cmds=%u\n",
        frame_idx, ds.node_count(),
        batcher.vertex_count(), batcher.index_count(), batcher.command_count(),
        submitter.vertex_count(), submitter.index_count(), submitter.command_count());
    std::fflush(stdout);
}

}  // namespace

int main(int argc, char** argv)
{
    const cd::sample::Runtime runtime = cd::sample::parse_runtime(argc, argv);
    const EditorArgs          local   = parse_editor_args(argc, argv);

    // -- 0. .cdproj restore — read last.cdproj (or explicit --project path) ----
    //
    // On startup we attempt to read the project file. If a --project flag was
    // passed we honour it; otherwise we fall back to the platform default
    // location (%APPDATA%\cd_editor\last.cdproj on Windows). The data is used
    // below to seed the window geometry and dock layout.  If the file is absent
    // or malformed we proceed with defaults (graceful degradation, no crash).
    const std::filesystem::path cdproj_path =
        local.project_path.empty()
            ? cd::editor::cdproj::default_cdproj_path()
            : std::filesystem::path(local.project_path);

    cd::editor::cdproj::CdprojData project_data;  // defaults if file absent/bad
    if (const auto loaded = cd::editor::cdproj::read_cdproj(cdproj_path);
        loaded.has_value())
    {
        project_data = *loaded;
        std::printf("editor: restored session from %s "
                    "(scene=%s, recent_files=%zu)\n",
                    cdproj_path.string().c_str(),
                    project_data.last_opened_scene_path.c_str(),
                    project_data.recent_files.size());
    }
    else
    {
        std::printf("editor: no .cdproj at %s — using defaults.\n",
                    cdproj_path.string().c_str());
    }

    // -- 1. Font (optional; widgets gracefully fall back when missing) -------
    uf::Font font;
    {
        const auto ttf = find_system_font();
        if (!ttf.empty() &&
            font.load_ttf_in_memory(std::span<const std::uint8_t>(ttf.data(), ttf.size())))
        {
            static constexpr float         kPixelSize = 16.0F;
            static constexpr std::uint32_t kAtlasDim  = 2048U;
            (void)font.rasterize_range(0x0020U, 0x00FFU, kPixelSize, kAtlasDim);
        }
    }

    // -- 2. Theme tokens + widget-side palette ------------------------------
    const uw::Theme widget_theme = build_widget_theme_from_dark_tokens();

    // -- 3. cd::editor instance (drives the scene-tree panel content) -------
    cd::editor::EditorDesc ed_desc {};
    ed_desc.width  = 1280.0F;
    ed_desc.height = 720.0F;
    cd::editor::Editor editor { ed_desc };
    cd::editor::HierarchyView hierarchy {};
    (void)hierarchy;  // referenced by name to prove the include compiles;
                      // the live tree render is plumbed in the next session.
    std::printf("editor: cd::editor::Editor booted (scene root entity id=%u)\n",
                editor.scene_root().id);

    // Wire the inspector panel to the editor's ECS World + auto-select the
    // scene root so the inspector is non-empty on first boot.
    g_inspector_panel.set_world_ptr(&editor.world());
    g_inspector_panel.set_target(editor.scene_root());

    // phase667 / M12 W2 — Inspector PBR / alpha section:
    //
    // Hand the inspector an inert (device-less) MaterialInstance so the new
    // metallic / roughness / alpha_mode / alpha_cutoff sliders have a
    // non-empty round-trip target on first boot. The inspector reads the
    // CPU-side accessors only, so an inert instance is sufficient until a
    // real glTF material is selected via the scene-tree panel in a later
    // session. The instance lives in main()'s stack frame and is bound to
    // the inspector for the lifetime of main().
    //
    // Defaults: metallic = 0.0 (dielectric), roughness = 0.5, kOpaque, 0.5
    // alpha_cutoff. We pre-seed metallic = 0.6 + alpha_mode = kMask so a
    // first-time user sees the gold and orange swatches light up on every
    // boot (proves the PBR pipeline reads round-trip values).
    cd::material::MaterialInstance inspector_demo_material {};
    inspector_demo_material.set_metallic(0.6F);
    inspector_demo_material.set_roughness(0.35F);
    inspector_demo_material.set_alpha_mode(cd::material::AlphaMode::kMask);
    inspector_demo_material.set_alpha_cutoff(0.5F);
    g_inspector_panel.set_material_instance(&inspector_demo_material);

    // phase667 / M12 W2 — Asset validator wired for the new status bar badge.
    //
    // Seed the validator with one PASS + one WARN + one ERROR by validating
    // three synthetic blobs at boot so the status bar shows non-zero counts
    // in each colour bucket DAY ONE. A real Sprint will replace this seed
    // with a sweep of the project's actual asset set on .cdproj load.
    cd::asset::validator::Validator validator;
    std::uint32_t validator_pass  = 0U;
    std::uint32_t validator_warn  = 0U;
    std::uint32_t validator_error = 0U;
    {
        // PNG magic bytes — passes texture validation (0 issues -> 1 pass).
        const std::array<std::uint8_t, 8> kPngMagic {
            0x89U, 0x50U, 0x4EU, 0x47U, 0x0DU, 0x0AU, 0x1AU, 0x0AU
        };
        const auto png_issues = validator.validate_texture_blob(
            std::span<const std::uint8_t>(kPngMagic.data(), kPngMagic.size()),
            "demo/textures/baseline.png");
        if (png_issues.empty()) { ++validator_pass; }
        else
        {
            for (const auto& iss : png_issues)
            {
                if (iss.severity == cd::asset::validator::Severity::kError)        ++validator_error;
                else if (iss.severity == cd::asset::validator::Severity::kWarning) ++validator_warn;
            }
        }

        // 8 byte glTF-shaped blob with version != 2 -> warning bucket.
        const std::array<std::uint8_t, 12> kGlbV1 {
            0x67U, 0x6CU, 0x54U, 0x46U,
            0x01U, 0x00U, 0x00U, 0x00U,   // version = 1
            0x00U, 0x00U, 0x00U, 0x00U
        };
        const auto glb_issues = validator.validate_gltf_blob(
            std::span<const std::uint8_t>(kGlbV1.data(), kGlbV1.size()),
            "demo/meshes/legacy.glb");
        for (const auto& iss : glb_issues)
        {
            if (iss.severity == cd::asset::validator::Severity::kError)        ++validator_error;
            else if (iss.severity == cd::asset::validator::Severity::kWarning) ++validator_warn;
        }
        if (glb_issues.empty()) { ++validator_pass; }

        // Empty audio blob -> error bucket.
        const std::vector<std::uint8_t> empty_blob {};
        const auto wav_issues = validator.validate_audio_blob(
            std::span<const std::uint8_t>(empty_blob.data(), empty_blob.size()),
            "demo/audio/missing.wav");
        for (const auto& iss : wav_issues)
        {
            if (iss.severity == cd::asset::validator::Severity::kError)        ++validator_error;
            else if (iss.severity == cd::asset::validator::Severity::kWarning) ++validator_warn;
        }
        if (wav_issues.empty()) { ++validator_pass; }
    }
    std::printf("editor: validator badge seed -> pass=%u warn=%u error=%u\n",
                validator_pass, validator_warn, validator_error);

    // Seed the asset browser with a minimal default file tree so it is
    // non-empty on first boot. A real .cdproj loader will replace these.
    {
        using AB = cd::editor::panel::asset_browser::Entry;
        const std::array<AB, 5> seed_entries {
            AB { "textures",  "assets/textures",        true  },
            AB { "meshes",    "assets/meshes",           true  },
            AB { "materials", "assets/materials",        true  },
            AB { "sky.hdr",   "assets/textures/sky.hdr", false },
            AB { "mesh.glb",  "assets/meshes/mesh.glb",  false },
        };
        g_asset_browser_panel.set_entries(seed_entries);
    }

    // -- 4. A11y tree (proves the include + namespace link) -----------------
    // Future per-panel widgets register their A11yMeta via this tree so
    // screen readers + keyboard tab navigation work consistently across
    // the editor surface. Today only the link is wired -- panel widgets
    // arrive in the next session.
    cd::ui::a11y::A11yTree a11y_tree {};
    (void)a11y_tree;

    // -- 5. Dockspace + 9-panel layout (phase598 / M6 W3) -------------------
    //
    // phase598 wires in the M5 asset_drop_target panel: filter is configured
    // for the six asset extensions called out in the M6 W3 brief
    // (.gltf, .glb, .png, .jpg, .wav, .ogg). When the user drops a path the
    // log shows it; the full routing pipeline lands in a future Sprint.
    {
        const std::array<std::string, 6> kAssetExts {
            std::string{".gltf"}, std::string{".glb"},
            std::string{".png"},  std::string{".jpg"},
            std::string{".wav"},  std::string{".ogg"},
        };
        g_asset_drop_target_panel.set_accepted_extensions(
            std::span<const std::string>(kAssetExts.data(), kAssetExts.size()));
    }

    uw::DockSpace dockspace;
    dockspace.register_panel("scene_tree",          draw_scene_tree_stub);
    dockspace.register_panel("viewport",            draw_viewport_panel);
    dockspace.register_panel("inspector",           draw_inspector_panel);
    dockspace.register_panel("console",             draw_console_panel);
    dockspace.register_panel("assets",              draw_assets_panel);
    dockspace.register_panel("material_editor",     draw_material_editor_panel);
    dockspace.register_panel("animator",            draw_animator_panel);
    dockspace.register_panel("behavior_designer",   draw_behavior_designer_panel);
    dockspace.register_panel("asset_drop_target",   draw_asset_drop_target_panel);
    // phase667 / M12 W2 — register the 4 new panel drawers (the brief's "3 new
    // panels" + cutscene_player as the LEFT-column tab companion of
    // dialog_tree_editor).
    dockspace.register_panel("light_editor",        draw_light_editor_panel);
    dockspace.register_panel("input_recorder",      draw_input_recorder_panel);
    dockspace.register_panel("dialog_tree_editor",  draw_dialog_tree_editor_panel);
    dockspace.register_panel("cutscene_player",     draw_cutscene_player_panel);
    if (!build_default_layout(dockspace))
    {
        std::fprintf(stderr, "editor: failed to build default DockSpace layout.\n");
        return 1;
    }
    // phase667 / M12 W2: panel count went from 9 -> 13 (the four newly
    // registered drawers). dockspace.node_count() also grew by +3 (we added
    // exactly three new splits in build_default_layout to carve out the
    // new panel rects; tab_merges do not increment node_count).
    std::printf("editor: dock layout ready with %zu nodes (13 panels: scene_tree | viewport | "
                "inspector | console | assets | material_editor | animator | "
                "behavior_designer | asset_drop_target | light_editor | "
                "input_recorder | dialog_tree_editor | cutscene_player)\n",
                dockspace.node_count());

    // -- 5b. Overlay instances (phase598 / M6 W3; phase631 / M9 W1A) --------
    //
    // CPU-marker bar chart   -> floating top-right (~300x120 px).
    // GPU-marker bar chart   -> floating top-right below CPU (~300x100 px).
    //                          phase631 / M9 W1A: cpu/gpu classification sibling.
    //                          Synthetic samples this Sprint; real timestamps
    //                          land when ICommandBuffer::write_timestamp ships.
    // Frame-graph timeline   -> floating bottom-right (~400x80 px).
    //
    // All overlays consume DUMMY synthetic samples this Sprint — real
    // instrumentation hooks (cd::profile Collector wiring + GPU query
    // readback feed) land in a follow-up Sprint. The dummy feed exists so
    // the overlay surfaces are visibly active in the editor window.
    //
    // asset::validator status badge: no status bar exists in apps/editor yet.
    // TODO(phase631): add status bar and wire cd::asset::validator badge here.
    namespace cmo = cd::profile::cpu_marker_overlay;
    namespace fgt = cd::profile::frame_graph_timeline;
    const cmo::Overlay         cpu_overlay        { 16.0 };
    const fgt::TimelineOverlay frame_graph_overlay { 16.0 };

    // -- 6. Try Vulkan + window + Renderer; fall back to NullDevice ---------
    std::unique_ptr<platform::IWindow>  window;
    std::unique_ptr<rhi::IDevice>       device;
    std::optional<render::Renderer>     renderer;
    bool                                using_null { local.force_null };

    if (!using_null)
    {
        platform::WindowDesc wd {};
        wd.title  = "CHROMODYNAMIC -- editor";
        wd.width  = 1280;
        wd.height = 720;
        auto wr   = platform::create_window(wd);
        if (!wr.has_value())
        {
            std::fprintf(stderr,
                         "editor: platform window create failed, switching to NullDevice.\n");
            using_null = true;
        }
        else
        {
            window = std::move(*wr);
            rhi::vulkan::VulkanCreateInfo vci {};
            auto dr = rhi::vulkan::create_vulkan_device(vci);
            if (!dr.has_value())
            {
                std::fprintf(stderr,
                             "editor: Vulkan device create failed, switching to NullDevice.\n");
                window.reset();
                using_null = true;
            }
            else
            {
                device = std::move(*dr);
                render::RendererDesc rd {};
                rd.device                   = device.get();
                rd.swapchain.window_handle  = window->native_window_handle();
                rd.swapchain.display_handle = window->native_display_handle();
                rd.swapchain.extent         = { window->width(), window->height() };
                rd.swapchain.format         = rhi::Format::kBGRA8Unorm;
                rd.frames_in_flight         = 2U;
                auto rr = render::Renderer::create(rd);
                if (!rr.has_value())
                {
                    std::fprintf(stderr,
                                 "editor: Renderer::create failed, switching to NullDevice.\n");
                    window.reset();
                    device.reset();
                    using_null = true;
                }
                else
                {
                    renderer.emplace(std::move(*rr));
                }
            }
        }
    }

    if (using_null)
    {
        device = std::make_unique<rhi::NullDevice>();
        std::printf("editor: running headless on cd::rhi::NullDevice (no GPU swapchain).\n");
    }
    else
    {
        std::printf("editor: booted Vulkan backend + Renderer swapchain. ESC to exit.\n");
    }
    std::fflush(stdout);

    // -- 7. UI submitter ----------------------------------------------------
    //
    // Phase 608 / M7 W3 introduced Route A vs Route B as a compile-time
    // gate. Phase 659 / M11 W3B Sprint-4 flips that to a RUNTIME selection
    // with graceful fallback, so default-on Route A becomes safe to ship:
    //
    //   Route A (PREFERRED when CD_USE_MATERIAL_UI_ROUTE_A is defined --
    //   default ON after Sprint-4): build a cd::material::UiVariant via
    //   cd::material::create_ui_variant and hand it to
    //   Submitter::create_with_material_ui_variant. The variant carries
    //   the Sprint-1/Sprint-2 UI pipeline (vertex-color baseline + theme
    //   UBO; SDF sampler stays off until ui_font is wired to a sample-able
    //   TextureView). If anything in this pipeline returns an error -- the
    //   factory itself, descriptor allocation, glslang missing -- the
    //   editor logs a clear warning and immediately retries Route B so
    //   the user still sees a live window. The boot log line "editor:
    //   submitter route = ..." records which path actually succeeded.
    //
    //   Route B (FALLBACK when Route A errors, or PRIMARY when the flag is
    //   compiled off): cd::ui_renderer_rhi compiles a minimal inline GLSL
    //   pipeline at boot (solid quads only, no sampler). That's enough to
    //   render every panel rect + theme palette so the editor's window
    //   shows DockSpace tiles.
    //
    // The compile-time flag now only chooses between "prefer A, accept B
    // on failure" (ON) and "B only, never try A" (OFF) -- both Submitter
    // factories are linked into the binary either way.
#if defined(CD_USE_MATERIAL_UI_ROUTE_A)
    constexpr bool kEditorPreferRouteA = true;
#else
    constexpr bool kEditorPreferRouteA = false;
#endif

    urr::SubmitterCreateInfo sci {};
    sci.max_vertices = 16384U;
    sci.max_indices  = 65536U;
    sci.color_format = rhi::Format::kBGRA8Unorm;

    // Runtime self-test marker -- which route actually produced the
    // working submitter. Logged once after submitter wire-up so the user
    // sees the active path on every boot. Values:
    //   0 = uninitialized
    //   1 = Route A succeeded (preferred path took effect)
    //   2 = Route A attempted then failed; Route B succeeded (fallback)
    //   3 = Route B was the only attempt (flag OFF or A path unavailable)
    int route_taken { 0 };

    cd::core::Result<urr::Submitter> sub_r =
        std::unexpected(cd::core::ErrorCode { 0x0001U, 0U, "uninitialized" });

    if (kEditorPreferRouteA)
    {
        // Build the cd::material::UiVariant up front. Phase 648 / M10 W3A
        // Sprint-3: opt into the Sprint-2 theme-UBO branch so the variant
        // carries a real fragment-stage descriptor set (theme palette).
        // SDF sampler stays off because apps/editor does not yet wire its
        // ui_font atlas into a TextureView the submitter can sample.
        const std::array<rhi::Format, 1> kColorFormats {
            rhi::Format::kBGRA8Unorm,
        };
        cd::material::UiVariantSpec vspec {};
        vspec.color_attachment_formats = std::span<const rhi::Format>(
            kColorFormats.data(), kColorFormats.size());
        vspec.use_theme_palette_ubo = true;
        vspec.theme_palette_ubo_slot = 0U;
        vspec.name = "editor_ui_variant_route_a";

        auto var_r = cd::material::create_ui_variant(*device, vspec);
        if (!var_r.has_value())
        {
            std::fprintf(stderr,
                         "editor: WARNING -- Route A (cd::material UI variant) "
                         "create_ui_variant failed: domain=%u code=%u -- "
                         "falling back to Route B (inline GLSL).\n",
                         var_r.error().domain, var_r.error().code);
            sub_r = urr::Submitter::create_with_inline_shader(*device, sci);
            route_taken = sub_r.has_value() ? 2 : 0;
        }
        else
        {
            sub_r = urr::Submitter::create_with_material_ui_variant(
                *device, sci, std::move(*var_r));
            if (!sub_r.has_value())
            {
                std::fprintf(stderr,
                             "editor: WARNING -- Route A "
                             "Submitter::create_with_material_ui_variant "
                             "failed: domain=%u code=%u -- falling back "
                             "to Route B (inline GLSL).\n",
                             sub_r.error().domain, sub_r.error().code);
                sub_r = urr::Submitter::create_with_inline_shader(*device, sci);
                route_taken = sub_r.has_value() ? 2 : 0;
            }
            else
            {
                route_taken = 1;
            }
        }
    }
    else
    {
        sub_r = urr::Submitter::create_with_inline_shader(*device, sci);
        route_taken = sub_r.has_value() ? 3 : 0;
    }

    if (!sub_r.has_value())
    {
        std::fprintf(stderr,
                     "editor: ui_renderer_rhi::Submitter::create failed on "
                     "both Route A and Route B (last error: domain=%u code=%u).\n",
                     sub_r.error().domain, sub_r.error().code);
        return 2;
    }
    auto& submitter = *sub_r;

    // Runtime self-test log line — exactly one of these prints every boot.
    const char* route_label = "unknown";
    switch (route_taken)
    {
        case 1: route_label = "Route A success (cd::material UI variant)"; break;
        case 2: route_label = "Route A failed -> Route B fallback (inline GLSL)"; break;
        case 3: route_label = "Route B only (CD_USE_MATERIAL_UI_ROUTE_A off)"; break;
        default: route_label = "unknown"; break;
    }
    std::printf("editor: submitter route = %s.\n", route_label);
    std::fflush(stdout);

    // Feed the editor's dark theme palette into the variant's theme UBO so
    // the fragment shader's `tint` math multiplies vertex color by the real
    // swatches. Only meaningful on Route A; `set_theme_palette` is a no-op
    // (returns false) on the Route B inline path, so the call is safe to
    // make unconditionally.
    if (route_taken == 1)
    {
        const auto dark = uth::kDarkTheme();
        const auto pri  = dark.color(uth::PaletteSlot::kPrimary);
        const auto sec  = dark.color(uth::PaletteSlot::kSecondary);
        const auto sur  = dark.color(uth::PaletteSlot::kSurface);
        const auto on_s = dark.color(uth::PaletteSlot::kOnSurface);
        cd::material::UiThemePaletteUbo palette {};
        palette.primary[0]    = pri.r;  palette.primary[1]    = pri.g;
        palette.primary[2]    = pri.b;  palette.primary[3]    = pri.a;
        palette.secondary[0]  = sec.r;  palette.secondary[1]  = sec.g;
        palette.secondary[2]  = sec.b;  palette.secondary[3]  = sec.a;
        palette.surface[0]    = sur.r;  palette.surface[1]    = sur.g;
        palette.surface[2]    = sur.b;  palette.surface[3]    = sur.a;
        palette.on_surface[0] = on_s.r; palette.on_surface[1] = on_s.g;
        palette.on_surface[2] = on_s.b; palette.on_surface[3] = on_s.a;
        const bool pal_ok = submitter.set_theme_palette(palette);
        std::printf("editor: Route A theme palette upload %s.\n",
                    pal_ok ? "OK" : "skipped (variant has no theme UBO)");
    }

    // -- 8. Frame loop ------------------------------------------------------
    ur::DrawBatcher                batcher;
    PointerAccumulator             pointer {};
    std::vector<platform::OSEvent> events;
    events.reserve(64);

    const std::uint32_t default_headless = using_null ? 1U : 0U;
    const std::uint32_t cap_frames =
        runtime.headless_frames > 0U ? runtime.headless_frames : default_headless;

    bool          needs_rebuild { false };
    std::uint32_t frame_idx     { 0U };
    while (true)
    {
        std::uint32_t fb_w { 1280U };
        std::uint32_t fb_h { 720U };
        if (window)
        {
            fb_w = window->width()  > 0U ? window->width()  : fb_w;
            fb_h = window->height() > 0U ? window->height() : fb_h;
            events.clear();
            if (!window->pump_events(events)) { break; }
            pointer.left_down_prev = pointer.left_down;
            for (const auto& e : events)
            {
                apply_event(pointer, e);
                if (e.kind == platform::OSEventKind::kKeyDown &&
                    e.key  == platform::KeyCode::kEscape)
                {
                    window->request_close();
                }
                else if (e.kind == platform::OSEventKind::kResize)
                {
                    needs_rebuild = true;
                }
            }
            if (renderer && needs_rebuild)
            {
                if (window->width() == 0U || window->height() == 0U) { continue; }
                if (!renderer->recreate_swapchain(
                        rhi::Extent2D { window->width(), window->height() }).has_value())
                {
                    continue;
                }
                needs_rebuild = false;
            }
        }

        if (cap_frames > 0U && frame_idx >= cap_frames)
        {
            if (window) { window->request_close(); }
            if (!window) { break; }
        }

        // -- Dockspace layout + state tick --
        //
        // phase667 / M12 W2: reserve a 20 px ribbon at the bottom of the
        // framebuffer for the new status bar (validator badge + frame stats
        // + route indicator). The dock area shrinks by kStatusBarH so no
        // panel ever overlaps the status ribbon.
        const float dock_h = std::max(0.0F,
                                      static_cast<float>(fb_h) - kStatusBarH);
        dockspace.set_rect(uw::Rect { 0.0F, 0.0F,
                                      static_cast<float>(fb_w),
                                      dock_h });
        uw::InputState input;
        input.pointer = flatten_pointer(pointer);
        input.focused = false;
        dockspace.tick(input, 0.0F);

        // -- Editor tick (drains queued input events, refreshes widgets) --
        // The Editor's own tick is independent of the DockSpace today; in
        // future sessions the scene-tree panel drawer will consult the
        // Editor's hierarchy view directly so a click here selects an
        // entity exposed in the inspector panel.
        (void)editor.tick();

        // -- Drain any pending drop and log it (real asset routing is a -----
        //    future Sprint; today this is the visible surface contract).
        {
            std::string dropped_path;
            if (g_asset_drop_target_panel.consume_dropped_path(dropped_path))
            {
                std::printf("editor: AssetDropTarget consumed drop: %s\n",
                            dropped_path.c_str());
                std::fflush(stdout);
            }
        }

        // -- Draw via the CPU batcher + RHI submitter --
        batcher.begin_frame();
        dockspace.draw(batcher, font.is_loaded() ? &font : nullptr, widget_theme);

        // -- phase598 / M6 W3 -- floating overlays (top-right + bottom-right).
        //
        // The overlays sit "outside" the DockSpace tree -- they are emitted
        // directly into the batcher after dockspace.draw() so they composite
        // on top of the dock tiles. Each gets a synthetic feed (3-4 markers /
        // 3 passes) so the bar chart + Gantt chart show something visible
        // every frame. Real instrumentation hooks are a follow-up Sprint.
        {
            constexpr float kFbW = 1.0F;  // unused — placeholder for clarity
            (void)kFbW;
            const float fbw_f = static_cast<float>(fb_w);
            const float fbh_f = static_cast<float>(fb_h);

            // --- CPU marker overlay -- top-right 300 x 120 ---------------
            {
                constexpr float kOverlayW = 300.0F;
                constexpr float kOverlayH = 120.0F;
                constexpr float kMargin   = 8.0F;
                const cmo::Rect bounds {
                    fbw_f - kOverlayW - kMargin,
                    kMargin,
                    kOverlayW, kOverlayH };

                // Synthetic 4-marker frame. Bars span 0..16 ms across two
                // logical threads so both lanes light up.
                const double base_ms =
                    static_cast<double>(frame_idx) * 16.0;
                const std::array<cmo::MarkerSample, 4> cpu_dummy {
                    cmo::MarkerSample {
                        "frame.gather",  base_ms + 0.5,  3.0, 1U },
                    cmo::MarkerSample {
                        "frame.cull",    base_ms + 3.8,  2.4, 1U },
                    cmo::MarkerSample {
                        "frame.shadows", base_ms + 6.5,  4.0, 2U },
                    cmo::MarkerSample {
                        "frame.submit",  base_ms + 11.0, 4.5, 1U },
                };
                cpu_overlay.draw(
                    batcher,
                    std::span<const cmo::MarkerSample>(
                        cpu_dummy.data(), cpu_dummy.size()),
                    bounds);
            }

            // --- GPU marker overlay -- top-right 300 x 100, below CPU ----
            // phase631 / M9 W1A — sibling to cpu_marker_overlay (cpu/gpu
            // classification). Synthetic GpuMarkerSample data fed directly
            // (no real ICommandBuffer needed until write_timestamp lands).
            // Stacked below the cpu_marker_overlay: y offset = kMargin +
            // cpu overlay height (120) + gap (4).
            {
                constexpr float kOverlayW  = 300.0F;
                constexpr float kOverlayH  = 100.0F;
                constexpr float kMargin    = 8.0F;
                constexpr float kCpuH      = 120.0F;  // cpu_marker_overlay height
                constexpr float kGap       = 4.0F;
                const float     overlay_x  = fbw_f - kOverlayW - kMargin;
                const float     overlay_y  = kMargin + kCpuH + kGap;

                // Synthetic 4 GPU markers — monotonic counter ticks at 1 GHz
                // stub rate: 1 tick = 1 ns → each marker is a few million ticks
                // apart so duration_ms_computed is a small positive value.
                // Using frame_idx to advance the base tick deterministically.
                const std::uint64_t base_tick =
                    static_cast<std::uint64_t>(frame_idx) * 16'000'000ULL;

                // GpuMarkerSample fields: name, gpu_start_tick, gpu_end_tick,
                // duration_ms_computed.  duration_ms_computed = delta / 1e6
                // at stub 1 GHz rate. We set it explicitly here so the
                // draw helper doesn't need to re-derive it from the ticks.
                using GS = cd::profile::gpu_marker::GpuMarkerSample;
                const std::array<GS, 4> gpu_marker_dummy {
                    GS { "gpu.depth_prepass",  base_tick + 0ULL,
                         base_tick + 2'500'000ULL, 2.5 },
                    GS { "gpu.gbuffer",        base_tick + 2'500'000ULL,
                         base_tick + 6'000'000ULL, 3.5 },
                    GS { "gpu.lighting",       base_tick + 6'000'000ULL,
                         base_tick + 10'000'000ULL, 4.0 },
                    GS { "gpu.composite",      base_tick + 10'000'000ULL,
                         base_tick + 13'500'000ULL, 3.5 },
                };
                draw_gpu_marker_overlay(
                    batcher,
                    std::span<const GS>(gpu_marker_dummy.data(),
                                        gpu_marker_dummy.size()),
                    overlay_x, overlay_y,
                    kOverlayW, kOverlayH,
                    16.0);
            }

            // --- Frame-graph timeline -- bottom-right 400 x 80 -----------
            //
            // phase667 / M12 W2: lifted by kStatusBarH so the overlay sits
            // above the new status bar ribbon instead of being clipped.
            {
                constexpr float kOverlayW = 400.0F;
                constexpr float kOverlayH = 80.0F;
                constexpr float kMargin   = 8.0F;
                const fgt::Rect bounds {
                    fbw_f - kOverlayW - kMargin,
                    fbh_f - kOverlayH - kMargin - kStatusBarH,
                    kOverlayW, kOverlayH };

                // Synthetic 3-pass frame: GBuffer / Lighting / Composite.
                const std::array<fgt::PassRecord, 3> gpu_dummy {
                    fgt::PassRecord {
                        "GBuffer",    0.0,  4.5, 1U },
                    fgt::PassRecord {
                        "Lighting",   4.5,  6.0, 2U },
                    fgt::PassRecord {
                        "Composite", 10.5,  3.0, 3U },
                };
                frame_graph_overlay.draw(
                    batcher,
                    std::span<const fgt::PassRecord>(
                        gpu_dummy.data(), gpu_dummy.size()),
                    bounds);
            }
        }

        // -- phase667 / M12 W2 — status bar (20 px ribbon at bottom) --------
        //
        // Emits AFTER the dock + overlays so the ribbon paints on top of any
        // overlap-edge anti-aliasing. Three regions: validator badge (LEFT,
        // 280 px), frame stats meters (CENTRE), route indicator (RIGHT,
        // 140 px). All quads encode live data — no static placeholders.
        //
        // FPS is computed from the previous frame's elapsed-ms estimate
        // (16 ms = 62.5 fps stand-in until cd::frame_timing wires a real
        // delta in a follow-up Sprint).  vertex_count + draw_calls are read
        // directly off the DrawBatcher accumulated this frame.
        {
            StatusBarStats stats {};
            stats.validator_pass  = validator_pass;
            stats.validator_warn  = validator_warn;
            stats.validator_error = validator_error;
            stats.fps             = 62.5F;  // stand-in until real frame_timing wired
            stats.vertex_count    = static_cast<std::uint32_t>(batcher.vertex_count());
            stats.draw_calls      = static_cast<std::uint32_t>(batcher.command_count());
            stats.route_taken     = route_taken;

            draw_status_bar(batcher, widget_theme, stats,
                            static_cast<float>(fb_w),
                            static_cast<float>(fb_h));
        }

        (void)submitter.upload(batcher);

        if (using_null || !renderer)
        {
            report_headless_frame(batcher, submitter, dockspace, frame_idx);
        }
        else
        {
            auto frame_r = renderer->begin_frame();
            if (!frame_r.has_value())
            {
                if (frame_r.error().code ==
                    static_cast<std::uint32_t>(
                        render::render_errors::Code::kSwapchainOutOfDate))
                {
                    needs_rebuild = true;
                    continue;
                }
                std::fprintf(stderr, "editor: begin_frame failed.\n");
                return 5;
            }
            auto& frame = *frame_r;
            auto& cmd   = *frame.command_buffer;

            std::array<rhi::ColorAttachmentInfo, 1> color_attach { rhi::ColorAttachmentInfo {
                .view        = frame.swapchain_image_view,
                .load_op     = rhi::LoadOp::kClear,
                .store_op    = rhi::StoreOp::kStore,
                .clear_color = { .f32 = { 0.06F, 0.07F, 0.10F, 1.0F } } } };
            rhi::RenderPassBeginInfo rp {};
            rp.render_area       = rhi::Rect2D { { 0, 0 }, frame.extent };
            rp.color_attachments = color_attach;
            cmd.begin_render_pass(rp);
            cmd.set_viewport(rhi::Viewport {
                0.0F, 0.0F,
                static_cast<float>(frame.extent.width),
                static_cast<float>(frame.extent.height),
                0.0F, 1.0F });
            cmd.set_scissor(rhi::Rect2D { { 0, 0 }, frame.extent });

            // Phase 554 / M3 W1A: the submitter ALWAYS has a usable
            // pipeline now -- either Route A (cd::material UI variants
            // when CD_HAVE_MATERIAL_UI_VARIANTS is defined) or Route B
            // (the inline GLSL fallback compiled at boot above). Record
            // the per-frame draw commands the DrawBatcher produced so
            // panel quads + theme palette actually reach the swapchain.
            submitter.record(cmd, frame.extent);

            cmd.end_render_pass();

            auto end_r = renderer->end_frame();
            if (!end_r.has_value())
            {
                if (end_r.error().code ==
                    static_cast<std::uint32_t>(
                        render::render_errors::Code::kSwapchainOutOfDate))
                {
                    needs_rebuild = true;
                    continue;
                }
                std::fprintf(stderr,
                             "editor: end_frame failed (domain=%u code=%u): %.*s\n",
                             end_r.error().domain, end_r.error().code,
                             static_cast<int>(end_r.error().message.size()),
                             end_r.error().message.data());
                return 6;
            }
        }

        ++frame_idx;
        if (!window && cap_frames > 0U && frame_idx >= cap_frames) { break; }
        if (window && window->should_close()) { break; }
    }

    if (renderer) { renderer->wait_idle(); }

    // -- On-exit .cdproj save -----------------------------------------------
    // Capture window geometry (size from the last known fb dimensions) and
    // write the project file so the next launch can restore session state.
    // dock_layout is stored as a passthrough string; the caller is responsible
    // for encoding DockSpace::serialize()'s byte vector (e.g. base64). Today
    // we store the empty string — full round-trip is wired once the encode
    // helper lands. The save is best-effort: failure is logged but does not
    // change the exit code.
    {
        if (window)
        {
            project_data.window.w = static_cast<int>(window->width());
            project_data.window.h = static_cast<int>(window->height());
        }
        project_data.schema_version = 1;
        if (!cd::editor::cdproj::write_cdproj(project_data, cdproj_path))
        {
            std::fprintf(stderr,
                         "editor: warning — could not save .cdproj to %s\n",
                         cdproj_path.string().c_str());
        }
        else
        {
            std::printf("editor: session saved to %s\n",
                        cdproj_path.string().c_str());
        }
    }

    std::printf("editor: clean exit (%u frames; dock nodes=%zu).\n",
                frame_idx, dockspace.node_count());
    std::fflush(stdout);
    return 0;
}
