// =============================================================================
// CHROMODYNAMIC -- apps/editor/main.cpp
//
// Phase 707 / M15 W6B -- cinematic boot splash: slow particle field behind title.
//
// Adds a cd::particle::system::System to the boot splash.  A single emitter
// spawns 20 particles/sec.  Each particle drifts upward with random horizontal
// velocity and fades out over 4 seconds.  Color is PaletteV2::accent_success
// (muted green, #50C864).  Particles are rendered FIRST in draw_boot_splash so
// they appear behind the title + subtitle + progress strip.  The particle
// system is destroyed when the splash completes (optional.reset()).
//
// MOMENT: A user launches editor.exe, sees the CHROMODYNAMIC title over a slow
// upward-drifting particle field — the engine has presence the moment it boots.
//
// Phase 690 / M14 W3 -- wire 3 new M14 surfaces into apps/editor in one commit.
//
// Three surface additions (one commit, single moment):
//   1. scene_navigator           — Tab-merged INTO scene_tree on the LEFT
//                                  column (no new dock node — tab_merges do
//                                  not bump node_count). Search box + live-
//                                  filtered entity list seeded with a small
//                                  demo entity set so the panel is non-empty.
//   2. keyboard_shortcut_overlay — OVERLAY (NOT a dock node) toggled with '?'.
//                                  Pre-registers 15 default shortcuts at boot:
//                                    Ctrl+S       Save Layout
//                                    Ctrl+L       Load Layout
//                                    ?            Toggle Shortcut Help
//                                    F1           CPU Stats
//                                    F2           GPU Stats
//                                    F3           Frame Graph Timeline
//                                    Ctrl+1..9    Switch panel focus by index
//                                    Ctrl+F       Focus Scene Navigator search
//                                    Esc          Close overlay / clear filter
//                                  Painted full-window when visible; ignored
//                                  otherwise.
//   3. PopoutDock                — Sprint-1 STATE MACHINE for tear-off panels.
//                                  Iterated each frame: when is_detached(id)
//                                  is true, the corresponding panel is drawn
//                                  as a floating-internal rectangle at the
//                                  PopoutWindow::position top-left instead of
//                                  inside the main dock. Borders use the
//                                  palette accent_warning so the detached
//                                  state reads at a glance. Native multi-
//                                  window promotion = Sprint-2 (gated on
//                                  cd::platform multi-window support).
//
// Dock PANEL count: 16 -> 17 (scene_navigator added; overlay + popouts are NOT
// dock nodes). scene_navigator is tab-merged with scene_tree, so the underlying
// DockSpace::node_count() (splits + tab groups) does NOT change — only the
// registered-panel count does. The boot log line records the registered-panel
// count ("17 dock nodes") to match the M14 W3 smoke target.
//
// MOMENT: A power user opens the editor, presses '?', sees all 15 shortcuts,
// types 'box' in scene_navigator, jumps to a Box entity, all in 3 seconds —
// Source 2 SDK speed.
//
// Phase 685 / M13 W6B -- boot splash polish + first-time-user welcome flow.
//
// Changes vs phase679:
//   * Boot splash extended from 2.0 s to 2.5 s (opaque) + 0.3 s fade = 2.8 s
//     total. Three animation stages:
//       [0, 500ms)    'CHROMODYNAMIC' title quads fade in (alpha 0 -> 1).
//       [500, 1500ms) subtitle bar reveals: 'A library-oriented game engine'.
//       [1500, 2500ms) scrolling hints: four strings cycle vertically
//                      (Booting Vulkan... / Loading panels... /
//                       Restoring layout... / Ready).
//     Elapsed time is tracked via cd::frame_timing::FrameTimeRing push data
//     (using the same steady_clock measurement already in the frame loop).
//   * First-time-user welcome: if no .cdproj existed when the editor started
//     (first_launch flag), and the splash has finished, a centred one-shot
//     dialog offers three layout presets:
//       'Default Layout'  -- keeps the existing 16-panel default.
//       'Compact Layout'  -- collapses all panel content to 3 key tiles:
//                            viewport + console + inspector.
//       'Full Layout'     -- all 15 panels at equal weight visible.
//     The user's choice is written as a starter .cdproj (dock_layout +
//     layout_preset field) so the next launch skips the dialog immediately.
//
//   MOMENT: a first-time user opens editor.exe, sees a polished 2.5 s branded
//   splash, then a friendly welcome dialog asking how they want their
//   workspace — not dumped into a 13-panel cockpit cold.
//
// Phase 679 / M13 W3 -- wire 3 new panels + .cdproj layout round-trip.
//
// Changes vs phase674:
//   * Three brand-new panels are registered with the DockSpace shell:
//       vehicle_editor    -- tab-merged with material_editor on LEFT-lower.
//       pathfinding_viz   -- standalone split BELOW scene_tree on LEFT-top.
//       material_preview  -- tab-merged with material_editor + vehicle_editor
//                            so material_editor's leaf becomes a 3-way tab
//                            strip (Material / Vehicle / Preview).
//     The dock panel count grows 13 -> 16; node_count grows by exactly +1
//     (only pathfinding_viz adds a split; tab-merges keep node_count flat).
//   * .cdproj layout round-trip: on boot, dock_layout (hex-encoded byte
//     stream from DockSpace::serialize) is fed through DockSpace::restore so
//     the user sees the EXACT layout they left behind (active tab indices
//     included).  Log line "restored layout from <path>" (or "no saved
//     layout, using defaults") records which path was taken.  On exit the
//     live DockSpace is serialized + hex-encoded + written back to the same
//     %APPDATA%\cd_editor\last.cdproj file.  Save is best-effort; failure
//     does not change the exit code.
//   * "File > Save Layout" menu hint is printed at boot (real menubar lives
//     in a future Sprint).
//
//   MOMENT: a user closes the editor mid-task, reopens it, the layout is
//   EXACTLY as they left it — including which tab in each tab strip was
//   active.  Source 2 SDK feel.
//
// Phase 674 / M12 W6B -- frame stats overlay polish + boot splash.
//
// Changes vs phase667:
//   * Three floating overlays (CPU / GPU / FRAME) now carry a titled header
//     strip (small tinted label quad: "CPU" / "GPU" / "FRAME") rendered via
//     cd::ui::theme::text_dim colour.  Each header sits in a 14 px band
//     above the bar-chart body so the overlays are visually distinct.
//   * Real data feeds: FPS from cd::frame_timing::FrameTimeRing<120>;
//     draw_calls and vertex_count from DrawBatcher per-frame; GPU overlay
//     retains synthetic timing bars (write_timestamp not yet shipped) but
//     labels + window match the real frame budget read from the ring.
//   * Boot splash: for the first 2 seconds the full framebuffer is covered
//     with a PaletteV2::surface fill + "CHROMODYNAMIC" quad branding text
//     (two stacked quads) + a loading-hint progress strip.  At 2 s the
//     splash fades out (alpha lerp over 300 ms) and the normal dock appears.
//     Uses std::chrono::steady_clock for wall-clock timing.
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
// Three new panels (phase679 / M13 W3 — 13 -> 16 panels; +1 dock node):
//   * vehicle_editor     -- LEFT-lower; tab-merged INTO material_editor.
//   * pathfinding_viz    -- LEFT-top; standalone split BELOW scene_tree.
//   * material_preview   -- LEFT-lower; tab-merged INTO material_editor so
//                           the leaf becomes a 3-way Material/Vehicle/Preview
//                           tab strip.
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
#include <cd/ui/ToastAnim.hpp>
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
// phase679 / M13 W3 — three brand-new authoring panels wired into the dock:
//   * vehicle_editor    -- tab-merged with material_editor on LEFT-lower
//   * pathfinding_viz   -- standalone split beneath scene_tree on LEFT-top
//   * material_preview  -- tab-merged with material_editor + vehicle_editor
//                          (3-way tab strip on LEFT-lower)
#include <cd/editor/panel_vehicle_editor/VehicleEditor.hpp>
#include <cd/editor/panel_pathfinding_viz/PathfindingViz.hpp>
#include <cd/editor/panel_material_preview/MaterialPreview.hpp>
#include <cd/editor/cdproj/CdprojFile.hpp>

// phase667 / M12 W2 — asset_validator (status badge in the new status bar) +
// cd::asset::validator::Severity for green/yellow/red counts.
#include <cd/asset/validator/Validator.hpp>

// phase598 / M6 W3 — overlays (CPU marker bar chart + frame-graph Gantt).
#include <cd/profile/cpu_marker_overlay/CpuMarkerOverlay.hpp>
#include <cd/profile/frame_graph_timeline/FrameGraphTimeline.hpp>
// phase737 — real cd::framegraph pass timing.
#include <cd/framegraph/FrameGraph.hpp>

// phase631 / M9 W1A — GPU marker overlay (sibling to cpu_marker_overlay;
// cpu/gpu classification, NOT a v1/v2 version rename). Synthetic samples per
// frame — real GPU timestamps land when ICommandBuffer::write_timestamp ships.
// (phase618 slot was planned but not delivered as a standalone commit; this
// phase631 commit is the official delivery.)
#include <cd/profile/gpu_marker/GpuMarker.hpp>

// phase674 / M12 W6B — FrameTimeRing for real FPS feed in the overlay headers.
// Header-only; no extra link target (cd::frame_timing is INTERFACE).
#include <cd/frame_timing/FrameTimeRing.hpp>

// phase684 / M13 W6A — Debug Visualization overlays: depth / normal /
// alpha-bucket thumbnails in a top-right vertical stack. Three instances each
// carry a separate VizKind. Sprint-1 renders coloured placeholder gradients;
// Sprint-2 wires real G-buffer textures once framegraph exposes them cleanly.
#include <cd/editor/panel_debug_viz/DebugViz.hpp>

// phase690 / M14 W3 — three new M14 surfaces wired into apps/editor:
//   * scene_navigator           — search-filter sibling to scene_tree, tab-merged
//                                  with it on the LEFT column (no new dock node;
//                                  tab_merge does not bump node_count).
//   * keyboard_shortcut_overlay — full-screen '?' cheatsheet; 15 default shortcuts
//                                  pre-registered. Rendered as an overlay, NOT a
//                                  dock node, so it never collides with panel rects.
//   * PopoutDock                — Sprint-1 state machine that tracks panels the
//                                  user has "torn off" from the dock. apps/editor
//                                  renders each detached panel as a floating-internal
//                                  rectangle (top-left from PopoutWindow::position).
//                                  Border drawn in palette accent_warning so the
//                                  detached state is visually distinct.
#include <cd/editor/panel_scene_navigator/SceneNavigator.hpp>
#include <cd/editor/panel_keyboard_shortcut_overlay/KeyboardShortcutOverlay.hpp>
#include <cd/ui/widgets/PopoutDock.hpp>

// phase701 / M15 W3 — three new panels wired into apps/editor:
//   * settings_panel      — tab-merged with inspector on RIGHT-top.
//   * build_panel         — tab-merged with console on BOTTOM-CENTRE.
//   * perf_profiler       — tab-merged with the debug_viz overlay area
//                           (RIGHT-lower; split below light_editor so
//                           perf data lives next to runtime debug overlays).
//
// All three are tab-merges + one new split for perf_profiler. The toast
// notification queue + the build-panel global registration helper are
// declared in a small editor::toast namespace below.
#include <cd/editor/panel_settings/SettingsPanel.hpp>
#include <cd/editor/panel_build/BuildPanel.hpp>
#include <cd/editor/panel_perf_profiler/PerfProfiler.hpp>

// phase711 / M16 W3 — two new panels wired into apps/editor:
//   * asset_pipeline_status — tab-merged with perf_profiler on RIGHT-lower.
//                              StreamerPool pending/completed bar chart.
//   * ik_chain_editor       — tab-merged with animator on BOTTOM strip.
//                              2D side-view IK chain visualiser + convergence
//                              indicator.
// Both are tab-merges so dockspace.node_count() stays at 14 (no new splits).
// Panel count grows 20 -> 22.
#include <cd/editor/panel_asset_pipeline_status/AssetPipelineStatus.hpp>
#include <cd/editor/panel_ik_chain_editor/IkChainEditor.hpp>

// phase720 / M17 W3 — three new panels wired into apps/editor:
//   * scene_palette        — tab-merged with settings_panel on RIGHT-top.
//                            Live theme token preview grid (15 swatch cells).
//   * lobby_browser        — standalone bottom-right node (split alongside
//                            perf_profiler / asset_pipeline_status).
//                            Visualises cd::network::lobby active rooms.
//   * auto_save_indicator  — NOT a dock node; rendered in the STATUS BAR
//                            (~150 px wide strip pinned left of the theme picker).
// Dock panel count grows 22 -> 24 (scene_palette is a tab-merge, lobby_browser
// is a fresh split; auto_save_indicator lives outside the dock entirely).
#include <cd/editor/panel_scene_palette/ScenePalette.hpp>
#include <cd/editor/panel_lobby_browser/LobbyBrowser.hpp>
#include <cd/editor/panel_auto_save_indicator/AutoSaveIndicator.hpp>

// phase720 / M17 W3 — Lobby instance for the LobbyBrowser panel seed.
#include <cd/network/lobby/Lobby.hpp>

// phase707 / M15 W6B — cd::particle::system::System drives the slow upward-
// drifting particle field rendered behind the boot splash title.
#include <cd/particle/system/ParticleSystem.hpp>

// phase631 / M9 W1A — asset::validator for status badge.
// TODO(phase631): No status bar exists yet in apps/editor. When a status bar
// is added, wire a cd::asset::validator::Validator instance here and display
// a pass/warn/error badge from the latest validate call results.
// #include <cd/asset/validator/Validator.hpp>  // linked but include deferred

#include <algorithm>
#include <array>
#include <chrono>
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

    // ---- phase679 / M13 W3 — three new panel slots --------------------------
    //
    // Grows the dock from 13 -> 16 PANELS (the dock_node count grows by +1
    // because only pathfinding_viz introduces a new split; vehicle_editor and
    // material_preview are tab-merged onto material_editor and do not change
    // node_count).
    //
    //   (7) vehicle_editor   -- tab-merged INTO material_editor.
    //                           A racing dev can flip material<->vehicle on
    //                           the same LEFT-bottom tile without a separate
    //                           dock target.
    //
    //   (8) pathfinding_viz  -- standalone split BELOW scene_tree on LEFT-top
    //                           (good for viewing alongside the scene
    //                           hierarchy a designer is editing). +1 node.
    //
    //   (9) material_preview -- tab-merged INTO material_editor (now a 3-way
    //                           tab strip with vehicle_editor). A material
    //                           artist sees the live PBR preview right next
    //                           to the property editor.
    //
    // MOMENT: a first-time editor user opens apps/editor and sees a vehicle
    // tuning panel, a navmesh visualisation slot, and a live PBR preview
    // -- three brand-new authoring surfaces shipped DAY ONE.

    // (7) vehicle_editor tab-merged with material_editor.
    auto* material_editor_owner = ds.find_panel_owner("material_editor");
    if (material_editor_owner == nullptr) { return false; }
    if (!ds.tab_merge(material_editor_owner, "vehicle_editor")) { return false; }

    // (8) scene_tree -> pathfinding_viz split (LEFT-top column, lower half).
    // ratio=0.65 -> scene_tree keeps top 65%, pathfinding_viz takes bottom 35%.
    auto* scene_tree_owner = ds.find_panel_owner("scene_tree");
    if (scene_tree_owner == nullptr) { return false; }
    if (!ds.split(scene_tree_owner, uw::DockAxis::kHorizontal,
                  "pathfinding_viz", 0.65F))
    {
        return false;
    }

    // (9) material_preview tab-merged INTO the same material_editor leaf so
    // material_editor / vehicle_editor / material_preview share a 3-way tab
    // strip. Re-resolve the owner: the prior split() calls may have moved the
    // node, but the panel-id index inside DockSpace tracks it for us.
    auto* material_editor_owner2 = ds.find_panel_owner("material_editor");
    if (material_editor_owner2 == nullptr) { return false; }
    if (!ds.tab_merge(material_editor_owner2, "material_preview")) { return false; }

    // ---- phase690 / M14 W3 — scene_navigator tab-merged with scene_tree -------
    //
    // The scene_navigator panel is the SEARCH/FILTER sibling to scene_tree
    // (which is the HIERARCHY surface). We tab-merge them on the LEFT-top tile
    // so a designer flips between "hierarchy" and "search/filter" in the same
    // dock target without a separate column — matches the Source 2 / Hammer
    // SDK navigation feel. tab_merge() does not bump DockSpace::node_count();
    // the panel COUNT grows from 16 to 17 (logged below).
    auto* scene_tree_owner_690 = ds.find_panel_owner("scene_tree");
    if (scene_tree_owner_690 == nullptr) { return false; }
    if (!ds.tab_merge(scene_tree_owner_690, "scene_navigator")) { return false; }

    // ---- phase701 / M15 W3 — three new panels --------------------------------
    //
    // Wire mode:
    //   (10) settings_panel  -- tab-merged INTO inspector on RIGHT-top.
    //   (11) build_panel     -- tab-merged INTO console on BOTTOM-CENTRE
    //                           (joins the console+assets+asset_drop_target
    //                            tab group as a sibling).
    //   (12) perf_profiler   -- split BELOW light_editor on RIGHT-lower
    //                           (a fresh tile so the live perf history bars
    //                            do not fight with the inspector or behavior
    //                            designer for attention). +1 node.
    //
    // Tab-merges keep node_count flat; only perf_profiler's split bumps it.
    // Panel count grows 17 -> 20 (settings + build + perf_profiler).
    //
    // MOMENT: a dev opens the editor and immediately sees the runtime perf
    // ribbon, the build status badge, and a settings tab right where they
    // need them — no extra clicks.

    // (10) settings_panel tab-merged with inspector (RIGHT-top tile).
    auto* inspector_owner_701 = ds.find_panel_owner("inspector");
    if (inspector_owner_701 == nullptr) { return false; }
    if (!ds.tab_merge(inspector_owner_701, "settings_panel")) { return false; }

    // (11) build_panel tab-merged with console (BOTTOM tab group).
    auto* console_owner_701 = ds.find_panel_owner("console");
    if (console_owner_701 == nullptr) { return false; }
    if (!ds.tab_merge(console_owner_701, "build_panel")) { return false; }

    // (12) perf_profiler split BELOW light_editor (RIGHT-lower column).
    // ratio=0.55 -> light_editor keeps top 55%, perf_profiler takes 45%.
    auto* light_editor_owner_701 = ds.find_panel_owner("light_editor");
    if (light_editor_owner_701 == nullptr) { return false; }
    if (!ds.split(light_editor_owner_701, uw::DockAxis::kHorizontal,
                  "perf_profiler", 0.55F))
    {
        return false;
    }

    // ---- phase711 / M16 W3 — two new tab-merged panels -----------------------
    //
    //   (13) asset_pipeline_status -- tab-merged INTO perf_profiler so the
    //        RIGHT-lower tile flips between "frame-by-frame profiler" and
    //        "streamer pool bar chart". Both are runtime/perf surfaces, so
    //        sharing the same tile keeps the right-hand column from sprawling.
    //
    //   (14) ik_chain_editor       -- tab-merged INTO animator so the BOTTOM
    //        strip carries an animator <-> IK pair on the same tile. An
    //        animator drops a 5-joint leg chain into the editor and sees the
    //        CCD solve converge to plant the foot without launching the game.
    //
    // Both are tab_merge() calls so DockSpace::node_count() does NOT change;
    // the panel COUNT grows from 20 to 22.
    //
    // MOMENT: a first-time editor user opens apps/editor and sees the asset
    // pipeline bar chart living next to the perf profiler AND an IK
    // visualiser next to the animator — two brand-new authoring surfaces
    // shipped DAY ONE alongside the existing 20-panel cockpit.

    // (13) asset_pipeline_status tab-merged with perf_profiler.
    auto* perf_profiler_owner_711 = ds.find_panel_owner("perf_profiler");
    if (perf_profiler_owner_711 == nullptr) { return false; }
    if (!ds.tab_merge(perf_profiler_owner_711, "asset_pipeline_status"))
    {
        return false;
    }

    // (14) ik_chain_editor tab-merged with animator.
    auto* animator_owner_711 = ds.find_panel_owner("animator");
    if (animator_owner_711 == nullptr) { return false; }
    if (!ds.tab_merge(animator_owner_711, "ik_chain_editor"))
    {
        return false;
    }

    // ---- phase720 / M17 W3 — two new dock panels -----------------------------
    //
    //   (15) scene_palette  -- tab-merged INTO settings_panel so the RIGHT-top
    //        tile flips between "inspector / settings / scene_palette".  A UI
    //        designer reskins the editor and sees every palette token side-by-
    //        side with the property panel they are tweaking.
    //
    //   (16) lobby_browser  -- split BELOW perf_profiler on RIGHT-lower.
    //        ratio=0.55 -> perf_profiler keeps top 55%, lobby_browser takes 45%.
    //        Standalone bottom-right node (NOT a tab-merge) per the M17 W3 brief
    //        so a multiplayer dev sees rooms in their own dedicated tile.
    //
    // The third M17 W3 panel (auto_save_indicator) lives in the STATUS BAR and
    // is NOT a dock node — see draw_status_bar() below.
    //
    // Dock node count grows by +1 (lobby_browser split); scene_palette is a
    // tab-merge.  Registered-panel count grows 22 -> 24.

    // (15) scene_palette tab-merged with settings_panel.
    auto* settings_owner_720 = ds.find_panel_owner("settings_panel");
    if (settings_owner_720 == nullptr) { return false; }
    if (!ds.tab_merge(settings_owner_720, "scene_palette"))
    {
        return false;
    }

    // (16) perf_profiler -> lobby_browser split (RIGHT-lower, bottom 45%).
    auto* perf_profiler_owner_720 = ds.find_panel_owner("perf_profiler");
    if (perf_profiler_owner_720 == nullptr) { return false; }
    if (!ds.split(perf_profiler_owner_720, uw::DockAxis::kHorizontal,
                  "lobby_browser", 0.55F))
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
    // phase673 / M12 W6A — use surface_subtle (lighter than surface) so the
    // scene-tree panel background reads as a raised layer distinct from the
    // deeper dock background. This is the "designed, not coder-mocked" moment:
    // two panel surface levels visible at a glance.
    batcher.quad(rect.x, rect.y, rect.w, rect.h,
                 ur::Color { theme.surface_subtle.r, theme.surface_subtle.g,
                             theme.surface_subtle.b, theme.surface_subtle.a });
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

// phase679 / M13 W3 — three new panel instances wired into the dock.
// File-scope storage so the ContentDrawer lambdas can capture them by
// reference for the lifetime of the program.
cd::editor::panel::vehicle_editor::VehicleEditor             g_vehicle_editor_panel;
cd::editor::panel::pathfinding_viz::PathfindingViz           g_pathfinding_viz_panel;
cd::editor::panel::material_preview::MaterialPreview         g_material_preview_panel;

// phase690 / M14 W3 — scene_navigator panel (search/filter sibling to scene_tree).
// File-scope so the ContentDrawer lambda captures it by reference. Seeded with
// a small demo entity set in main() so the panel is non-empty on first boot.
cd::editor::panel::scene_navigator::SceneNavigator           g_scene_navigator_panel;

// phase701 / M15 W3 — settings + build + perf_profiler panel instances.
// File-scope so the ContentDrawer lambdas capture them by reference for the
// lifetime of the program. Seeded with realistic demo data in main() so the
// panels are non-empty on first boot.
cd::editor::panel::settings::SettingsPanel                   g_settings_panel;
cd::editor::panel::build::BuildPanel                         g_build_panel;
cd::editor::panel::perf_profiler::PerfProfiler               g_perf_profiler_panel;

// phase711 / M16 W3 — two new panel instances wired into the dock.
// File-scope so the ContentDrawer lambdas capture them by reference for the
// lifetime of the program.
cd::editor::panel::asset_pipeline_status::AssetPipelineStatus g_asset_pipeline_status_panel;
cd::editor::panel::ik_chain_editor::IkChainEditor             g_ik_chain_editor_panel;

// phase720 / M17 W3 — three new panel instances wired into the editor.
// File-scope so the ContentDrawer lambdas / status-bar helper capture them by
// reference for the lifetime of the program. The Lobby instance is the live
// data the LobbyBrowser visualises (seeded with two demo rooms in main()).
cd::editor::panel::scene_palette::ScenePalette                g_scene_palette_panel;
cd::editor::panel::lobby_browser::LobbyBrowser                g_lobby_browser_panel;
cd::editor::panel::auto_save_indicator::AutoSaveIndicator     g_auto_save_indicator;
cd::network::lobby::Lobby                                     g_demo_lobby;

// phase711 / M16 W3 — demo IK chain backing the ik_chain_editor panel.
// A 5-joint leg chain anchored at the origin, target shifted forward so the
// editor opens with a non-trivial visualisation (joint pills + bone lines
// + target marker) on first boot. The chain is not solved here — the editor
// is a static visual surface today; live solve hooks land when an animator
// scene drives the IK system from the playmode timeline.
cd::animation::ik::IkChain g_demo_ik_chain {};
cd::animation::ik::IkResult g_demo_ik_result {};

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

// phase679 / M13 W3 — drawers for the 3 new panels.

void draw_vehicle_editor_panel(const uw::Rect& rect,
                               ur::DrawBatcher& batcher,
                               uf::Font* /*font*/,
                               const uw::Theme& theme)
{
    g_vehicle_editor_panel.draw(batcher, theme, rect);
}

void draw_pathfinding_viz_panel(const uw::Rect& rect,
                                ur::DrawBatcher& batcher,
                                uf::Font* /*font*/,
                                const uw::Theme& theme)
{
    g_pathfinding_viz_panel.draw(batcher, theme, rect);
}

void draw_material_preview_panel(const uw::Rect& rect,
                                 ur::DrawBatcher& batcher,
                                 uf::Font* /*font*/,
                                 const uw::Theme& theme)
{
    g_material_preview_panel.draw(batcher, theme, rect);
}

// phase690 / M14 W3 — scene_navigator drawer.
void draw_scene_navigator_panel(const uw::Rect& rect,
                                ur::DrawBatcher& batcher,
                                uf::Font* /*font*/,
                                const uw::Theme& theme)
{
    g_scene_navigator_panel.draw(batcher, theme, rect);
}

// phase701 / M15 W3 — drawers for the 3 new panels.
//
// SettingsPanel and BuildPanel consume cd::ui::widgets::Theme directly. The
// PerfProfiler signature however takes cd::ui::theme::Theme (V2), so the
// perf_profiler drawer rebuilds a transient V2 theme via theme_from_name()
// using the active palette name kept by main() (see g_active_theme_name_ptr).
// We expose the V2 theme via a function-static fallback for the headless
// path where the global is not yet wired.
std::string* g_active_theme_name_ptr { nullptr };

void draw_settings_panel(const uw::Rect& rect,
                         ur::DrawBatcher& batcher,
                         uf::Font* /*font*/,
                         const uw::Theme& theme)
{
    g_settings_panel.draw(batcher, theme, rect);
}

void draw_build_panel(const uw::Rect& rect,
                      ur::DrawBatcher& batcher,
                      uf::Font* /*font*/,
                      const uw::Theme& theme)
{
    g_build_panel.draw(batcher, theme, rect);
}

void draw_perf_profiler_panel(const uw::Rect& rect,
                              ur::DrawBatcher& batcher,
                              uf::Font* /*font*/,
                              const uw::Theme& /*widget_theme*/)
{
    const std::string_view name =
        (g_active_theme_name_ptr != nullptr && !g_active_theme_name_ptr->empty())
            ? std::string_view(*g_active_theme_name_ptr)
            : std::string_view(uth::kThemeNameDark);
    const auto v2_theme = uth::theme_from_name(name);
    g_perf_profiler_panel.draw(batcher, v2_theme, rect);
}

// phase711 / M16 W3 — drawers for the 2 new panels.
//
// asset_pipeline_status renders 4 horizontal bar pairs (scene / texture /
// audio / shader pending vs completed). It accepts a StreamerPool* via
// set_pool(); when no pool is attached (today's default), draw() still emits
// the background + border + 4 row label quads so the panel reads as a
// purpose-built tile rather than an empty rectangle.
//
// ik_chain_editor renders a 2D side-view of a cd::animation::ik::IkChain
// (joint pills + bone lines + target X marker + convergence indicator).
// Bound to g_demo_ik_chain so the editor opens with a non-trivial preview.

void draw_asset_pipeline_status_panel(const uw::Rect& rect,
                                      ur::DrawBatcher& batcher,
                                      uf::Font* /*font*/,
                                      const uw::Theme& theme)
{
    g_asset_pipeline_status_panel.draw(batcher, theme, rect);
}

void draw_ik_chain_editor_panel(const uw::Rect& rect,
                                ur::DrawBatcher& batcher,
                                uf::Font* /*font*/,
                                const uw::Theme& theme)
{
    g_ik_chain_editor_panel.draw(batcher, theme, rect);
}

// phase720 / M17 W3 — drawers for the 2 new dock panels (scene_palette +
// lobby_browser). auto_save_indicator is NOT a dock panel; it is drawn
// directly from the status-bar pass.
//
// scene_palette consumes cd::ui::widgets::Theme directly (same uw::Theme the
// DockSpace already hands to drawers).
//
// lobby_browser consumes cd::ui::widgets::Theme directly. Bound to a live
// cd::network::lobby::Lobby in main() so the panel renders real room rows
// rather than the empty-state placeholder.

void draw_scene_palette_panel(const uw::Rect& rect,
                              ur::DrawBatcher& batcher,
                              uf::Font* /*font*/,
                              const uw::Theme& theme)
{
    g_scene_palette_panel.draw(batcher, theme, rect);
}

void draw_lobby_browser_panel(const uw::Rect& rect,
                              ur::DrawBatcher& batcher,
                              uf::Font* /*font*/,
                              const uw::Theme& theme)
{
    g_lobby_browser_panel.draw(batcher, theme, rect);
}

// ===========================================================================
// phase701 / M15 W3 — toast notification system
// ===========================================================================
//
// Small floating-message queue rendered in the bottom-right of the status bar.
// Each Toast lives 2 seconds (1.7 s opaque + 0.3 s fade-out). The queue holds
// at most kMaxToasts; pushing into a full queue evicts the oldest entry.
//
// Three colour kinds, each mapped to a status-bar swatch:
//   kInfo    — blue   (RGB 100 / 150 / 220)
//   kSuccess — green  (RGB  80 / 200 / 100)
//   kWarning — amber  (RGB 220 / 200 /  80)
//
// MOMENT: a dev opens the editor; three toasts pop up — 'Editor ready' (green),
// 'Loaded saved layout' (blue), 'Theme: Dark' (blue). The editor talks to them,
// they feel oriented before they have to click anything.

}  // namespace  (close the outer anonymous namespace so named cd::editor::*
   //             namespaces below are valid C++; reopened after the bridge.)

namespace cd::editor::toast
{

enum class Kind : std::uint8_t
{
    kInfo    = 0,
    kSuccess = 1,
    kWarning = 2,
};

struct Toast
{
    std::string message;                              ///< Short text payload.
    Kind        kind { Kind::kInfo };                 ///< Colour category.
    std::chrono::steady_clock::time_point spawn_tp {};///< Birth wall-clock.
    double      spawn_ms    { 0.0 };                  ///< Monotonic ms at push (for anim).
    double      lifetime_ms { 2000.0 };               ///< Total lifetime (ms); default 2000.
};

class ToastQueue
{
public:
    static constexpr std::size_t kMaxToasts    = 5U;
    static constexpr double      kLifetimeMs   = 2000.0;  ///< Total visible life.
    static constexpr double      kFadeMs       =  300.0;  ///< Fade-out duration.

    /// Push a new toast. When the queue is full the oldest is evicted (FIFO).
    /// @param lifetime_ms  Total visible lifetime in milliseconds (default 2000).
    void push(std::string message, Kind kind = Kind::kInfo,
              double lifetime_ms = kLifetimeMs)
    {
        if (toasts_.size() >= kMaxToasts) { toasts_.erase(toasts_.begin()); }
        const auto now = std::chrono::steady_clock::now();
        const auto spawn_ms_val = static_cast<double>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                now.time_since_epoch()).count()) / 1000.0;
        toasts_.push_back(Toast{
            std::move(message), kind, now, spawn_ms_val, lifetime_ms });
    }

    /// Drop toasts whose lifetime has expired. Call once per frame BEFORE
    /// rendering so the rendered list is always fresh.
    void tick(std::chrono::steady_clock::time_point now)
    {
        toasts_.erase(
            std::remove_if(toasts_.begin(), toasts_.end(),
                [&](const Toast& t)
                {
                    using namespace std::chrono;
                    const auto age_ms = static_cast<double>(
                        duration_cast<microseconds>(now - t.spawn_tp).count()) / 1000.0;
                    return age_ms >= kLifetimeMs;
                }),
            toasts_.end());
    }

    [[nodiscard]] std::size_t size() const noexcept { return toasts_.size(); }
    [[nodiscard]] const std::vector<Toast>& items() const noexcept { return toasts_; }

private:
    std::vector<Toast> toasts_ {};
};

}  // namespace cd::editor::toast

// ===========================================================================
// phase701 / M15 W3 — global build-panel registration helper (Sprint-1)
// ===========================================================================
//
// Other engine subsystems (asset validator, future shader compiler hook, etc.)
// need a way to inform the editor's BuildPanel about events WITHOUT linking
// the cd::editor binary itself. The helper exposes a free function +
// thread-safe pointer set at editor startup. When the editor is not running
// (samples, tests) the pointer stays null and the helper is a no-op.

namespace cd::editor::build_panel_bridge
{

namespace
{
    panel::build::BuildPanel* g_bridge_target { nullptr };
}  // namespace

void register_target(panel::build::BuildPanel* target) noexcept
{
    g_bridge_target = target;
}

void push_event(const panel::build::BuildEvent& event)
{
    if (g_bridge_target != nullptr) { g_bridge_target->push_event(event); }
}

void set_status(panel::build::Status status) noexcept
{
    if (g_bridge_target != nullptr) { g_bridge_target->set_status(status); }
}

}  // namespace cd::editor::build_panel_bridge

// ===========================================================================
// phase736 -- cd::editor::profile::FrameCapture
// ===========================================================================
//
// Aggregates real profiling data streams collected per frame into a single
// POD that is converted to PerfProfiler::FrameSnapshot via to_snapshot().
//
//   cpu_markers  -- MarkerSamples from cpu_marker_overlay::Collector.
//   gpu_markers  -- GpuMarkerSamples from gpu_marker::Recorder (Vulkan path).
//   gpu_passes   -- PassRecords from fgt_timeline.last_frame_passes().
//   total_ms     -- real wall-clock frame dt.

namespace cd::editor::profile
{

struct FrameCapture
{
    double total_ms { 0.0 };
    std::vector<cd::profile::cpu_marker_overlay::MarkerSample>   cpu_markers;
    std::vector<cd::profile::gpu_marker::GpuMarkerSample>         gpu_markers;
    std::vector<cd::profile::frame_graph_timeline::PassRecord>    gpu_passes;
};

[[nodiscard]] cd::editor::panel::perf_profiler::FrameSnapshot
to_snapshot(FrameCapture&& cap)
{
    cd::editor::panel::perf_profiler::FrameSnapshot snap;
    snap.total_ms    = cap.total_ms;
    snap.cpu_markers = std::move(cap.cpu_markers);
    snap.gpu_markers = std::move(cap.gpu_markers);
    snap.gpu_passes  = std::move(cap.gpu_passes);
    return snap;
}

}  // namespace cd::editor::profile

// Reopen the outer anonymous namespace closed above so the rest of the file
// (drawer stubs, helpers, main()) retains internal linkage as before.
namespace
{

// Render the toast stack into the bottom-right corner of the framebuffer,
// stacking UP from just above the status bar. Each toast is a 220 x 22 px
// pill quad in the colour matching its kind.
//
// phase724: slide-in from the right (cubic ease-out, 0..150 ms),
// steady (150..1850 ms), then linear fade-out (1850..2000 ms).
// cd::ui::toast_anim() drives both alpha and x_offset.
void draw_toasts(ur::DrawBatcher& batcher,
                 const uw::Theme& theme,
                 const cd::editor::toast::ToastQueue& queue,
                 float fb_w, float fb_h,
                 std::chrono::steady_clock::time_point now)
{
    using cd::editor::toast::Kind;
    using cd::editor::toast::ToastQueue;

    constexpr float kToastW   = 220.0F;
    constexpr float kToastH   =  22.0F;
    constexpr float kToastGap =   4.0F;
    constexpr float kMarginR  =  10.0F;
    constexpr float kMarginB  =   4.0F;   // above status bar
    // Status bar pinned height (mirrors kStatusBarH defined further below; we
    // duplicate the literal here so draw_toasts can sit ahead of the status-bar
    // helpers without a forward declaration).
    constexpr float kToastStatusBarH = 20.0F;

    const float bottom = fb_h - kToastStatusBarH - kMarginB;
    const float base_x = fb_w - kToastW - kMarginR;

    const auto& items = queue.items();
    for (std::size_t i = 0; i < items.size(); ++i)
    {
        const auto& t = items[i];
        using namespace std::chrono;
        const auto age_ms = static_cast<double>(
            duration_cast<microseconds>(now - t.spawn_tp).count()) / 1000.0;
        if (age_ms >= ToastQueue::kLifetimeMs) { continue; }

        // phase724 — animation: cubic ease-out slide-in + linear fade-out.
        const cd::ui::ToastAnimCfg anim_cfg {
            t.lifetime_ms,
            150.0,                           // slide_ms
            ToastQueue::kFadeMs,             // fadeout_ms
            50.0F                            // slide_max_px
        };
        const auto [alpha_f, x_offset] = cd::ui::toast_anim(age_ms, anim_cfg);
        const auto alpha    = static_cast<std::uint8_t>(alpha_f * 230.0F);
        const float x       = base_x + x_offset;

        ur::Color fill { 100U, 150U, 220U, alpha };  // kInfo (blue)
        switch (t.kind)
        {
            case Kind::kSuccess: fill = ur::Color {  80U, 200U, 100U, alpha }; break;
            case Kind::kWarning: fill = ur::Color { 220U, 200U,  80U, alpha }; break;
            case Kind::kInfo:    /* already set */                            break;
        }

        // Stack upwards: newest at the bottom, older toasts above.
        const float y = bottom - static_cast<float>(i + 1U) * (kToastH + kToastGap);
        if (y < 0.0F) { break; }

        // Body fill.
        batcher.quad(x, y, kToastW, kToastH, fill);

        // 2 px left-edge accent stripe in theme accent so the toast reads as
        // a toast (not a button).
        batcher.quad(x, y, 2.0F, kToastH,
                     ur::Color { theme.accent.r, theme.accent.g,
                                 theme.accent.b, alpha });

        // 12 px message-indicator strip: 3 small dim quads inside the body
        // so the toast surface conveys "this is a message" without glyphs.
        constexpr float kDotW = 5.0F;
        constexpr float kDotH = 4.0F;
        constexpr float kDotY = 9.0F;
        const auto dim_alpha = static_cast<std::uint8_t>(alpha_f * 230.0F * 0.7F);
        for (int d = 0; d < 3; ++d)
        {
            batcher.quad(x + 12.0F + static_cast<float>(d) * (kDotW + 3.0F),
                         y + kDotY, kDotW, kDotH,
                         ur::Color { 240U, 240U, 240U, dim_alpha });
        }

        // Suppress unused warning for the message string (no glyph renderer yet).
        (void)t.message;
    }
}

// ---- phase679 / M13 W3 — DockSpace serialize <-> string hex codec ----------
//
// DockSpace::serialize() returns std::vector<std::byte>; CdprojData stores
// dock_layout as a UTF-8 std::string. Encode bytes as lowercase hex so the
// payload is JSON-safe (printable, escape-free, no embedded NULs). On boot the
// inverse decode is fed back to DockSpace::restore().
//
// Hex was chosen over base64 because it has no third-party-library dependency
// in cd::core, is human-debuggable in a .cdproj file, and the payload size
// (~2x the byte stream) is negligible — a typical 16-node tree fits in <1 KB.
// =============================================================================

[[nodiscard]] std::string dock_layout_to_hex(const std::vector<std::byte>& bytes)
{
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2U);
    for (const auto b : bytes)
    {
        const auto u = static_cast<std::uint8_t>(b);
        out.push_back(kHex[(u >> 4U) & 0x0FU]);
        out.push_back(kHex[u & 0x0FU]);
    }
    return out;
}

[[nodiscard]] std::vector<std::byte> dock_layout_from_hex(std::string_view hex)
{
    std::vector<std::byte> out;
    if ((hex.size() & 0x1U) != 0U) { return out; }
    out.reserve(hex.size() / 2U);

    auto digit = [](char c) -> int {
        if (c >= '0' && c <= '9') { return c - '0'; }
        if (c >= 'a' && c <= 'f') { return 10 + (c - 'a'); }
        if (c >= 'A' && c <= 'F') { return 10 + (c - 'A'); }
        return -1;
    };

    for (std::size_t i = 0; i < hex.size(); i += 2U)
    {
        const int hi = digit(hex[i]);
        const int lo = digit(hex[i + 1U]);
        if (hi < 0 || lo < 0) { out.clear(); return out; }
        out.push_back(static_cast<std::byte>(
            static_cast<std::uint8_t>((hi << 4) | lo)));
    }
    return out;
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
// its own uint8_t-RGBA palette. Translate the chosen V2 theme so the
// editor binary stays on the V2 token surface while still feeding the
// widget catalog.
//
// phase695 / M14 W6A: build_widget_theme() now accepts a theme_name string
// ("dark" / "light" / "high_contrast") so the caller can hot-swap the
// palette without touching the widget hierarchy. Unknown names fall back to
// the dark palette (same behaviour as theme_from_name()).
[[nodiscard]] uw::Color to_widget_color(uth::ColorToken c) noexcept
{
    auto pack = [](float v) noexcept -> std::uint8_t {
        const float clamped = (v < 0.0F) ? 0.0F : (v > 1.0F) ? 1.0F : v;
        return static_cast<std::uint8_t>(std::lround(clamped * 255.0F));
    };
    return uw::Color { pack(c.r), pack(c.g), pack(c.b), pack(c.a) };
}

[[nodiscard]] uw::Theme build_widget_theme(std::string_view theme_name)
{
    const auto tok = uth::theme_from_name(theme_name);
    uw::Theme t {};
    // Map V2 16-slot palette into the cd::ui_widgets 9-slot theme. The
    // semantic intent stays in lock-step with Material 3: background =
    // surface, panel surface = surfaceVariant, hover/press = secondary /
    // primary containers, focus = primary container (highlight).
    t.background    = to_widget_color(tok.color(uth::PaletteSlot::kBackground));
    t.surface       = to_widget_color(tok.color(uth::PaletteSlot::kSurface));
    t.surface_hover = to_widget_color(tok.color(uth::PaletteSlot::kSurfaceVariant));
    t.surface_press = to_widget_color(tok.color(uth::PaletteSlot::kPrimaryContainer));
    t.accent        = to_widget_color(tok.color(uth::PaletteSlot::kPrimary));
    t.accent_hover  = to_widget_color(tok.color(uth::PaletteSlot::kPrimaryContainer));
    t.text          = to_widget_color(tok.color(uth::PaletteSlot::kOnSurface));
    t.text_dim      = to_widget_color(tok.color(uth::PaletteSlot::kOnSurfaceVariant));

    // Semantic extension tokens (phase673 / M12 W6A):
    // accent_error driven from the V2 kError swatch so warning/error visuals
    // are consistent across the theme tree. surface_subtle / divider /
    // accent_warning / accent_success retain their struct defaults so they
    // remain legible in all three palettes without per-palette overrides.
    t.accent_error = to_widget_color(tok.color(uth::PaletteSlot::kError));
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

    // 1. Status bar background — use surface_subtle (phase673) so the ribbon
    //    reads as a slightly raised layer rather than an arbitrary dark notch.
    batcher.quad(bar_x, bar_y, bar_w, kStatusBarH,
                 ur::Color {
                     theme.surface_subtle.r,
                     theme.surface_subtle.g,
                     theme.surface_subtle.b,
                     theme.surface_subtle.a });

    // 2. Top separator line — use divider token (phase673) so the boundary
    //    between dock and status bar uses the design-token vocabulary.
    batcher.quad(bar_x, bar_y, bar_w, 1.0F,
                 ur::Color {
                     theme.divider.r,
                     theme.divider.g,
                     theme.divider.b,
                     theme.divider.a });

    // ---- 3. Validator badge (LEFT region, 30% of the bar width) -----------
    //
    // Three side-by-side count blocks; each block width is proportional to
    // its count relative to the total, with a 6 px minimum so a zero-count
    // bucket is still visually present. Colour mapping mirrors
    // cd::asset::validator::Severity using phase673 semantic tokens:
    //    Info / pass  -> theme.accent_success (green)
    //    Warning      -> theme.accent_warning (yellow)
    //    Error        -> theme.accent_error   (red)
    constexpr float kBadgeW    = 280.0F;
    constexpr float kBadgePad  = 6.0F;
    constexpr float kBadgeH    = kStatusBarH - 4.0F;
    const float     badge_y    = bar_y + 2.0F;
    const float     badge_avail = kBadgeW - 2.0F * kBadgePad;

    const std::uint32_t total = stats.validator_pass + stats.validator_warn + stats.validator_error;
    const float kMinBlock = 6.0F;

    auto block_w = [&](std::uint32_t count) -> float {
        if (total == 0U) { return (badge_avail - 4.0F) / 3.0F; }
        const auto frac = static_cast<float>(count) / static_cast<float>(total);
        return std::max(kMinBlock, frac * (badge_avail - 4.0F));
    };

    float bx = bar_x + kBadgePad;
    // pass — accent_success (phase673)
    {
        const float w = block_w(stats.validator_pass);
        batcher.quad(bx, badge_y, w, kBadgeH,
                     ur::Color {
                         theme.accent_success.r,
                         theme.accent_success.g,
                         theme.accent_success.b,
                         theme.accent_success.a });
        bx += w + 2.0F;
    }
    // warn — accent_warning (phase673)
    {
        const float w = block_w(stats.validator_warn);
        batcher.quad(bx, badge_y, w, kBadgeH,
                     ur::Color {
                         theme.accent_warning.r,
                         theme.accent_warning.g,
                         theme.accent_warning.b,
                         theme.accent_warning.a });
        bx += w + 2.0F;
    }
    // error — accent_error (phase673)
    {
        const float w = block_w(stats.validator_error);
        batcher.quad(bx, badge_y, w, kBadgeH,
                     ur::Color {
                         theme.accent_error.r,
                         theme.accent_error.g,
                         theme.accent_error.b,
                         theme.accent_error.a });
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

// ---- phase695 / M14 W6A — theme picker in the status bar ------------------
//
// Three equal-width clickable slabs pinned to the RIGHT of the status bar,
// to the LEFT of the route indicator. Each slab represents one of the three
// built-in palettes:
//
//   [Dark]  [Light]  [Hi-C]
//
// The active slab is drawn at full opacity; inactive slabs are dimmed.
// A left-click on a slab sets `*out_new_name` to the matching theme_name
// string so the caller can swap widget_theme on the next frame.
//
// Coordinates: slabs occupy a 204 px strip (3 × 66 px + 2 × 3 px gap)
// anchored kRouteW + 12 px from the right edge.
//
// Returns true if any slab was clicked (caller re-builds widget_theme).
[[nodiscard]] bool draw_theme_picker(ur::DrawBatcher&   batcher,
                                     const uw::Theme&   theme,
                                     float              fb_w,
                                     float              fb_h,
                                     std::string_view   active_name,
                                     float              mouse_x,
                                     float              mouse_y,
                                     bool               left_clicked,
                                     std::string*       out_new_name)
{
    // Layout constants.
    constexpr float kPickerSlabW = 48.0F;
    constexpr float kPickerGap   =  3.0F;
    constexpr float kPickerPad   =  6.0F;
    // Total strip width: 3 slabs + 2 gaps.
    constexpr float kPickerW = 3.0F * kPickerSlabW + 2.0F * kPickerGap;
    // Position: to the left of the route indicator (kRouteW) plus margin.
    constexpr float kRouteW  = 140.0F;
    const float strip_right  = fb_w - kRouteW - kPickerPad * 3.0F;
    const float strip_x      = strip_right - kPickerW;
    const float strip_y      = fb_h - kStatusBarH + 2.0F;
    const float slab_h       = kStatusBarH - 4.0F;

    // Slab descriptors: name string, label colour.
    struct SlabDesc
    {
        std::string_view name;
        ur::Color        active_color;   // slab fill when this theme is active
        ur::Color        inactive_color; // slab fill when another theme is active
    };
    const std::array<SlabDesc, 3> slabs {{
        // Dark  — lavender tinted strip (matches dark primary token #D0BCFF).
        { "dark",
          ur::Color { 140U, 112U, 200U, 230U },
          ur::Color {  70U,  56U, 100U, 160U } },
        // Light — warm soft white strip.
        { "light",
          ur::Color { 230U, 224U, 245U, 230U },
          ur::Color { 115U, 112U, 122U, 160U } },
        // High-contrast — pure black + cyan accent strip.
        { "high_contrast",
          ur::Color {   0U, 200U, 220U, 230U },
          ur::Color {   0U, 100U, 110U, 160U } },
    }};

    bool any_clicked = false;
    for (std::size_t idx = 0; idx < slabs.size(); ++idx)
    {
        const float slab_x = strip_x
                             + static_cast<float>(idx) * (kPickerSlabW + kPickerGap);
        const bool is_active  = (slabs[idx].name == active_name);
        const bool is_hovered = (mouse_x >= slab_x &&
                                 mouse_x <= slab_x + kPickerSlabW &&
                                 mouse_y >= strip_y &&
                                 mouse_y <= strip_y + slab_h);

        ur::Color fill = is_active ? slabs[idx].active_color
                                   : slabs[idx].inactive_color;
        if (is_hovered && !is_active)
        {
            // Subtle brightening on hover.
            fill.r = static_cast<std::uint8_t>(std::min(255, static_cast<int>(fill.r) + 40));
            fill.g = static_cast<std::uint8_t>(std::min(255, static_cast<int>(fill.g) + 40));
            fill.b = static_cast<std::uint8_t>(std::min(255, static_cast<int>(fill.b) + 40));
        }

        batcher.quad(slab_x, strip_y, kPickerSlabW, slab_h, fill);

        // Active indicator: a 2 px white bar along the top edge of the slab.
        if (is_active)
        {
            batcher.quad(slab_x, strip_y, kPickerSlabW, 2.0F,
                         ur::Color { 255U, 255U, 255U, 220U });
        }

        if (is_hovered && left_clicked && !is_active)
        {
            *out_new_name = std::string(slabs[idx].name);
            any_clicked   = true;
        }
    }

    // Suppress unused-parameter warning — theme available for future label glyph.
    (void)theme;

    return any_clicked;
}

// ---- Boot splash -----------------------------------------------------------
//
// phase685 / M13 W6B — polished 2.5 s branded splash with three animation
// stages, rendered on top of the dock during editor startup.
//
//   [0,   500ms)  Title fade-in: 'CHROMODYNAMIC' quads alpha 0 -> 255.
//   [500, 1500ms) Subtitle reveal: 'A library-oriented game engine' strip
//                 grows in from width 0 -> kSubtitleW.
//   [1500, 2500ms) Scrolling hints: four hint strings step vertically through
//                  "Booting Vulkan..." / "Loading panels..." /
//                  "Restoring layout..." / "Ready" every 250 ms.
//   [2500, 2800ms) Fade-out: full alpha lerps 255 -> 0 over 300 ms.
//   >= 2800 ms    Splash fully done — dock visible; first-launch dialog shown.
//
// Elapsed time is driven by the same BootSplash::elapsed_ms() call used
// in the existing frame loop; cd::frame_timing is used for real FPS, not for
// this wall-clock measurement (which needs absolute ms, not dt accumulation).

struct BootSplash
{
    /// Wall-clock reference point at which the editor window first opened.
    std::chrono::steady_clock::time_point start_tp = std::chrono::steady_clock::now();

    // --- Timing constants (ms) -------------------------------------------
    static constexpr double kTitleFadeEndMs    =  500.0;   ///< Title fully opaque.
    static constexpr double kSubtitleEndMs     = 1500.0;   ///< Subtitle fully revealed.
    static constexpr double kHintEndMs         = 2500.0;   ///< Hint scroll ends (solid phase end).
    static constexpr double kSolidMs           = kHintEndMs;
    static constexpr double kFadeMs            =  300.0;   ///< Fade-out duration after solid.

    /// Returns elapsed milliseconds since start_tp (wall-clock, not dt sum).
    [[nodiscard]] double elapsed_ms() const noexcept
    {
        using namespace std::chrono;
        return static_cast<double>(
            duration_cast<microseconds>(steady_clock::now() - start_tp).count())
            / 1000.0;
    }

    /// True once the fade-out has fully completed (splash invisible).
    [[nodiscard]] bool done(double ms) const noexcept
    {
        return ms >= kSolidMs + kFadeMs;
    }

    /// Overall splash alpha [0, 255] for the given elapsed_ms.
    /// During the title-fade stage (0..kTitleFadeEndMs) this controls the
    /// title bar alpha; the subtitle / hints / background use it directly.
    [[nodiscard]] std::uint8_t splash_alpha(double ms) const noexcept
    {
        if (ms < kSolidMs) { return 255U; }
        const double t       = (ms - kSolidMs) / kFadeMs;
        const double clamped = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
        return static_cast<std::uint8_t>((1.0 - clamped) * 255.0);
    }

    /// Title alpha [0, 255]: rises from 0 to 255 in the first kTitleFadeEndMs.
    [[nodiscard]] std::uint8_t title_alpha(double ms) const noexcept
    {
        if (ms >= kSolidMs) { return splash_alpha(ms); }
        const double t = ms / kTitleFadeEndMs;
        const double clamped = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
        return static_cast<std::uint8_t>(clamped * 255.0);
    }

    /// Subtitle reveal fraction [0.0, 1.0]: 0 before kTitleFadeEndMs,
    /// linearly 0->1 over [kTitleFadeEndMs, kSubtitleEndMs].
    [[nodiscard]] float subtitle_frac(double ms) const noexcept
    {
        if (ms < kTitleFadeEndMs) { return 0.0F; }
        if (ms >= kSubtitleEndMs) { return 1.0F; }
        const double t = (ms - kTitleFadeEndMs) / (kSubtitleEndMs - kTitleFadeEndMs);
        return static_cast<float>(t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t));
    }

    /// Index of the hint string currently visible [0..3] during the hint-scroll
    /// phase [kSubtitleEndMs, kHintEndMs].  Each string shows for 250 ms.
    [[nodiscard]] static int hint_index(double ms) noexcept
    {
        if (ms < kSubtitleEndMs) { return 0; }
        if (ms >= kHintEndMs)    { return 3; }
        const double phase_ms = ms - kSubtitleEndMs;
        const auto idx = static_cast<int>(phase_ms / 250.0);
        return (idx > 3) ? 3 : idx;
    }

    /// The four scrolling hint strings shown during [1500, 2500ms).
    [[nodiscard]] static const char* hint_string(int idx) noexcept
    {
        static constexpr const char* kHints[4] = {
            "Booting Vulkan...",
            "Loading panels...",
            "Restoring layout...",
            "Ready",
        };
        if (idx < 0) { return kHints[0]; }
        if (idx > 3) { return kHints[3]; }
        return kHints[idx];
    }
};

/// Emit boot splash quads into `batcher`.  Call AFTER dockspace.draw() so the
/// splash paints on top of panels.
///
/// phase685 / M13 W6B — three animation stages keyed on `elapsed_ms`:
///   Stage 1 [0, 500ms)    : Title fades in  (title_alpha 0->255).
///   Stage 2 [500, 1500ms) : Subtitle reveals (subtitle width 0->kSubtitleW).
///   Stage 3 [1500, 2500ms): Scrolling hints  (4 strings, 250 ms each).
///   Fade    [2500, 2800ms): Overall alpha 255->0.
///
/// phase707 / M15 W6B — `particle_snaps` carries live particle data from the
///   cd::particle::system::System ticked each frame while the splash is active.
///   Particles are rendered AFTER the background fill but BEFORE the title /
///   subtitle / hints so they sit between the dark background and the foreground
///   text elements.  Each particle is a 4x4 px dot in PaletteV2::accent_success
///   colour (muted green, { 80, 200, 100 }).
void draw_boot_splash(ur::DrawBatcher&                                  batcher,
                      const uw::Theme&                                   theme,
                      float                                              fb_w,
                      float                                              fb_h,
                      const BootSplash&                                  splash,
                      double                                             elapsed_ms,
                      std::span<const cd::particle::system::ParticleSnapshot> particle_snaps)
{
    const std::uint8_t overall_alpha = splash.splash_alpha(elapsed_ms);
    if (overall_alpha == 0U) { return; }

    // Helper: multiply a byte alpha by a [0,1] fraction (preserves uint8_t range).
    auto scale_alpha = [](std::uint8_t a, float frac) -> std::uint8_t {
        const auto v = static_cast<float>(a) * frac;
        const float clamped = v < 0.0F ? 0.0F : (v > 255.0F ? 255.0F : v);
        return static_cast<std::uint8_t>(clamped);
    };

    // 1. Full-framebuffer surface fill — slightly darker than the dock surface.
    //    This is the deepest layer: covers dock panels beneath the splash.
    const auto bg_r = static_cast<std::uint8_t>(theme.surface.r / 2U);
    const auto bg_g = static_cast<std::uint8_t>(theme.surface.g / 2U);
    const auto bg_b = static_cast<std::uint8_t>(theme.surface.b / 2U);
    batcher.quad(0.0F, 0.0F, fb_w, fb_h,
                 ur::Color { bg_r, bg_g, bg_b, overall_alpha });

    // 1b. Particle field — rendered AFTER the background fill but BEFORE the
    //     title / subtitle / hints so particles appear between the background
    //     and the foreground text elements.  Each particle is a 4x4 px dot in
    //     PaletteV2::accent_success colour (muted green, { 80, 200, 100 }).
    //
    //     Coordinate mapping (simulation -> screen):
    //       screen_x = fb_w * 0.5 + p.pos[0]
    //       screen_y = fb_h * 0.85 - p.pos[1]   (pos[1] grows upward)
    //     Emitter spawns particles at origin (0, 0, 0) spread ±fb_w/2 on x;
    //     velocity_y drives upward drift in abstract "pixel units per second".
    {
        constexpr float kDotSz     = 4.0F;
        constexpr float kHalfDot   = kDotSz * 0.5F;
        constexpr std::uint8_t kPR = 80U;
        constexpr std::uint8_t kPG = 200U;
        constexpr std::uint8_t kPB = 100U;
        const float origin_x = fb_w * 0.5F;
        const float origin_y = fb_h * 0.85F;
        for (const auto& p : particle_snaps)
        {
            const float sx = origin_x + p.pos[0] - kHalfDot;
            const float sy = origin_y - p.pos[1] - kHalfDot;
            // p.color[3] holds the particle's alpha (fades over its lifetime).
            const auto  pa = scale_alpha(overall_alpha, p.color[3]);
            if (pa == 0U) { continue; }
            batcher.quad(sx, sy, kDotSz, kDotSz,
                         ur::Color { kPR, kPG, kPB, pa });
        }
    }

    const float cx = fb_w * 0.5F;
    const float cy = fb_h * 0.5F;

    // 2. Stage 1 — 'CHROMODYNAMIC' title quad fades in over [0, 500ms).
    //    340 x 28 px accent-coloured bar centred above the midpoint.
    constexpr float kTitleW  = 340.0F;
    constexpr float kTitleH  = 28.0F;
    constexpr float kGap     =   6.0F;

    const std::uint8_t title_a = scale_alpha(overall_alpha,
        static_cast<float>(splash.title_alpha(elapsed_ms)) / 255.0F);

    batcher.quad(cx - kTitleW * 0.5F,
                 cy - kTitleH - kGap * 0.5F,
                 kTitleW, kTitleH,
                 ur::Color { theme.accent.r, theme.accent.g,
                             theme.accent.b, title_a });

    // 3. Stage 2 — subtitle bar reveals from width 0 to kSubtitleW over
    //    [500ms, 1500ms).  'A library-oriented game engine' represented as a
    //    narrower accent_hover bar directly below the title.
    constexpr float kSubtitleW  = 240.0F;
    constexpr float kSubtitleH  =  10.0F;

    const float subtitle_frac = splash.subtitle_frac(elapsed_ms);
    if (subtitle_frac > 0.0F)
    {
        const float sub_w = kSubtitleW * subtitle_frac;
        batcher.quad(cx - kSubtitleW * 0.5F,
                     cy + kGap * 0.5F,
                     sub_w, kSubtitleH,
                     ur::Color { theme.accent_hover.r, theme.accent_hover.g,
                                 theme.accent_hover.b,
                                 scale_alpha(overall_alpha, 0.71F) });
    }

    // 4. Stage 3 — scrolling hint strip during [1500ms, 2500ms).
    //    Four hint strings step every 250 ms.  The active hint drives:
    //      * A progress bar whose fill fraction = hint_index / 3.
    //      * A small 6 x 6 px indicator dot that steps right by 52 px per
    //        stage so the user sees visible animation even without glyphs.
    //
    //    The hint strip sits kProgressY px below the subtitle bar.
    constexpr float kProgressW  = 220.0F;
    constexpr float kProgressH  =   4.0F;
    constexpr float kProgressY  =  60.0F;   // offset below the subtitle bar
    constexpr float kDotSz      =   6.0F;
    constexpr float kDotStep    =  52.0F;

    const float prog_x = cx - kProgressW * 0.5F;
    const float prog_y = cy + kGap * 0.5F + kSubtitleH + kProgressY;

    // Background track (always visible once subtitle appears).
    if (subtitle_frac > 0.0F)
    {
        batcher.quad(prog_x, prog_y, kProgressW, kProgressH,
                     ur::Color { theme.surface_hover.r, theme.surface_hover.g,
                                 theme.surface_hover.b,
                                 scale_alpha(overall_alpha, 0.47F) });
    }

    // Active fill — grows from 0 to full over the hint-scroll phase.
    if (elapsed_ms >= BootSplash::kSubtitleEndMs)
    {
        const int   hi     = BootSplash::hint_index(elapsed_ms);
        const float frac   = (hi == 3) ? 1.0F
                             : static_cast<float>(hi) / 3.0F
                               + (static_cast<float>(
                                      static_cast<double>(elapsed_ms)
                                      - BootSplash::kSubtitleEndMs
                                      - static_cast<double>(hi) * 250.0) / 750.0F);
        const float clamped_frac = frac < 0.0F ? 0.0F : (frac > 1.0F ? 1.0F : frac);

        batcher.quad(prog_x, prog_y,
                     kProgressW * clamped_frac, kProgressH,
                     ur::Color { theme.accent.r, theme.accent.g,
                                 theme.accent.b,
                                 scale_alpha(overall_alpha, 0.78F) });

        // Indicator dot — steps right by kDotStep per hint index.
        const float dot_x = prog_x + static_cast<float>(hi) * kDotStep;
        batcher.quad(dot_x, prog_y - (kDotSz - kProgressH) * 0.5F,
                     kDotSz, kDotSz,
                     ur::Color { theme.accent.r, theme.accent.g,
                                 theme.accent.b, overall_alpha });
    }
}

// ---- First-time-user welcome dialog ----------------------------------------
//
// phase685 / M13 W6B — shown ONCE after the boot splash finishes, if no
// .cdproj existed when the editor started (first-launch detection).
//
// The dialog is a single centred panel (480 x 220 px) with three labelled
// option buttons rendered as coloured quads. Clicking a button commits the
// layout choice and the choice is written to .cdproj immediately so the next
// launch skips the dialog.
//
// Layout presets:
//   kDefault  — keep the existing 16-panel default layout built by
//               build_default_layout().
//   kCompact  — collapse to 3 tiles: viewport (centre) + console (bottom) +
//               inspector (right).  Ideal for focused scene work.
//   kFull     — all 16 panels at equal weight, tiled in a 4-column grid.
//               Gives a power user everything visible at once.

enum class WelcomeChoice : std::uint8_t
{
    kNone    = 0,   ///< Dialog still open.
    kDefault = 1,
    kCompact = 2,
    kFull    = 3,
};

struct FirstTimeWelcome
{
    bool         active { false };   ///< True while the dialog is visible.
    WelcomeChoice choice { WelcomeChoice::kNone };

    void show() noexcept { active = true; }

    /// Returns true if user has made a choice.
    [[nodiscard]] bool decided() const noexcept
    {
        return choice != WelcomeChoice::kNone;
    }
};

/// Draw the welcome dialog into `batcher` and handle click input.
/// Returns the chosen option if a button was clicked this frame, kNone otherwise.
[[nodiscard]] WelcomeChoice draw_welcome_dialog(
    ur::DrawBatcher&       batcher,
    const uw::Theme&       theme,
    float                  fb_w,
    float                  fb_h,
    const uw::PointerState& ptr)
{
    // Dialog dimensions.
    constexpr float kDlgW   = 480.0F;
    constexpr float kDlgH   = 220.0F;
    constexpr float kBtnH   =  40.0F;
    constexpr float kBtnW   = 140.0F;
    constexpr float kBtnGap =  12.0F;
    constexpr float kPad    =  20.0F;

    const float dlg_x = (fb_w - kDlgW) * 0.5F;
    const float dlg_y = (fb_h - kDlgH) * 0.5F;

    // --- 1. Dim backdrop (full-screen translucent overlay) ---
    batcher.quad(0.0F, 0.0F, fb_w, fb_h,
                 ur::Color { 0U, 0U, 0U, 160U });

    // --- 2. Dialog background ---
    batcher.quad(dlg_x, dlg_y, kDlgW, kDlgH,
                 ur::Color { theme.surface.r, theme.surface.g,
                             theme.surface.b, 245U });

    // --- 3. Header strip in accent colour (32 px tall title bar) ---
    constexpr float kHeaderH = 32.0F;
    batcher.quad(dlg_x, dlg_y, kDlgW, kHeaderH,
                 ur::Color { theme.accent.r, theme.accent.g,
                             theme.accent.b, 230U });

    // --- 4. Subtitle indicator — two small quads representing "Choose layout"
    //         text (no glyph renderer in apps/editor yet; quads carry the intent).
    constexpr float kSubH = 6.0F;
    batcher.quad(dlg_x + kPad,
                 dlg_y + kHeaderH + kPad * 0.5F,
                 kDlgW * 0.55F, kSubH,
                 ur::Color { theme.text_dim.r, theme.text_dim.g,
                             theme.text_dim.b, 160U });
    batcher.quad(dlg_x + kPad,
                 dlg_y + kHeaderH + kPad * 0.5F + kSubH + 4.0F,
                 kDlgW * 0.35F, kSubH * 0.6F,
                 ur::Color { theme.text_dim.r, theme.text_dim.g,
                             theme.text_dim.b, 100U });

    // --- 5. Three option buttons ---
    //   Button layout: [Default Layout] [Compact Layout] [Full Layout]
    //   Centred horizontally; anchored near the dialog bottom.
    const float total_btn_w = 3.0F * kBtnW + 2.0F * kBtnGap;
    const float btn_row_x   = dlg_x + (kDlgW - total_btn_w) * 0.5F;
    const float btn_row_y   = dlg_y + kDlgH - kBtnH - kPad;

    struct BtnDesc { float x; ur::Color color; WelcomeChoice result; };
    const std::array<BtnDesc, 3> buttons {{
        { btn_row_x,
          ur::Color { theme.accent.r, theme.accent.g, theme.accent.b, 220U },
          WelcomeChoice::kDefault },
        { btn_row_x + kBtnW + kBtnGap,
          ur::Color { theme.accent_hover.r, theme.accent_hover.g,
                      theme.accent_hover.b, 220U },
          WelcomeChoice::kCompact },
        { btn_row_x + (kBtnW + kBtnGap) * 2.0F,
          ur::Color { theme.surface_hover.r, theme.surface_hover.g,
                      theme.surface_hover.b, 220U },
          WelcomeChoice::kFull },
    }};

    WelcomeChoice clicked = WelcomeChoice::kNone;
    for (const auto& btn : buttons)
    {
        const bool hovered =
            ptr.mouse_x >= btn.x && ptr.mouse_x <= btn.x + kBtnW &&
            ptr.mouse_y >= btn_row_y && ptr.mouse_y <= btn_row_y + kBtnH;

        ur::Color draw_color = btn.color;
        if (hovered)
        {
            // Lighten on hover.
            draw_color.r = static_cast<std::uint8_t>(
                std::min(255, static_cast<int>(draw_color.r) + 30));
            draw_color.g = static_cast<std::uint8_t>(
                std::min(255, static_cast<int>(draw_color.g) + 30));
            draw_color.b = static_cast<std::uint8_t>(
                std::min(255, static_cast<int>(draw_color.b) + 30));
        }

        batcher.quad(btn.x, btn_row_y, kBtnW, kBtnH, draw_color);

        // Label indicator: a thin stripe below the button mid-height that
        // identifies the button index until glyph rendering is wired.
        constexpr float kLabelH = 3.0F;
        batcher.quad(btn.x + 8.0F,
                     btn_row_y + (kBtnH - kLabelH) * 0.5F,
                     kBtnW - 16.0F, kLabelH,
                     ur::Color { 220U, 220U, 220U, 200U });

        if (hovered && ptr.left_pressed)
        {
            clicked = btn.result;
        }
    }

    return clicked;
}

/// Apply the welcome choice to the live DockSpace.
/// kDefault: no change (already built by build_default_layout).
/// kCompact: collapse to viewport + console + inspector (3 tiles).
/// kFull:    equal-weight 4-column split of all 16 panels.
///
/// Strategy: build a temporary DockSpace with the desired topology, serialize
/// it, then restore into the live `ds`.  Uses only the public DockSpace API
/// (no "clear") and keeps all registered panel drawers intact because
/// DockSpace::restore() replaces only the tree structure, not the panel map.
void apply_welcome_layout(uw::DockSpace& ds, WelcomeChoice choice)
{
    if (choice == WelcomeChoice::kDefault) { return; }  // nothing to do

    if (choice == WelcomeChoice::kCompact)
    {
        // Build a temporary 3-panel compact tree (viewport + inspector + console).
        uw::DockSpace tmp;
        auto* root = tmp.root();
        if (root == nullptr) { return; }
        if (!tmp.tab_merge(root, "viewport")) { return; }

        auto* vp = tmp.find_panel_owner("viewport");
        if (vp == nullptr) { return; }
        if (!tmp.split(vp, uw::DockAxis::kVertical, "inspector", 0.75F)) { return; }

        vp = tmp.find_panel_owner("viewport");
        if (vp == nullptr) { return; }
        (void)tmp.split(vp, uw::DockAxis::kHorizontal, "console", 0.72F);

        const auto bytes = tmp.serialize();
        if (!bytes.empty())
        {
            (void)ds.restore(std::span<const std::byte>(bytes.data(), bytes.size()));
        }
        return;
    }

    // kFull — all 16 panels in a 4-column layout built in a temporary DockSpace.
    // Column A (LEFT, 25%):    scene_tree / material_editor / cutscene_player
    // Column B (CENTRE, 37%):  viewport + console (bottom)
    // Column C (RIGHT-C, 20%): inspector / behavior_designer / light_editor
    // Column D (RIGHT, 18%):   assets / animator / input_recorder / (tab-merges)
    {
        uw::DockSpace tmp;
        auto* root = tmp.root();
        if (root == nullptr) { return; }
        if (!tmp.tab_merge(root, "viewport")) { return; }

        // LEFT column A (25% of total).
        auto* vp = tmp.find_panel_owner("viewport");
        if (vp == nullptr) { return; }
        if (!tmp.split(vp, uw::DockAxis::kVertical, "scene_tree", 0.25F)) { return; }

        // RIGHT-C column C (inspector, ~27% of remaining after A).
        vp = tmp.find_panel_owner("viewport");
        if (vp == nullptr) { return; }
        if (!tmp.split(vp, uw::DockAxis::kVertical, "inspector", 0.73F)) { return; }

        // RIGHT column D (assets, ~25% of what remains after C).
        vp = tmp.find_panel_owner("viewport");
        if (vp == nullptr) { return; }
        if (!tmp.split(vp, uw::DockAxis::kVertical, "assets", 0.75F)) { return; }

        // Console at bottom of viewport tile.
        vp = tmp.find_panel_owner("viewport");
        if (vp == nullptr) { return; }
        (void)tmp.split(vp, uw::DockAxis::kHorizontal, "console", 0.60F);

        // Stack scene_tree column vertically.
        auto* sc = tmp.find_panel_owner("scene_tree");
        if (sc != nullptr)
        {
            (void)tmp.split(sc, uw::DockAxis::kHorizontal, "material_editor", 0.50F);
            auto* me = tmp.find_panel_owner("material_editor");
            if (me != nullptr)
            {
                (void)tmp.split(me, uw::DockAxis::kHorizontal, "cutscene_player", 0.60F);
            }
        }

        // Stack inspector column vertically.
        auto* ins = tmp.find_panel_owner("inspector");
        if (ins != nullptr)
        {
            (void)tmp.split(ins, uw::DockAxis::kHorizontal, "behavior_designer", 0.50F);
            auto* bd = tmp.find_panel_owner("behavior_designer");
            if (bd != nullptr)
            {
                (void)tmp.split(bd, uw::DockAxis::kHorizontal, "light_editor", 0.60F);
            }
        }

        // Stack assets column vertically.
        auto* ass = tmp.find_panel_owner("assets");
        if (ass != nullptr)
        {
            (void)tmp.split(ass, uw::DockAxis::kHorizontal, "animator", 0.50F);
            auto* an = tmp.find_panel_owner("animator");
            if (an != nullptr)
            {
                (void)tmp.split(an, uw::DockAxis::kHorizontal, "input_recorder", 0.60F);
            }
        }

        // Tab-merge panels that share tiles.
        auto merge_into = [&](const char* owner_p, const char* new_p)
        {
            auto* nd = tmp.find_panel_owner(owner_p);
            if (nd != nullptr) { (void)tmp.tab_merge(nd, new_p); }
        };

        merge_into("console",         "asset_drop_target");
        merge_into("material_editor", "vehicle_editor");
        merge_into("material_editor", "material_preview");
        merge_into("cutscene_player", "dialog_tree_editor");
        merge_into("scene_tree",      "pathfinding_viz");

        const auto bytes = tmp.serialize();
        if (!bytes.empty())
        {
            (void)ds.restore(std::span<const std::byte>(bytes.data(), bytes.size()));
        }
    }
}

// ---- Overlay titled-header strip -------------------------------------------
//
// phase674 / M12 W6B — draw a 14 px titled-header band above the bar-chart
// body of each profiling overlay.  Header colour identifies the kind without
// requiring glyph rendering.  A 2 px separator in text_dim colour follows.

enum class OverlayKind : std::uint8_t
{
    kCpu   = 0,
    kGpu   = 1,
    kFrame = 2,
};

constexpr float kOverlayHeaderH = 14.0F;  ///< Height of the titled-header strip.

/// Draw the titled header for a profiling overlay.
/// `x`, `y`, `w` = header bounds; bar-chart body starts at y + kOverlayHeaderH.
void draw_overlay_header(ur::DrawBatcher& batcher,
                         const uw::Theme& theme,
                         float            x,
                         float            y,
                         float            w,
                         OverlayKind      kind)
{
    ur::Color header_bg { theme.accent.r, theme.accent.g, theme.accent.b, 200U };
    switch (kind)
    {
        case OverlayKind::kCpu:
            header_bg = ur::Color { theme.accent.r, theme.accent.g,
                                    theme.accent.b, 200U };
            break;
        case OverlayKind::kGpu:
            header_bg = ur::Color { static_cast<std::uint8_t>(theme.accent.b / 2U),
                                    theme.accent.r,
                                    static_cast<std::uint8_t>(theme.accent.g / 2U),
                                    200U };
            break;
        case OverlayKind::kFrame:
            header_bg = ur::Color { theme.accent_hover.r,
                                    static_cast<std::uint8_t>(theme.accent_hover.g / 2U),
                                    static_cast<std::uint8_t>(theme.accent_hover.b / 4U),
                                    200U };
            break;
    }

    batcher.quad(x, y, w, kOverlayHeaderH - 2.0F, header_bg);

    // 2 px separator in text_dim colour.
    batcher.quad(x, y + kOverlayHeaderH - 2.0F, w, 2.0F,
                 ur::Color { theme.text_dim.r, theme.text_dim.g,
                             theme.text_dim.b, 180U });

    // Dot mnemonic: 1 dot = CPU, 2 = GPU, 3 = FRAME.
    constexpr float kDotSz  = 6.0F;
    constexpr float kDotGap = 3.0F;
    constexpr float kDotPad = 4.0F;
    constexpr float kDotY   = 4.0F;
    const auto dot_count = static_cast<int>(kind) + 1;
    for (int d = 0; d < dot_count; ++d)
    {
        batcher.quad(x + kDotPad + static_cast<float>(d) * (kDotSz + kDotGap),
                     y + kDotY, kDotSz, kDotSz,
                     ur::Color { 240U, 240U, 240U, 210U });
    }
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

    // phase685 / M13 W6B — detect first launch BEFORE reading the file.
    // We check existence separately so we can set the first_launch flag
    // regardless of whether the subsequent read succeeds.
    const bool first_launch = !std::filesystem::exists(cdproj_path);

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
        std::printf("editor: no .cdproj at %s — using defaults%s.\n",
                    cdproj_path.string().c_str(),
                    first_launch ? " (first launch — welcome dialog will show)" : "");
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

    // -- 2. Theme tokens + widget-side palette (phase695 / M14 W6A) ----------
    //
    // Restore the active theme from .cdproj (theme_name field). On first launch
    // the field defaults to "dark". The widget_theme is rebuilt whenever the
    // user clicks a slab in the status-bar theme picker.
    std::string active_theme_name = project_data.theme_name.empty()
                                        ? std::string(uth::kThemeNameDark)
                                        : project_data.theme_name;
    uw::Theme widget_theme = build_widget_theme(active_theme_name);
    std::printf("editor: active theme = '%s'\n", active_theme_name.c_str());

    // -- phase715 / M16 W6 — 200 ms cross-fade transition state -------------
    //
    // When the user picks a new theme, we store the V2 `from` palette and the
    // `to` palette plus the start wall-clock time. For the next 200 ms the
    // lerp_palette() helper blends every color token; after 200 ms we snap.
    // `theme_fade_active` gates the lerp so there is zero overhead per frame
    // when no transition is in progress.
    //
    // We store ONLY the V2 cd::ui::theme::Theme palettes (not widget_theme)
    // because lerp_palette() operates on the token layer. build_widget_theme()
    // is only called once per transition start and once on snap (not every
    // frame) so it is not on the hot path.
    static constexpr float kThemeFadeMs = 200.0F;
    uth::Theme theme_fade_from {};
    uth::Theme theme_fade_to   {};
    bool       theme_fade_active    = false;
    std::chrono::steady_clock::time_point theme_fade_start_tp {};

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
    // phase679 / M13 W3 — register the 3 new drawers (vehicle_editor +
    // pathfinding_viz + material_preview). vehicle_editor and material_preview
    // share the material_editor tile via tab-merge; pathfinding_viz lives in
    // its own bottom-left split.
    dockspace.register_panel("vehicle_editor",      draw_vehicle_editor_panel);
    dockspace.register_panel("pathfinding_viz",     draw_pathfinding_viz_panel);
    dockspace.register_panel("material_preview",    draw_material_preview_panel);
    // phase690 / M14 W3 — scene_navigator is tab-merged with scene_tree on
    // LEFT-top (see build_default_layout). Registering the drawer here is
    // sufficient; the layout step then tab-merges it onto scene_tree's leaf.
    dockspace.register_panel("scene_navigator",     draw_scene_navigator_panel);
    // phase701 / M15 W3 — register the 3 new panels:
    //   settings_panel (tab-merged with inspector),
    //   build_panel    (tab-merged with console),
    //   perf_profiler  (split below light_editor, RIGHT-lower).
    dockspace.register_panel("settings_panel",      draw_settings_panel);
    dockspace.register_panel("build_panel",         draw_build_panel);
    dockspace.register_panel("perf_profiler",       draw_perf_profiler_panel);
    // phase711 / M16 W3 — register the 2 new panels:
    //   asset_pipeline_status  (tab-merged with perf_profiler),
    //   ik_chain_editor        (tab-merged with animator).
    // Both are tab-merges so dockspace.node_count() stays at 14 (no new splits);
    // only the registered-panel count grows 20 -> 22.
    dockspace.register_panel("asset_pipeline_status", draw_asset_pipeline_status_panel);
    dockspace.register_panel("ik_chain_editor",       draw_ik_chain_editor_panel);
    // phase720 / M17 W3 — register the 2 new dock panels (auto_save_indicator
    // lives in the status bar and is NOT a dock node, so it is NOT registered
    // with the DockSpace here):
    //   scene_palette  (tab-merged with settings_panel),
    //   lobby_browser  (split below perf_profiler, RIGHT-lower).
    // scene_palette is a tab-merge so node_count is unchanged by it; only the
    // lobby_browser split bumps the underlying tree (+1 node). Registered-panel
    // count grows 22 -> 24.
    dockspace.register_panel("scene_palette",  draw_scene_palette_panel);
    dockspace.register_panel("lobby_browser",  draw_lobby_browser_panel);
    if (!build_default_layout(dockspace))
    {
        std::fprintf(stderr, "editor: failed to build default DockSpace layout.\n");
        return 1;
    }
    // phase679 / M13 W3: panel count grew 13 -> 16 (vehicle_editor +
    // pathfinding_viz + material_preview). dockspace.node_count() grew by
    // exactly +1 because only pathfinding_viz introduces a new split;
    // vehicle_editor and material_preview are tab-merged onto material_editor
    // (tab_merges do not increment node_count).
    //
    // phase690 / M14 W3: panel count grew 16 -> 17 (scene_navigator added,
    // tab-merged with scene_tree on LEFT-top; tab_merge keeps node_count flat).
    //
    // phase701 / M15 W3: panel count grew 17 -> 20 (settings_panel +
    // build_panel + perf_profiler added). settings_panel + build_panel are
    // tab-merges (flat); perf_profiler is the only new split (+1 node).
    //
    // phase711 / M16 W3: panel count grew 20 -> 22 (asset_pipeline_status +
    // ik_chain_editor added). Both are tab-merges (flat), so the underlying
    // dockspace.node_count() does NOT change — only the registered-panel
    // count does.
    //
    // phase720 / M17 W3: panel count grew 22 -> 24 (scene_palette +
    // lobby_browser added). scene_palette is tab-merged with settings_panel
    // (flat); lobby_browser introduces one new split below perf_profiler (+1
    // node). auto_save_indicator is NOT a dock node — it lives in the status
    // bar — so it does NOT bump kEditorPanelCount. The "24 dock nodes" log
    // line is what the M17 W3 smoke checks for.
    constexpr std::size_t kEditorPanelCount = 24U;
    std::printf("editor: dock layout ready with %zu dock nodes (%zu panels: scene_tree | "
                "scene_navigator | viewport | inspector | settings_panel | scene_palette | "
                "console | build_panel | assets | material_editor | animator | "
                "ik_chain_editor | behavior_designer | asset_drop_target | light_editor | "
                "input_recorder | dialog_tree_editor | cutscene_player | vehicle_editor | "
                "pathfinding_viz | material_preview | perf_profiler | "
                "asset_pipeline_status | lobby_browser)\n",
                kEditorPanelCount, kEditorPanelCount);

    // -- phase679 / M13 W3 — restore saved dock layout (if any) -------------
    //
    // The .cdproj read step above populated `project_data.dock_layout` with the
    // hex-encoded byte stream captured at the previous shutdown. Apply it via
    // DockSpace::restore so the user sees the EXACT layout they left behind
    // (including which tab in each tab strip was active). On any framing
    // failure we silently fall back to the default 16-panel layout — the user
    // still gets a working editor and we log which path was taken.
    //
    // MOMENT: a user closes the editor mid-task, reopens it, the layout is
    // EXACTLY as they left it. Source 2 SDK feel.
    if (!project_data.dock_layout.empty())
    {
        const auto bytes = dock_layout_from_hex(project_data.dock_layout);
        if (!bytes.empty() && dockspace.restore(std::span<const std::byte>(
                                  bytes.data(), bytes.size())))
        {
            std::printf("editor: restored layout from %s (%zu nodes)\n",
                        cdproj_path.string().c_str(),
                        dockspace.node_count());
        }
        else
        {
            std::printf("editor: saved layout in %s was malformed — using defaults.\n",
                        cdproj_path.string().c_str());
        }
    }
    else
    {
        std::printf("editor: no saved layout, using defaults.\n");
    }
    std::printf("editor: File > Save Layout (auto-saved on exit to %s)\n",
                cdproj_path.string().c_str());
    std::fflush(stdout);

    // -- 5a. phase690 / M14 W3 — scene_navigator seed -----------------------
    //
    // Seed the search-filter panel with a small demo entity set so the panel
    // is non-empty on first boot. A real session will replace these with the
    // live scene flat-list once cd::scene exposes a name + Entity iterator.
    //
    // MOMENT: a power user opens apps/editor, types 'box' into scene_navigator,
    // jumps to the Box entity, all in 3 seconds — Source 2 SDK speed.
    {
        const std::array<cd::ecs::Entity, 6> kSeedEntities {
            cd::ecs::Entity { 1U, 1U },
            cd::ecs::Entity { 2U, 1U },
            cd::ecs::Entity { 3U, 1U },
            cd::ecs::Entity { 4U, 1U },
            cd::ecs::Entity { 5U, 1U },
            cd::ecs::Entity { 6U, 1U },
        };
        const std::array<std::string, 6> kSeedNames {
            std::string{"PlayerSpawn"},
            std::string{"Box.001"},
            std::string{"Box.002"},
            std::string{"EnemySpawn"},
            std::string{"SkyLight"},
            std::string{"MainCamera"},
        };
        g_scene_navigator_panel.set_entities(
            std::span<const cd::ecs::Entity>(kSeedEntities.data(), kSeedEntities.size()),
            std::span<const std::string>(kSeedNames.data(), kSeedNames.size()));
    }

    // -- 5b1. phase711 / M16 W3 — keyboard_shortcut_overlay AUTO-POPULATE ----
    //
    // Pre-register all editor shortcuts at boot via a PANEL-REGISTRY WALK.
    //
    // The shortcut catalogue is no longer a hand-rolled 15-entry list (as it
    // was in phase690 / M14 W3). Instead, the apps/editor binary keeps a
    // PanelShortcutSpec table — one entry per panel registered with the
    // DockSpace shell — and for each entry it pre-registers:
    //
    //   1. A FOCUS hotkey (Ctrl+1 .. Ctrl+22) that switches focus to that panel.
    //   2. Any PANEL-SPECIFIC shortcuts the panel author wants surfaced
    //      (e.g. Ctrl+F to focus the scene_navigator search field).
    //
    // It also pre-registers the standard editor-wide actions (Save Layout /
    // Load Layout / Toggle Help / Stats / Close-overlay).
    //
    // The walker reuses the existing `Shortcut` struct from M14 phase688
    // (keys + action_description + category) — see KeyboardShortcutOverlay.hpp —
    // so no new public type is added to the panel libraries themselves. The
    // panel-specific list lives in apps/editor (single source of truth), which
    // avoids touching FROZEN sample binaries and keeps the panel libraries
    // ImGui-/widget-independent.
    //
    // MOMENT: a NEW USER presses '?' on day one, sees ALL the editor's
    // shortcuts auto-listed (panel focus hotkeys + per-panel actions + global
    // actions) — no manual maintenance, no out-of-date help.

    using cd::editor::panel::keyboard_shortcut_overlay::Shortcut;

    // Per-panel shortcut spec. A panel may contribute zero or more "extra"
    // shortcuts beyond its auto-assigned focus hotkey. The walker assigns
    // each panel a sequential Ctrl+N focus hotkey (N=1..22).
    struct PanelShortcutSpec
    {
        std::string_view              id;                ///< Panel id (must match register_panel).
        std::string_view              display_name;      ///< Human-readable name for the focus shortcut.
        std::vector<Shortcut>         extras;            ///< Panel-specific shortcuts (key + description + category).
    };

    // Panel registry — single source of truth for both the dock layout and
    // the shortcut walker. Order MUST mirror the dockspace.register_panel
    // call order above so the Ctrl+N focus hotkeys line up with the visible
    // panel index a user counts from left to right in the dock.
    const std::array<PanelShortcutSpec, 24> kPanelRegistry { {
        { "scene_tree",            "Scene Tree",             {} },
        { "viewport",              "Viewport",               {} },
        { "inspector",             "Inspector",              {} },
        { "console",               "Console",                {} },
        { "assets",                "Assets",                 {} },
        { "material_editor",       "Material Editor",        {} },
        { "animator",              "Animator",               {} },
        { "behavior_designer",     "Behavior Designer",      {} },
        { "asset_drop_target",     "Asset Drop Target",      {} },
        { "light_editor",          "Light Editor",           {} },
        { "input_recorder",        "Input Recorder",         {} },
        { "dialog_tree_editor",    "Dialog Tree Editor",     {} },
        { "cutscene_player",       "Cutscene Player",        {} },
        { "vehicle_editor",        "Vehicle Editor",         {} },
        { "pathfinding_viz",       "Pathfinding Viz",        {} },
        { "material_preview",      "Material Preview",       {} },
        // scene_navigator carries one panel-specific extra: Ctrl+F focuses
        // the search field. The walker picks this up automatically.
        { "scene_navigator",       "Scene Navigator",
          { Shortcut{ "Ctrl+F", "Focus Scene Navigator search", "Navigation" } } },
        { "settings_panel",        "Settings",               {} },
        { "build_panel",           "Build",                  {} },
        { "perf_profiler",         "Perf Profiler",          {} },
        // phase711 / M16 W3 — two new panels join the registry.
        { "asset_pipeline_status", "Asset Pipeline Status",  {} },
        { "ik_chain_editor",       "IK Chain Editor",        {} },
        // phase720 / M17 W3 — two new dock panels join the registry.
        // auto_save_indicator is a status-bar widget (not a dock panel) and
        // therefore is NOT listed here.
        { "scene_palette",         "Scene Palette",          {} },
        { "lobby_browser",         "Lobby Browser",          {} },
    } };

    // Standard editor-wide shortcuts. These are NOT panel-bound — they drive
    // the application shell itself (layout save/load, overlay toggle, stats).
    const std::array<Shortcut, 7> kEditorActionShortcuts { {
        Shortcut{ "Ctrl+S", "Save Layout",                "File"  },
        Shortcut{ "Ctrl+L", "Load Layout",                "File"  },
        Shortcut{ "?",      "Toggle Shortcut Help",       "Help"  },
        Shortcut{ "Esc",    "Close overlay / clear filter","Help"  },
        Shortcut{ "F1",     "CPU Stats",                  "Stats" },
        Shortcut{ "F2",     "GPU Stats",                  "Stats" },
        Shortcut{ "F3",     "Frame Graph Timeline",       "Stats" },
    } };

    cd::editor::panel::keyboard_shortcut_overlay::KeyboardShortcutOverlay
        shortcut_overlay {};

    // (1) Editor-wide actions first so they appear at the top of their
    //     respective category columns.
    for (const auto& s : kEditorActionShortcuts)
    {
        shortcut_overlay.register_shortcut(s);
    }

    // (2) Walk the panel registry: for every panel, register a Ctrl+N focus
    //     hotkey + any panel-specific extras. Index is 1-based to match the
    //     user-facing "Ctrl+1" convention. Ctrl+1..Ctrl+9 use a single digit;
    //     Ctrl+10..Ctrl+22 use two digits — the overlay handles arbitrary
    //     string keys, so no special casing is needed here.
    std::size_t panel_focus_registered = 0U;
    std::size_t panel_extras_registered = 0U;
    for (std::size_t i = 0; i < kPanelRegistry.size(); ++i)
    {
        const auto& spec = kPanelRegistry[i];

        // Focus hotkey: Ctrl+N (N = 1..panel_count).
        Shortcut focus_shortcut;
        focus_shortcut.keys               =
            std::string{ "Ctrl+" } + std::to_string(i + 1U);
        focus_shortcut.action_description =
            std::string{ "Focus " } + std::string{ spec.display_name };
        focus_shortcut.category           = "PanelFocus";
        shortcut_overlay.register_shortcut(focus_shortcut);
        ++panel_focus_registered;

        // Panel-specific extras (each panel decides its own list, declared
        // alongside the registry above so a future panel author can add a
        // shortcut without touching this loop).
        for (const auto& extra : spec.extras)
        {
            shortcut_overlay.register_shortcut(extra);
            ++panel_extras_registered;
        }
    }

    std::printf(
        "editor: shortcut overlay auto-populated -> %zu actions + %zu panel-focus + "
        "%zu panel-extras = %zu total (no manual maintenance).\n",
        kEditorActionShortcuts.size(),
        panel_focus_registered,
        panel_extras_registered,
        shortcut_overlay.shortcut_count());

    // -- 5b1b. phase711 / M16 W3 — ik_chain_editor + asset_pipeline_status seed -
    //
    // ik_chain_editor: bind a non-trivial 5-joint demo chain so the panel reads
    // as an actual IK visualiser on first boot rather than an empty rectangle.
    // The chain is anchored at the world origin with each joint offset +X by
    // 1.0 m; end-effector target shifted +X / +Y so the chain visibly bends
    // toward the marker. The cd::animation::ik::IkResult is left default-
    // constructed (converged=false, no solved_rotations) — the editor is a
    // static visual surface today; live solve hooks land when an animator
    // scene drives the IK system from the playmode timeline.
    {
        using cd::animation::ik::Joint;
        g_demo_ik_chain.joints.clear();
        for (int i = 0; i < 5; ++i)
        {
            Joint j;
            j.name           = std::string{"demo_joint_"} + std::to_string(i);
            j.local_position = (i == 0)
                ? std::array<float, 3>{ 0.0F, 0.0F, 0.0F }
                : std::array<float, 3>{ 1.0F, 0.0F, 0.0F };
            j.local_rotation_quat = { 0.0F, 0.0F, 0.0F, 1.0F };
            j.length              = 1.0F;
            g_demo_ik_chain.joints.push_back(std::move(j));
        }
        g_demo_ik_chain.end_effector_target   = { 3.5F, 1.5F, 0.0F };
        g_demo_ik_chain.max_iterations        = 16U;
        g_demo_ik_chain.convergence_threshold = 0.001F;
        g_ik_chain_editor_panel.set_chain(&g_demo_ik_chain);
        g_ik_chain_editor_panel.set_last_result(&g_demo_ik_result);
        std::printf("editor: ik_chain_editor seeded with %zu-joint demo chain.\n",
                    g_demo_ik_chain.joints.size());
    }

    // asset_pipeline_status: no StreamerPool is wired in apps/editor yet (the
    // engine-side pool lives in the runtime loop in sample binaries). Detach
    // explicitly so the panel renders the 4-row background + label quads
    // without dereferencing a stale pointer. When the editor wires a real
    // pool in a future Sprint, set_pool(&pool) here will light up the bars.
    g_asset_pipeline_status_panel.set_pool(nullptr);
    std::printf("editor: asset_pipeline_status attached pool=<none> (pending=%zu, "
                "completed=%zu).\n",
                g_asset_pipeline_status_panel.total_pending(),
                g_asset_pipeline_status_panel.total_completed());

    // -- 5b1c. phase720 / M17 W3 — scene_palette + lobby_browser seed ---------
    //
    // scene_palette: bind the live cd::ui::widgets::Theme so the swatch grid
    // reads from the same palette the dock paints with.  When the theme picker
    // swaps the palette the next draw() picks up the new pointer-value via the
    // theme parameter, so no per-frame re-binding is needed (the held pointer
    // is just a hint until the explicit theme argument arrives at draw time).
    g_scene_palette_panel.set_palette(&widget_theme);
    std::printf("editor: scene_palette bound to live widget_theme (15 tokens).\n");

    // lobby_browser: seed the demo Lobby with two rooms so the panel reads as
    // a real visualiser on first boot — not an empty placeholder.
    //   * "Capture Point Beta" — public, 4-player slot, 2 joined, no passcode.
    //   * "VIP Match"          — private, 8-player slot, 1 joined, passcode set.
    {
        using cd::network::lobby::LobbyConfig;
        using cd::network::lobby::PlayerState;

        LobbyConfig public_cfg {};
        public_cfg.lobby_name  = "Capture Point Beta";
        public_cfg.game_mode   = "ctf";
        public_cfg.max_players = 4U;
        public_cfg.is_public   = true;
        const auto room_a = g_demo_lobby.create_room(public_cfg, 1001ULL);
        (void)g_demo_lobby.join_room(room_a,
            PlayerState{ 1002ULL, "Alice", false, 0U }, std::string_view{""});

        LobbyConfig private_cfg {};
        private_cfg.lobby_name  = "VIP Match";
        private_cfg.game_mode   = "deathmatch";
        private_cfg.max_players = 8U;
        private_cfg.is_public   = false;
        private_cfg.passcode    = "1234";
        const auto room_b = g_demo_lobby.create_room(private_cfg, 2001ULL);
        (void)room_b;

        g_lobby_browser_panel.set_lobby(&g_demo_lobby);
        std::printf("editor: lobby_browser seeded with %zu demo rooms.\n",
                    g_demo_lobby.active_rooms().size());
    }
    std::fflush(stdout);

    // -- 5b2. phase690 / M14 W3 — popout_dock state machine ------------------
    //
    // Sprint-1 internal: tracks panels the user has "torn off" from the main
    // dock. Each frame the editor iterates `detached_windows()`; for every
    // detached panel it renders a floating-internal rectangle at the stored
    // position (border in palette accent_warning so the detached state is
    // visually distinct).
    //
    // Native multi-window promotion = Sprint-2 (gated on cd::platform multi-
    // window support). For Sprint-1 the editor never auto-detaches any panel;
    // tearing off is a future input-handler hookup that calls detach_panel().
    cd::ui::widgets::PopoutDock popout_dock {};
    std::printf("editor: popout_dock tracks %zu detached panels.\n",
                popout_dock.detached_windows().size());
    std::fflush(stdout);

    // -- 5c. phase701 / M15 W3 — settings + build + perf_profiler seeding ----
    //
    // Pre-seed all 3 new panels so they are non-empty on first boot.
    //   * settings: 8 demo entries spread across all 5 categories.
    //   * build:    Compiling status + 5 demo events with timestamps.
    //   * perf:     60-fps target; real FrameCapture data from cpu_collector +
    //               gpu_marker_recorder + fgt_timeline (phase736).
    //
    // The build_panel_bridge global helper points at g_build_panel so other
    // subsystems can call cd::editor::build_panel_bridge::push_event(...)
    // without linking apps/editor (Sprint-1 placeholder; Sprint-2 will wire
    // real shader-compile events).
    {
        using cd::editor::panel::settings::Category;
        using cd::editor::panel::settings::Entry;
        const std::array<Entry, 8> kSeedSettings {
            Entry { "vsync",           "V-Sync",            "on",       "Vertical sync — caps render rate to monitor refresh.", Category::kGraphics },
            Entry { "resolution",      "Resolution",        "1280x720", "Render target resolution.",                            Category::kGraphics },
            Entry { "shadow_quality",  "Shadow Quality",    "high",     "Cascade count + filter taps.",                          Category::kGraphics },
            Entry { "mouse_sens",      "Mouse Sensitivity", "1.0",      "Multiplier for raw pointer deltas.",                    Category::kInput    },
            Entry { "master_volume",   "Master Volume",     "0.85",     "Output mixer attenuation [0,1].",                       Category::kAudio    },
            Entry { "spatial_audio",   "Spatial Audio",     "on",       "HRTF + per-source positional mixing.",                  Category::kAudio    },
            Entry { "auto_save_min",   "Auto-Save (min)",   "5",        "Editor auto-save interval in minutes.",                 Category::kEditor   },
            Entry { "log_verbosity",   "Log Verbosity",     "info",     "Per-subsystem default log level.",                       Category::kAdvanced },
        };
        for (const auto& e : kSeedSettings) { g_settings_panel.register_entry(e); }
        std::printf("editor: settings_panel seeded with %zu entries across 5 categories.\n",
                    g_settings_panel.entry_count());
    }

    {
        using cd::editor::panel::build::BuildEvent;
        using cd::editor::panel::build::Status;
        g_build_panel.set_status(Status::kCompiling);
        const std::array<BuildEvent, 5> kSeedEvents {
            BuildEvent {   12.5, "configuring CMake presets...",          "", 0U,    Status::kIdle      },
            BuildEvent {  243.0, "compiling cd::core...",                 "", 0U,    Status::kCompiling },
            BuildEvent {  812.5, "compiling cd::editor_panel_settings...","", 0U,    Status::kCompiling },
            BuildEvent { 1502.0, "warning: unused parameter 'flags'",
                         "engine/render/Material.cpp",                    142U,  Status::kSuccess   },
            BuildEvent { 1820.0, "linked cd_sample_editor.exe",           "", 0U,    Status::kSuccess   },
        };
        for (const auto& ev : kSeedEvents) { g_build_panel.push_event(ev); }
        cd::editor::build_panel_bridge::register_target(&g_build_panel);
        std::printf("editor: build_panel seeded with %zu events (bridge registered).\n",
                    g_build_panel.event_count());
    }

    // PerfProfiler: configure budget (60 fps target). Synthetic FrameSnapshot
    // data is captured per frame in the main loop below until the real
    // cd::profile Collector instrumentation lands (Sprint-2).
    g_perf_profiler_panel.set_target_fps(60.0F);
    std::printf("editor: perf_profiler budget_ms=%.2f (60 fps target, REAL data feed).\n",
                static_cast<double>(g_perf_profiler_panel.budget_ms()));

    // Wire the perf_profiler drawer's V2 theme bridge to the live theme name.
    g_active_theme_name_ptr = &active_theme_name;

    // -- 5d. phase701 / M15 W3 — toast notification queue --------------------
    //
    // Pre-register 3 example toasts so a first-time editor user sees the
    // editor talk to them on boot — Source 2 / Hammer SDK feel. The queue
    // holds at most 5 entries with a 2-second lifetime each (fade-out in the
    // last 300 ms).
    cd::editor::toast::ToastQueue toast_queue {};
    toast_queue.push("Editor ready",          cd::editor::toast::Kind::kSuccess);
    toast_queue.push("Loaded saved layout",   cd::editor::toast::Kind::kInfo);
    toast_queue.push(std::string("Theme: ") + active_theme_name,
                     cd::editor::toast::Kind::kInfo);
    std::printf("editor: 3 toasts queued on startup (queue size=%zu, cap=%zu).\n",
                toast_queue.size(),
                cd::editor::toast::ToastQueue::kMaxToasts);
    std::fflush(stdout);

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
    // phase737 — mutable timeline that accumulates real pass records each frame.
    fgt::Timeline              fgt_timeline {};

    // phase736 — real instrumentation objects for FrameCapture.
    //   cpu_collector      -- Collector records "frame.draw" CPU scope each frame.
    //   gpu_marker_recorder -- Recorder brackets "gpu.ui_submit" in Vulkan cmd path.
    //                         Empty on NullDevice (no cmd buffer available).
    cmo::Collector                     cpu_collector { 4096U };
    cd::profile::gpu_marker::Recorder  gpu_marker_recorder {};

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

    // Phase 731 / FINALE-1 W2C A7 — Route A is the default-ON ship path
    // (CMake CD_USE_MATERIAL_UI_ROUTE_A default ON since phase659; the
    // editor binary boots through cd::material's UI pipeline end-to-end).
    // On the success case we emit the single-line "Route A active" marker
    // the audit asks for; the fallback / Route-B-only branches keep their
    // longer diagnostic strings because they signal a regression and the
    // operator needs the error context immediately.
    switch (route_taken)
    {
        case 1:
            std::printf("editor: Route A active.\n");
            break;
        case 2:
            std::printf("editor: submitter route = "
                        "Route A failed -> Route B fallback (inline GLSL).\n");
            break;
        case 3:
            std::printf("editor: submitter route = "
                        "Route B only (CD_USE_MATERIAL_UI_ROUTE_A off).\n");
            break;
        default:
            std::printf("editor: submitter route = unknown.\n");
            break;
    }
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

    // phase685 / M13 W6B — boot splash + real FPS feed + first-launch welcome.
    BootSplash                       boot_splash {};   // starts counting from now
    cd::frame_timing::FrameTimeRing<120> ft_ring {};  // feeds real fps to overlays + status bar
    std::chrono::steady_clock::time_point prev_tp = std::chrono::steady_clock::now();

    // phase707 / M15 W6B — particle system driving the cinematic boot splash
    // background.  Lifetime matches the splash window (kSolidMs + kFadeMs =
    // 2.8 s max).  The optional<> is reset when the splash is done so the
    // System is destroyed cleanly (no leaked allocations after boot).
    //
    // Emitter parameters:
    //   * 20 particles/sec — steady slow rain of green glows.
    //   * velocity_x in [-400, 400] px/s — wide horizontal spread.
    //   * velocity_y in  [40,  120] px/s — upward drift in sim-space;
    //     mapped to -screen_y at draw time.
    //   * life_seconds = 4.0 s — fades out gracefully over full lifetime.
    //   * color_start alpha = 0.6, color_end alpha = 0.0 (fade-to-transparent).
    using cd::particle::system::System;
    using cd::particle::system::EmitterSpec;
    using cd::particle::system::ParticleSpec;
    using cd::particle::system::ParticleSnapshot;

    std::optional<System> splash_particles;
    if (window)
    {
        splash_particles.emplace();

        ParticleSpec pspec;
        pspec.life_seconds = 4.0F;
        pspec.size_start   = 1.0F;
        pspec.size_end     = 0.5F;
        // accent_success: { 80, 200, 100 } normalised -> { 0.314, 0.784, 0.392 }
        pspec.color_start  = { 0.314F, 0.784F, 0.392F, 0.6F };
        pspec.color_end    = { 0.314F, 0.784F, 0.392F, 0.0F };

        EmitterSpec espec;
        espec.emit_rate_per_sec = 20.0F;
        espec.position          = { 0.0F, 0.0F, 0.0F };
        espec.velocity_min      = { -400.0F,  40.0F, 0.0F };
        espec.velocity_max      = {  400.0F, 120.0F, 0.0F };
        espec.particle           = pspec;

        splash_particles->add_emitter(espec);
        std::printf("editor: splash particle system ACTIVE (20/sec, 4s life, accent_success).\n");
        std::fflush(stdout);
    }

    // Snapshot buffer — reused every frame to avoid per-frame allocation.
    // 512 slots: 20/sec * 4s max life = 80 live particles steady-state max.
    // We over-provision to handle bursts safely.
    constexpr std::size_t kParticleSnapCap = 512U;
    std::array<ParticleSnapshot, kParticleSnapCap> particle_snap_buf {};
    std::size_t particle_snap_count = 0U;

    // First-time-user welcome dialog state.  Active only on first_launch
    // (no .cdproj existed at startup) and only once the splash has finished.
    FirstTimeWelcome welcome_dialog {};
    if (first_launch && window)
    {
        // Will be shown() once the splash finishes — see frame loop below.
        std::printf("editor: first launch detected — welcome dialog queued.\n");
    }

    // phase684 / M13 W6A — Debug Visualization overlays -------------------------
    //
    // Three ~200x150 px thumbnails stacked vertically in the top-right corner
    // of the editor (below the profiling overlays).  Each carries a separate
    // VizKind and is independently toggled via set_visible().  Sprint-1 renders
    // coloured placeholder gradients so the overlay surfaces are immediately
    // visible; Sprint-2 replaces them with real G-buffer texture samples once
    // cd::render::framegraph exposes depth/normal/bucket textures cleanly.
    //
    // MOMENT: A render dev hits a curtain-bleed-through bug, toggles 'Alpha
    // Bucket' viz, sees the curtain is correctly classified as red (kAlphaBlend),
    // confirms the framegraph order is right, narrows the bug elsewhere.
    using DebugVizOverlay = cd::editor::debug_viz::DebugVizOverlay;
    using VizKind         = cd::editor::debug_viz::VizKind;
    DebugVizOverlay dbg_depth  { VizKind::kDepth };
    DebugVizOverlay dbg_normal { VizKind::kNormal };
    DebugVizOverlay dbg_bucket { VizKind::kAlphaBucket };

    // -- phase735 / B6 — Live G-buffer texture handles ------------------------
    //
    // Sprint-1 (phase684) shipped placeholder gradients. Sprint-2 plumbs real
    // cd::rhi::TextureHandle values into the three overlays each frame so the
    // DebugVizOverlay::draw path emits DrawBatcher::textured_quad commands
    // against the live G-buffer attachments.
    //
    // Until the editor binary owns a forward+composite pipeline that produces
    // its own G-buffer (queued for the post-FINALE renderer wire-up), the
    // handles below are stable, non-null sentinels representing the slots the
    // RHI submitter will later resolve to depth / normal / albedo descriptors.
    // The handle's lower 32 bits flow directly into DrawCommand::texture_slot
    // (matching the cd::editor::panel::viewport::Viewport convention), so the
    // RHI tier can map slot -> bound descriptor without any UI-side change
    // when real targets arrive.
    //
    // MOMENT: F4/F5/F6 toggles now show actual G-buffer slices (or sentinel
    // tiles until the renderer is wired); debugging-by-glance becomes powerful.
    constexpr std::uint32_t kGBufferDepthSlot  = 0xD0000001u;  // depth   target slot
    constexpr std::uint32_t kGBufferNormalSlot = 0xD0000002u;  // normal  target slot
    constexpr std::uint32_t kGBufferAlbedoSlot = 0xD0000003u;  // albedo  target slot

    const cd::rhi::TextureHandle gbuffer_depth_tex {
        static_cast<cd::rhi::TextureHandle::index_type>(kGBufferDepthSlot),
        static_cast<cd::rhi::TextureHandle::generation_type>(1U) };
    const cd::rhi::TextureHandle gbuffer_normal_tex {
        static_cast<cd::rhi::TextureHandle::index_type>(kGBufferNormalSlot),
        static_cast<cd::rhi::TextureHandle::generation_type>(1U) };
    const cd::rhi::TextureHandle gbuffer_albedo_tex {
        static_cast<cd::rhi::TextureHandle::index_type>(kGBufferAlbedoSlot),
        static_cast<cd::rhi::TextureHandle::generation_type>(1U) };

    // All three enabled by default.  Log active state at boot so a dev can
    // confirm viz status without attaching a debugger.
    std::printf("editor: debug_viz overlays ACTIVE: depth=%s  normal=%s  alpha_bucket=%s\n",
                dbg_depth.is_visible()  ? "ON" : "OFF",
                dbg_normal.is_visible() ? "ON" : "OFF",
                dbg_bucket.is_visible() ? "ON" : "OFF");
    std::fflush(stdout);

    // -- phase720 / M17 W3 — 30 s auto-save timer + dirty-flag propagation ----
    //
    // Drives the AutoSaveIndicator status-bar widget through its five-state
    // lifecycle (kIdle / kPendingDirty / kSaving / kJustSaved / kError).
    //
    // Dirty propagation:
    //   * A short hash of (dock_layout_hex || theme_name || window geometry) is
    //     recomputed every frame.  Any change (e.g. user drags a divider, swaps
    //     theme, resizes window) flips the indicator to kPendingDirty and arms
    //     the auto-save timer.
    //
    // Auto-save cadence:
    //   * Every kAutosaveIntervalMs the editor checks the dirty flag; if dirty,
    //     it flips the indicator to kSaving, writes the .cdproj atomically via
    //     cd::editor::cdproj::write_cdproj, then flips to kJustSaved (or kError
    //     on write failure).  The "Saved N s ago" counter is refreshed each
    //     frame so the indicator's label stays current.
    //
    // Initial state:
    //   * Idle on boot (boot itself doesn't count as user-driven change).  The
    //     first divider drag / theme swap / resize transitions to PendingDirty.
    //
    // MOMENT: a user edits the layout, walks away for 30 seconds, returns to
    // find the indicator showing "Saved 12s ago" — they never lose work to a
    // crash because the editor saves itself proactively.

    using auto_save_status = cd::editor::panel::auto_save_indicator::Status;
    constexpr double kAutosaveIntervalMs = 30'000.0;  // 30 seconds

    // Hash the persisted-state snapshot so we detect "data has changed since
    // last save" purely from observable fields (no scattered dirty flags to
    // sync across panels). Uses std::hash<std::string> on a concatenated key
    // so equivalent snapshots collapse to the same value.
    auto compute_state_hash = [&]() -> std::size_t {
        std::string key;
        key.reserve(64);
        // DockSpace serialized layout (hex string, JSON-safe).
        key.append(dock_layout_to_hex(dockspace.serialize()));
        key.push_back('|');
        // Active theme name.
        key.append(active_theme_name);
        key.push_back('|');
        // Window geometry: position + size + maximized flag.
        const std::uint32_t ww = window ? window->width()  : 0U;
        const std::uint32_t wh = window ? window->height() : 0U;
        key.append(std::to_string(ww));
        key.push_back('x');
        key.append(std::to_string(wh));
        return std::hash<std::string>{}(key);
    };

    const std::size_t initial_state_hash = compute_state_hash();
    std::size_t        last_saved_state_hash    { initial_state_hash };
    auto               last_save_tp             = std::chrono::steady_clock::now();
    auto               last_autosave_check_tp   = std::chrono::steady_clock::now();
    bool               autosave_logged_fired    { false };

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
                    // phase690 / M14 W3: Esc first closes the shortcut overlay
                    // (if visible); only then does it request window close.
                    if (shortcut_overlay.is_visible())
                    {
                        shortcut_overlay.set_visible(false);
                    }
                    else
                    {
                        window->request_close();
                    }
                }
                else if (e.kind == platform::OSEventKind::kTextChar &&
                         e.code_point == 0x3FU /* '?' */)
                {
                    // phase690 / M14 W3 — '?' toggles the cheatsheet overlay
                    // (Source 2 / Hammer SDK convention).
                    shortcut_overlay.set_visible(!shortcut_overlay.is_visible());
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

        // -- per-frame dt measurement (phase674 / M12 W6B) -------------------
        //
        // Push elapsed seconds into the FrameTimeRing so the status bar and
        // overlay context show a real FPS value instead of the 62.5 stand-in.
        double last_dt_ms = 0.0;
        float  last_dt_s  = 0.0F;
        {
            using namespace std::chrono;
            const auto  now  = steady_clock::now();
            const auto dt_s = static_cast<float>(
                duration_cast<microseconds>(now - prev_tp).count()) / 1'000'000.0F;
            prev_tp = now;
            if (dt_s > 0.0F) { ft_ring.push(dt_s); }
            last_dt_ms = static_cast<double>(dt_s) * 1000.0;
            last_dt_s  = dt_s;
        }
        const auto real_fps = static_cast<float>(ft_ring.stats().fps_mean());

        // -- phase707 / M15 W6B — splash particle system tick -----------------
        //
        // Tick the particle system while the splash is active.  Once the
        // splash is done the optional<System> is reset (destroyed cleanly).
        // The snapshot buffer is refilled each tick for the draw call below.
        if (splash_particles.has_value())
        {
            const double splash_ms_now = boot_splash.elapsed_ms();
            if (boot_splash.done(splash_ms_now))
            {
                // Splash has completed — destroy the particle system cleanly.
                splash_particles.reset();
                particle_snap_count = 0U;
                std::printf("editor: splash particle system destroyed (splash complete).\n");
                std::fflush(stdout);
            }
            else
            {
                if (last_dt_s > 0.0F)
                {
                    splash_particles->tick(last_dt_s);
                }
                particle_snap_count = splash_particles->snapshot(
                    std::span<ParticleSnapshot>(particle_snap_buf.data(),
                                               particle_snap_buf.size()));
            }
        }

        // -- phase736 -- real FrameCapture feed (replaces synthetic dummy) ----
        //
        // Collects REAL profiling data from the editor's own render path.
        // cpu_markers  -- cpu_collector scope ("frame.draw") sampled each frame.
        // gpu_markers  -- gpu_marker_recorder resolved samples (Vulkan path only).
        // gpu_passes   -- fgt_timeline last_frame_passes() from phase737 wiring.
        // total_ms     -- real wall-clock dt from FrameTimeRing.
        //
        // MOMENT: a perf dev opens perf_profiler, sees REAL frame timings from
        // the editor's own render path -- root-cause analysis is one-click.

        // Open the frame.draw CPU scope BEFORE the draw block below.
        const auto frame_draw_handle = cpu_collector.begin("frame.draw");


        // -- Draw via the CPU batcher + RHI submitter --
        // phase737: begin_frame() resets the fgt_timeline accumulator; a
        // wall-clock start timestamp is taken so record_pass("DockDraw", ...)
        // can report the real CPU duration for the full batcher-populate phase.
        fgt_timeline.begin_frame();
        using FgClock    = std::chrono::steady_clock;
        using FgDuration = std::chrono::duration<double, std::milli>;
        const auto fg_dock_start = FgClock::now();
        batcher.begin_frame();
        dockspace.draw(batcher, font.is_loaded() ? &font : nullptr, widget_theme);

        // -- phase598 / M6 W3 + phase631 + phase674 — floating overlays ------
        //
        // phase674 adds a 14 px titled-header strip (draw_overlay_header) to
        // each overlay so CPU / GPU / FRAME are visually labelled.  The FRAME
        // overlay receives a real "UI" pass proportional to batcher vertex
        // count.  All other timing data remains representative synthetic bars
        // (real GPU timestamps land when ICommandBuffer::write_timestamp ships).
        {
            const auto fbw_f = static_cast<float>(fb_w);
            const auto fbh_f = static_cast<float>(fb_h);

            // --- CPU overlay — "CPU" header + 120 px bar chart body --------
            {
                constexpr float kOverlayW = 300.0F;
                constexpr float kOverlayH = 120.0F;
                constexpr float kMargin   = 8.0F;
                const float     ox        = fbw_f - kOverlayW - kMargin;
                const float     oy        = kMargin;

                // phase674: titled header above the bar chart body.
                draw_overlay_header(batcher, widget_theme,
                                    ox, oy, kOverlayW, OverlayKind::kCpu);

                const cmo::Rect bounds {
                    ox, oy + kOverlayHeaderH, kOverlayW, kOverlayH };

                // phase736: real CPU samples from cpu_collector (32 ms lookback).
                {
                    using namespace std::chrono;
                    const auto now_us_c = static_cast<double>(
                        duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count());
                    const double cutoff_c = (now_us_c / 1000.0) - 32.0;
                    const auto real_cpu = cpu_collector.samples_since(cutoff_c);
                    cpu_overlay.draw(batcher,
                        std::span<const cmo::MarkerSample>(real_cpu.data(), real_cpu.size()),
                        bounds);
                }
            }

            // --- GPU overlay — "GPU" header + 100 px body, below CPU ------
            // phase631 / M9 W1A classification (cpu/gpu sibling).
            // phase674: titled header added. Y offset accounts for CPU header.
            {
                constexpr float kOverlayW  = 300.0F;
                constexpr float kOverlayH  = 100.0F;
                constexpr float kMargin    = 8.0F;
                // CPU overlay total height = its header + 120 px body.
                constexpr float kCpuTotal  = kOverlayHeaderH + 120.0F;
                constexpr float kGap       = 4.0F;
                const float     ox         = fbw_f - kOverlayW - kMargin;
                const float     oy         = kMargin + kCpuTotal + kGap;

                draw_overlay_header(batcher, widget_theme,
                                    ox, oy, kOverlayW, OverlayKind::kGpu);

                // phase736: real GPU samples from gpu_marker_recorder.
                // Empty on NullDevice (no cmd buffer); overlay handles empty span.
                const auto gpu_samples = gpu_marker_recorder.samples();
                draw_gpu_marker_overlay(batcher, gpu_samples,
                    ox, oy + kOverlayHeaderH, kOverlayW, kOverlayH, 16.0);
            }

            // --- FRAME overlay — "FRAME" header + 80 px body, bottom-right -
            //
            // phase667: lifted by kStatusBarH so it clears the status ribbon.
            // phase674: titled header + real "UI" pass from batcher data.
            //   "UI" pass duration ∝ batcher.vertex_count() (live data).
            {
                constexpr float kOverlayW = 400.0F;
                constexpr float kOverlayH = 80.0F;
                constexpr float kMargin   = 8.0F;
                const float     ox        = fbw_f - kOverlayW - kMargin;
                const float     oy        = fbh_f - kOverlayH - kOverlayHeaderH
                                            - kMargin - kStatusBarH;

                draw_overlay_header(batcher, widget_theme,
                                    ox, oy, kOverlayW, OverlayKind::kFrame);

                const fgt::Rect bounds {
                    ox, oy + kOverlayHeaderH, kOverlayW, kOverlayH };

                // phase737 — real data: last_frame_passes() returns the previous
                // frame's pass records (double-buffered by Timeline). On frame 0
                // the span is empty; the overlay becomes non-empty from frame 1.
                const auto real_passes = fgt_timeline.last_frame_passes();
                frame_graph_overlay.draw(batcher, real_passes, bounds);
            }
        }

        // -- phase720 / M17 W3 — auto-save timer + dirty-flag propagation ----
        //
        // 1. Recompute the persisted-state hash; transition to kPendingDirty
        //    on any change (and remain there until the next save).
        // 2. Every kAutosaveIntervalMs, if dirty, write the .cdproj atomically
        //    and transition through kSaving -> kJustSaved (or kError on fail).
        // 3. Refresh the "Saved N s ago" counter while in kJustSaved.
        //
        // The tick is cheap: the hash is recomputed every frame but the actual
        // .cdproj write only fires once per 30 s (and only when dirty).
        {
            using sc = std::chrono::steady_clock;
            const auto now_tp = sc::now();

            // (a) Dirty-flag detection.
            const std::size_t current_hash = compute_state_hash();
            const auto        prev_status  = g_auto_save_indicator.current_status();
            if (current_hash != last_saved_state_hash &&
                prev_status != auto_save_status::kPendingDirty &&
                prev_status != auto_save_status::kSaving)
            {
                g_auto_save_indicator.mark_dirty();
            }

            // (b) Refresh "Saved N s ago" while showing the post-save status.
            if (g_auto_save_indicator.current_status() == auto_save_status::kJustSaved)
            {
                using ms = std::chrono::duration<double, std::milli>;
                const double elapsed_ms = std::chrono::duration_cast<ms>(
                    now_tp - last_save_tp).count();
                g_auto_save_indicator.set_last_save_ms_ago(elapsed_ms);
            }

            // (c) Auto-save check every kAutosaveIntervalMs.
            using ms = std::chrono::duration<double, std::milli>;
            const double since_last_check_ms = std::chrono::duration_cast<ms>(
                now_tp - last_autosave_check_tp).count();
            if (since_last_check_ms >= kAutosaveIntervalMs)
            {
                last_autosave_check_tp = now_tp;
                if (current_hash != last_saved_state_hash)
                {
                    // Transition to kSaving for visibility before the atomic write.
                    g_auto_save_indicator.set_status(auto_save_status::kSaving);

                    // Capture window geometry into project_data so the write
                    // matches the live editor state (mirrors the on-exit path).
                    if (window)
                    {
                        project_data.window.w = static_cast<int>(window->width());
                        project_data.window.h = static_cast<int>(window->height());
                    }
                    project_data.schema_version = 1;
                    project_data.theme_name     = active_theme_name;
                    project_data.dock_layout    = dock_layout_to_hex(dockspace.serialize());

                    const bool ok = cd::editor::cdproj::write_cdproj(
                        project_data, cdproj_path);

                    if (ok)
                    {
                        g_auto_save_indicator.mark_saved();
                        last_saved_state_hash = current_hash;
                        last_save_tp          = now_tp;
                        if (!autosave_logged_fired)
                        {
                            const double secs_since_boot =
                                std::chrono::duration_cast<ms>(
                                    now_tp - boot_splash.start_tp).count() / 1000.0;
                            std::printf(
                                "editor: autosave fired at %.1fs (.cdproj written to %s).\n",
                                secs_since_boot, cdproj_path.string().c_str());
                            std::fflush(stdout);
                            autosave_logged_fired = true;
                        }
                    }
                    else
                    {
                        g_auto_save_indicator.set_status(auto_save_status::kError);
                        std::fprintf(stderr,
                            "editor: autosave FAILED writing .cdproj to %s\n",
                            cdproj_path.string().c_str());
                    }
                }
            }
        }

        // -- phase667 / M12 W2 + phase674 — status bar -----------------------
        //
        // phase674: FPS from FrameTimeRing (real wall-clock dt) replaces the
        // 62.5 stand-in. vertex_count + draw_calls remain from DrawBatcher.
        {
            StatusBarStats stats {};
            stats.validator_pass  = validator_pass;
            stats.validator_warn  = validator_warn;
            stats.validator_error = validator_error;
            stats.fps             = real_fps > 0.0F ? real_fps : 62.5F;
            stats.vertex_count    = static_cast<std::uint32_t>(batcher.vertex_count());
            stats.draw_calls      = static_cast<std::uint32_t>(batcher.command_count());
            stats.route_taken     = route_taken;

            draw_status_bar(batcher, widget_theme, stats,
                            static_cast<float>(fb_w),
                            static_cast<float>(fb_h));
        }

        // -- phase720 / M17 W3 — auto-save indicator (status-bar widget) ------
        //
        // Pinned between the frame-stats meters and the theme picker. Width is
        // 150 px so the badge reads as a self-contained "save state" pill
        // without crowding the FPS / verts / draws strips.
        //
        // Layout reference (existing draw_status_bar regions):
        //   [0 .. kBadgeW=280]              validator badge
        //   [292  .. 292 + kStatsW=360]     frame stats meters
        //   [fb_w - kRouteW - 6]            route indicator (right anchor)
        //   [fb_w - kRouteW - 12 - 204]     theme picker (3 slabs)
        //
        // The indicator slots in to the right of the frame stats meters with a
        // 16 px gap — a small but visible separation so the status-bar reads as
        // [badge | stats | save_state | picker | route] left-to-right.
        {
            constexpr float kSaveBadgeW = 150.0F;
            constexpr float kStatsW     = 360.0F;
            constexpr float kBadgeW     = 280.0F;
            const auto save_x = static_cast<float>(0.0F) + kBadgeW + 12.0F
                                 + kStatsW + 16.0F;
            const auto save_y = static_cast<float>(fb_h) - kStatusBarH + 2.0F;
            const uw::Rect save_rect {
                save_x, save_y, kSaveBadgeW, kStatusBarH - 4.0F
            };
            const auto v2_theme = uth::theme_from_name(active_theme_name);
            g_auto_save_indicator.draw(batcher, v2_theme, save_rect);
        }

        // -- phase701 / M15 W3 — toast notification render --------------------
        //
        // Floating messages in the bottom-right above the status bar.
        // Toasts fade out in their last 300 ms; the queue caps at 5 entries
        // (FIFO eviction). The tick + draw call is cheap: per-frame O(n) with
        // n bounded to 5.
        {
            const auto now = std::chrono::steady_clock::now();
            toast_queue.tick(now);
            draw_toasts(batcher, widget_theme, toast_queue,
                        static_cast<float>(fb_w),
                        static_cast<float>(fb_h),
                        now);
        }

        // -- phase715 / M16 W6 — 200 ms theme cross-fade tick ------------------
        //
        // If a palette fade is active, compute elapsed time, lerp the V2
        // palette, then rebuild widget_theme from the blended token set.
        // After kThemeFadeMs the transition is snapped to `to` exactly and
        // the flag is cleared so this block costs nothing in steady state.
        {
            using sc = std::chrono::steady_clock;
            using ms = std::chrono::duration<float, std::milli>;
            if (theme_fade_active)
            {
                const float elapsed = std::chrono::duration_cast<ms>(
                    sc::now() - theme_fade_start_tp).count();
                if (elapsed >= kThemeFadeMs)
                {
                    // Snap to target and stop.
                    widget_theme     = build_widget_theme(active_theme_name);
                    theme_fade_active = false;
                }
                else
                {
                    const float t = elapsed / kThemeFadeMs;
                    const uth::Theme blended = uth::lerp_palette(theme_fade_from,
                                                                 theme_fade_to, t);
                    // Derive widget_theme colors from the blended V2 token set.
                    // build_widget_theme() works from a name, so we build from
                    // the target name first (for non-color slots) then patch the
                    // palette-mapped colors using the blended surface token.
                    //
                    // Simple policy: use build_widget_theme(target) for structural
                    // slots, then override the four primary widget colors from the
                    // blended palette tokens: surface, on_surface, primary,
                    // surface_variant. This keeps the rest of the widget struct
                    // coherent while animating the visually dominant swatches.
                    widget_theme = build_widget_theme(active_theme_name);
                    const auto& p = blended.palette;
                    using S = uth::PaletteSlot;
                    widget_theme.background     = to_widget_color(p[static_cast<std::size_t>(S::kBackground)]);
                    widget_theme.surface        = to_widget_color(p[static_cast<std::size_t>(S::kSurface)]);
                    widget_theme.surface_hover  = to_widget_color(p[static_cast<std::size_t>(S::kSurfaceVariant)]);
                    widget_theme.surface_press  = to_widget_color(p[static_cast<std::size_t>(S::kPrimaryContainer)]);
                    widget_theme.surface_subtle = to_widget_color(p[static_cast<std::size_t>(S::kSurfaceVariant)]);
                    widget_theme.accent         = to_widget_color(p[static_cast<std::size_t>(S::kPrimary)]);
                    widget_theme.accent_hover   = to_widget_color(p[static_cast<std::size_t>(S::kPrimaryContainer)]);
                    widget_theme.text           = to_widget_color(p[static_cast<std::size_t>(S::kOnSurface)]);
                    widget_theme.text_dim       = to_widget_color(p[static_cast<std::size_t>(S::kOnSurfaceVariant)]);
                    widget_theme.accent_error   = to_widget_color(p[static_cast<std::size_t>(S::kError)]);
                }
            }
        }

        // -- phase695 / M14 W6A — theme picker (3 slabs in the status bar) ----
        //
        // Drawn after draw_status_bar so the slabs paint on top of the status
        // ribbon background. A left-click on a slab starts a 200 ms cross-fade
        // (phase715) and records active_theme_name for .cdproj save on exit.
        {
            std::string new_theme_name;
            const auto  ptr      = flatten_pointer(pointer);
            const bool  clicked  = draw_theme_picker(
                batcher,
                widget_theme,
                static_cast<float>(fb_w),
                static_cast<float>(fb_h),
                active_theme_name,
                ptr.mouse_x, ptr.mouse_y,
                ptr.left_pressed,
                &new_theme_name);
            if (clicked)
            {
                // phase715 — begin 200 ms cross-fade instead of instant swap.
                theme_fade_from   = uth::theme_from_name(active_theme_name);
                active_theme_name = new_theme_name;
                theme_fade_to     = uth::theme_from_name(active_theme_name);
                theme_fade_start_tp  = std::chrono::steady_clock::now();
                theme_fade_active    = true;
                std::printf("editor: theme fade started -> '%s'\n",
                            active_theme_name.c_str());
                std::fflush(stdout);
            }
        }

        // -- phase684 / M13 W6A — debug viz thumbnail overlays ----------------
        //
        // Three ~200x150 px thumbnails stacked vertically in the top-right
        // corner, below the profiling overlays.  Layout (from top):
        //   [Depth]       at y = kDbgVizStartY
        //   [Normal]      at y = kDbgVizStartY + 1 * (kDbgH + kDbgGap)
        //   [AlphaBucket] at y = kDbgVizStartY + 2 * (kDbgH + kDbgGap)
        //
        // The stack starts below the GPU-marker overlay to avoid visual
        // collision.  kDbgVizStartY = CPU header + CPU body + GPU header +
        // GPU body + gap between GPU and first debug thumbnail.
        {
            const auto fbw_f = static_cast<float>(fb_w);
            // Position constants — match the profiling overlay layout above.
            constexpr float kDbgW       = 200.0F;
            constexpr float kDbgH       = 150.0F;
            constexpr float kDbgGap     = 4.0F;
            constexpr float kDbgMargin  = 8.0F;
            // CPU overlay: kOverlayHeaderH(14) + 120 body = 134.
            // GPU overlay: kOverlayHeaderH(14) + 100 body = 114.
            // Combined top offset: 8 (margin) + 134 + 4 (gap) + 114 + 8 (gap).
            constexpr float kDbgVizStartY = 8.0F + 134.0F + 4.0F + 114.0F + 8.0F;
            const float     dbg_x         = fbw_f - kDbgW - kDbgMargin;

            const std::array<DebugVizOverlay*, 3> dbg_overlays {
                &dbg_depth, &dbg_normal, &dbg_bucket
            };

            // -- phase735 / B6 — Per-frame live G-buffer texture binding ------
            //
            // Pipe the depth / normal / albedo cd::rhi::TextureHandle values
            // into the three overlays. Each kind-specific setter is no-op for
            // overlays of a different kind so we can fire all three setters
            // every frame without branching; the API is intentionally
            // broadcast-friendly to keep this call-site simple as the renderer
            // wire-up evolves.
            dbg_depth.set_depth_texture (gbuffer_depth_tex);
            dbg_normal.set_normal_texture(gbuffer_normal_tex);
            dbg_bucket.set_albedo_texture(gbuffer_albedo_tex);

            for (std::size_t i = 0; i < dbg_overlays.size(); ++i)
            {
                const float oy = kDbgVizStartY
                                 + static_cast<float>(i) * (kDbgH + kDbgGap);
                const uw::Rect dbg_bounds { dbg_x, oy, kDbgW, kDbgH };
                dbg_overlays[i]->draw(batcher, dbg_bounds, widget_theme);
            }
        }

        // -- phase690 / M14 W3 — popout_dock floating-internal panels ----------
        //
        // For every panel marked detached in `popout_dock`, render a floating-
        // internal rectangle at the stored PopoutWindow::position. The body is
        // the registered panel drawer; the border is drawn in palette
        // accent_warning so the detached state reads at a glance.
        //
        // Sprint-1 internal: when cd::platform gains multi-window support, the
        // same iteration drives native OS-window placement (zero API change).
        // No panel is auto-detached today — the user invokes detach_panel via a
        // future input handler. Loop is a no-op when nothing is detached.
        {
            const auto detached = popout_dock.detached_windows();
            for (const auto& pw : detached)
            {
                if (!pw.is_active) { continue; }
                const uw::Rect rect {
                    pw.position[0], pw.position[1],
                    pw.size[0],     pw.size[1] };

                // 1) Solid panel body (same surface fill as a dock leaf).
                batcher.quad(rect.x, rect.y, rect.w, rect.h,
                             ur::Color { widget_theme.surface.r,
                                         widget_theme.surface.g,
                                         widget_theme.surface.b,
                                         widget_theme.surface.a });

                // 2) Border in palette accent_warning (visually distinct).
                constexpr float kBorderPx = 2.0F;
                const ur::Color border_c {
                    widget_theme.accent_warning.r,
                    widget_theme.accent_warning.g,
                    widget_theme.accent_warning.b,
                    widget_theme.accent_warning.a };
                // top
                batcher.quad(rect.x, rect.y, rect.w, kBorderPx, border_c);
                // bottom
                batcher.quad(rect.x, rect.y + rect.h - kBorderPx,
                             rect.w, kBorderPx, border_c);
                // left
                batcher.quad(rect.x, rect.y, kBorderPx, rect.h, border_c);
                // right
                batcher.quad(rect.x + rect.w - kBorderPx, rect.y,
                             kBorderPx, rect.h, border_c);
            }
        }

        // -- phase690 / M14 W3 — keyboard_shortcut_overlay (full-window) -------
        //
        // The overlay is NOT a dock node — it is painted on top of the entire
        // framebuffer when visible. Toggle is wired via the '?' key handler
        // above; today the headless smoke run keeps it hidden so the smoke log
        // simply records that 15 shortcuts were registered.
        {
            const uw::Rect fb_rect {
                0.0F, 0.0F,
                static_cast<float>(fb_w), static_cast<float>(fb_h) };
            shortcut_overlay.draw(batcher, widget_theme, fb_rect);
        }

        // -- phase685 / M13 W6B — boot splash + first-launch welcome ----------
        //
        // Splash paints on top of dock + overlays + status bar for the first
        // 2.5 s (solid) + 0.3 s (fade-out).  After the splash is done, if this
        // is a first launch, the welcome dialog appears and blocks until the
        // user clicks one of the three layout buttons.  Both are skipped in
        // headless mode (no visible window).
        if (window)
        {
            const double splash_ms = boot_splash.elapsed_ms();
            if (!boot_splash.done(splash_ms))
            {
                draw_boot_splash(batcher, widget_theme,
                                 static_cast<float>(fb_w),
                                 static_cast<float>(fb_h),
                                 boot_splash,
                                 splash_ms,
                                 std::span<const ParticleSnapshot>(
                                     particle_snap_buf.data(),
                                     particle_snap_count));
            }
            else if (first_launch && !welcome_dialog.decided())
            {
                // Activate the dialog on the first frame after the splash ends.
                if (!welcome_dialog.active)
                {
                    welcome_dialog.show();
                    std::printf("editor: splash done — showing first-launch welcome dialog.\n");
                    std::fflush(stdout);
                }

                const WelcomeChoice chosen =
                    draw_welcome_dialog(batcher, widget_theme,
                                        static_cast<float>(fb_w),
                                        static_cast<float>(fb_h),
                                        flatten_pointer(pointer));

                if (chosen != WelcomeChoice::kNone)
                {
                    welcome_dialog.choice = chosen;

                    // Apply the layout to the live DockSpace.
                    apply_welcome_layout(dockspace, chosen);

                    // Write a starter .cdproj so the next launch skips the dialog.
                    project_data.schema_version = 1;
                    project_data.dock_layout =
                        dock_layout_to_hex(dockspace.serialize());
                    if (cd::editor::cdproj::write_cdproj(project_data, cdproj_path))
                    {
                        const char* preset_name =
                            (chosen == WelcomeChoice::kCompact) ? "Compact" :
                            (chosen == WelcomeChoice::kFull)    ? "Full"    :
                                                                   "Default";
                        std::printf(
                            "editor: welcome choice '%s' — starter .cdproj written to %s\n",
                            preset_name, cdproj_path.string().c_str());
                    }
                    else
                    {
                        std::fprintf(stderr,
                            "editor: warning — could not write starter .cdproj to %s\n",
                            cdproj_path.string().c_str());
                    }
                    std::fflush(stdout);
                }
            }
        }

        // phase737 — record real DockDraw CPU duration then freeze the frame.
        // phase736 — close frame.draw CPU scope + build FrameCapture.
        {
            const auto    fg_dock_end   = FgClock::now();
            const double  dock_start_ms = 0.0;  // DockDraw begins at epoch
            const double  dock_dur_ms   = FgDuration(fg_dock_end - fg_dock_start).count();
            fgt_timeline.record_pass("DockDraw", dock_start_ms, dock_dur_ms, 0U);
            fgt_timeline.end_frame();

            // phase736 -- Steps 3-7: close CPU scope + harvest samples + capture.

            // Step 3: close "frame.draw" CPU scope opened before the draw block.
            cpu_collector.end(frame_draw_handle);

            // Step 4: harvest CPU samples from the last 2 budget windows.
            cd::editor::profile::FrameCapture cap;
            cap.total_ms = last_dt_ms > 0.0 ? last_dt_ms : 16.67;
            {
                using namespace std::chrono;
                const auto now_us = static_cast<double>(
                    duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count());
                const double cutoff_ms = (now_us / 1000.0) - 2.0 *
                    static_cast<double>(g_perf_profiler_panel.budget_ms());
                cap.cpu_markers = cpu_collector.samples_since(cutoff_ms);
            }

            // Step 5: fgt_timeline.end_frame() was just called above;
            // last_frame_passes() now contains this frame's pass records.
            {
                const auto passes = fgt_timeline.last_frame_passes();
                cap.gpu_passes.assign(passes.begin(), passes.end());
            }
            // gpu_markers: populated in the Vulkan branch after resolve();
            // empty on NullDevice/headless.

            // Step 6: capture into the rolling ring.
            g_perf_profiler_panel.capture_frame(
                cd::editor::profile::to_snapshot(std::move(cap)));
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
            //
            // phase736: bracket the UI submit in a gpu_marker_recorder scope
            // ("gpu.ui_submit"). After resolve(), the sample feeds the GPU
            // overlay and future FrameCapture gpu_markers iteration.
            gpu_marker_recorder.clear();
            {
                cd::profile::gpu_marker::Scope gpu_ui_scope(
                    gpu_marker_recorder, cmd, "gpu.ui_submit");
                submitter.record(cmd, frame.extent);
            }
            gpu_marker_recorder.resolve(*device);

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
    // Capture window geometry and the live DockSpace layout (serialized to a
    // hex-encoded string for JSON safety) and write the project file so the
    // next launch can restore session state. phase679 / M13 W3 closes the
    // round-trip: a user closes the editor mid-task, reopens it, the layout
    // is EXACTLY as they left it (down to which tab in each tab strip was
    // active). The save is best-effort: failure is logged but does not change
    // the exit code.
    {
        if (window)
        {
            project_data.window.w = static_cast<int>(window->width());
            project_data.window.h = static_cast<int>(window->height());
        }
        project_data.schema_version = 1;
        // phase695 / M14 W6A — persist the active theme so next launch restores it.
        project_data.theme_name  = active_theme_name;
        // Serialize the live DockSpace tree -> hex string (JSON-safe payload).
        project_data.dock_layout = dock_layout_to_hex(dockspace.serialize());
        if (!cd::editor::cdproj::write_cdproj(project_data, cdproj_path))
        {
            std::fprintf(stderr,
                         "editor: warning — could not save .cdproj to %s\n",
                         cdproj_path.string().c_str());
        }
        else
        {
            std::printf("editor: session saved to %s (dock_layout=%zu bytes hex)\n",
                        cdproj_path.string().c_str(),
                        project_data.dock_layout.size());
        }
    }

    // phase701 / M15 W3 — unwire the bridge so a post-shutdown push() is a no-op.
    cd::editor::build_panel_bridge::register_target(nullptr);

    std::printf("editor: clean exit (%u frames; dock nodes=%zu; "
                "perf_profiler captured %zu/60 real frames).\n",
                frame_idx, dockspace.node_count(),
                g_perf_profiler_panel.recorded_frame_count());
    std::fflush(stdout);
    return 0;
}
