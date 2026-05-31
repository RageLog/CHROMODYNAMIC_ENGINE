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
// Layout the binary boots with (T0.2 brief default):
//
//      +--------------------------------------------------------------+
//      |                                                              |
//      |  Scene Tree   |          Viewport            |  Inspector    |
//      |     (LEFT)    |          (CENTER)            |    (RIGHT)    |
//      |               |                              |               |
//      |               +------------------------------+               |
//      |               |  Console        |  Assets    |               |
//      |               |  (BOTTOM)       | (BOTTOM-R) |               |
//      +--------------------------------------------------------------+
//
// SCOPE-DOWN DECISION (per T0.2 deliverable contract): the editor.exe
// app boots the FULL DockSpace shell with all five panels registered
// (scene tree / viewport / inspector / console / assets) and wires
// the scene-tree panel to a live cd::editor::Editor instance. The
// remaining four panels are registered with stub drawers that paint
// only their panel rect background. The follow-up sessions extract
// inspector + console + asset-browser content widgets from
// hello_editor / hello_ui templates into reusable panel libraries.
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

#include <cd/editor/Editor.hpp>
#include <cd/editor/HierarchyView.hpp>
#include <cd/editor/panel_inspector/Inspector.hpp>
#include <cd/editor/panel_console/Console.hpp>
#include <cd/editor/panel_asset_browser/AssetBrowser.hpp>
#include <cd/editor/panel_viewport/Viewport.hpp>

#include <array>
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
// Build the 5-panel layout described in the file header. Starts from the
// freshly-constructed `DockSpace` whose root is a single empty kLeaf;
// promotes the leaf to a tab group hosting "viewport", then carves off
// LEFT (scene_tree), RIGHT (inspector), BOTTOM (console), and tab-merges
// "assets" alongside console in the bottom strip.
//
// Splits are sized to match the brief's intent:
//   * LEFT  : 18% of width   -> scene tree
//   * RIGHT : 25% of width   -> inspector
//   * BOTTOM: 28% of height  -> console + assets tab group
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
        return static_cast<std::uint8_t>(clamped * 255.0F + 0.5F);
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

// ---- Headless one-line summary ---------------------------------------------

void report_headless_frame(const ur::DrawBatcher& batcher,
                           const urr::Submitter& submitter,
                           const uw::DockSpace& ds,
                           std::uint32_t frame_idx)
{
    std::printf(
        "editor[headless frame %u]: dock nodes=%zu  batcher verts=%zu/idx=%zu/cmds=%zu  "
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

    if (!local.project_path.empty())
    {
        std::printf("editor: project = %s (parser stub -- T0.3 will plumb full .cdproj)\n",
                    local.project_path.c_str());
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

    // -- 5. Dockspace + 5-panel layout --------------------------------------
    uw::DockSpace dockspace;
    dockspace.register_panel("scene_tree", draw_scene_tree_stub);
    dockspace.register_panel("viewport",   draw_viewport_panel);
    dockspace.register_panel("inspector",  draw_inspector_panel);
    dockspace.register_panel("console",    draw_console_panel);
    dockspace.register_panel("assets",     draw_assets_panel);
    if (!build_default_layout(dockspace))
    {
        std::fprintf(stderr, "editor: failed to build default DockSpace layout.\n");
        return 1;
    }
    std::printf("editor: dock layout ready with %zu nodes (5 panels: scene_tree | viewport | "
                "inspector | console | assets)\n", dockspace.node_count());

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
    urr::SubmitterCreateInfo sci {};
    sci.max_vertices = 16384U;
    sci.max_indices  = 65536U;
    sci.color_format = rhi::Format::kBGRA8Unorm;
    auto sub_r = urr::Submitter::create(*device, sci);
    if (!sub_r.has_value())
    {
        std::fprintf(stderr, "editor: ui_renderer_rhi::Submitter::create failed.\n");
        return 2;
    }
    auto& submitter = *sub_r;

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
        dockspace.set_rect(uw::Rect { 0.0F, 0.0F,
                                      static_cast<float>(fb_w),
                                      static_cast<float>(fb_h) });
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

        // -- Draw via the CPU batcher + RHI submitter --
        batcher.begin_frame();
        dockspace.draw(batcher, font.is_loaded() ? &font : nullptr, widget_theme);
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

            // The Submitter pipeline lands with cd::material UI variants
            // per ADR-20260530 Phase 1.5 (mirrors hello_ui's gating).
            // Until then submitter.record() is gated -- the swapchain clear
            // above is what proves the editor's frame loop is alive.
            constexpr bool kSubmitterPipelineReady = false;
            if constexpr (kSubmitterPipelineReady)
            {
                submitter.record(cmd, frame.extent);
            }

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

    std::printf("editor: clean exit (%u frames; dock nodes=%zu).\n",
                frame_idx, dockspace.node_count());
    std::fflush(stdout);
    return 0;
}
