// =============================================================================
// CHROMODYNAMIC — samples/editor/hello_behavior_designer
//
// phase592-hello-behavior-designer-sample
//
// Visual sample for cd::editor::panel::behavior_designer::BehaviorDesigner.
// Demonstrates an editor panel sample built entirely on cd::ui_*
// (no ImGui dependency).
//
// What this sample demonstrates:
//   * Opens a Vulkan window via cd::platform.
//   * Creates a cd::ui::renderer_rhi::Submitter via the M3 W1A
//     create_with_inline_shader() factory (Route B inline GLSL pipeline).
//   * Constructs a cd::editor::panel::behavior_designer::BehaviorDesigner,
//     binds a demo BT root node, selects a child node, and sets a pan
//     offset — so the panel renders in a non-trivial state on the first frame.
//   * Render loop:
//       batcher.begin_frame()
//       panel.draw(batcher, theme, full_window_rect)
//       submitter.upload(batcher)
//       [begin_render_pass → submitter.record(cmd, extent) → end_render_pass]
//       renderer.end_frame()
//   * 60-frame headless mode (--headless / Runtime::should_continue) then
//     exits with code 0.  ESC also triggers an early clean exit.
//
// ImGui-free: this sample imports NO imgui headers. All UI is driven via
// cd::ui_renderer DrawBatcher quads emitted by BehaviorDesigner::draw().
// =============================================================================

#include <SampleRuntime.hpp>

#include <cd/platform/Window.hpp>
#include <cd/render/Renderer.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi/vulkan/VulkanDevice.hpp>
#include <cd/ui/font/Font.hpp>
#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/renderer_rhi/Submitter.hpp>
#include <cd/ui/theme/Theme.hpp>
#include <cd/ui/widgets/Widgets.hpp>

#include <cd/editor/panel_behavior_designer/BehaviorDesigner.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <vector>

// ---- Theme bridge (mirrors apps/editor pattern) ----------------------------

namespace
{

[[nodiscard]] cd::ui::widgets::Color to_widget_color(cd::ui::theme::ColorToken c) noexcept
{
    auto pack = [](float v) noexcept -> std::uint8_t {
        const float clamped = (v < 0.0F) ? 0.0F : (v > 1.0F) ? 1.0F : v;
        return static_cast<std::uint8_t>(std::lround(clamped * 255.0F));
    };
    return cd::ui::widgets::Color { pack(c.r), pack(c.g), pack(c.b), pack(c.a) };
}

[[nodiscard]] cd::ui::widgets::Theme build_dark_widget_theme()
{
    const auto dark = cd::ui::theme::kDarkTheme();
    cd::ui::widgets::Theme t {};
    t.background    = to_widget_color(dark.color(cd::ui::theme::PaletteSlot::kBackground));
    t.surface       = to_widget_color(dark.color(cd::ui::theme::PaletteSlot::kSurface));
    t.surface_hover = to_widget_color(dark.color(cd::ui::theme::PaletteSlot::kSurfaceVariant));
    t.surface_press = to_widget_color(dark.color(cd::ui::theme::PaletteSlot::kPrimaryContainer));
    t.accent        = to_widget_color(dark.color(cd::ui::theme::PaletteSlot::kPrimary));
    t.accent_hover  = to_widget_color(dark.color(cd::ui::theme::PaletteSlot::kPrimaryContainer));
    t.text          = to_widget_color(dark.color(cd::ui::theme::PaletteSlot::kOnSurface));
    t.text_dim      = to_widget_color(dark.color(cd::ui::theme::PaletteSlot::kOnSurfaceVariant));
    return t;
}

// ---- Optional font probe (same candidates as apps/editor) ------------------

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

}  // namespace

// ===========================================================================
// main
// ===========================================================================
int main(int argc, char** argv)
{
    const cd::sample::Runtime runtime = cd::sample::parse_runtime(argc, argv);

    // ---- Font (optional; widgets gracefully fall back when missing) ---------
    cd::ui::font::Font font;
    {
        const auto ttf = find_system_font();
        if (!ttf.empty() &&
            font.load_ttf_in_memory(std::span<const std::uint8_t>(ttf.data(), ttf.size())))
        {
            static constexpr float         kPixelSize = 15.0F;
            static constexpr std::uint32_t kAtlasDim  = 1024U;
            (void)font.rasterize_range(0x0020U, 0x00FFU, kPixelSize, kAtlasDim);
        }
    }

    // ---- Theme ---------------------------------------------------------------
    const cd::ui::widgets::Theme widget_theme = build_dark_widget_theme();

    // ---- BehaviorDesigner panel ---------------------------------------------
    //
    // Seed with a non-trivial BT state:
    //   root_id = 1 (a Selector node)
    //   selected = 2 (Child A — Sequence node) → accent border will be drawn
    //   pan_offset = (20, 10) → view is slightly panned
    namespace bd = cd::editor::panel::behavior_designer;
    bd::BehaviorDesigner panel;
    panel.set_tree_root(1U);          // bind a demo BT root
    panel.set_selected(2U);           // select Child A (accent highlight)
    panel.set_pan_offset(20.0F, 10.0F);

    std::printf("[hello_behavior_designer] BehaviorDesigner seeded:\n"
                "  tree_root = %u\n"
                "  selected  = %u\n"
                "  pan       = (%.1f, %.1f)\n",
                panel.tree_root(),
                panel.selected(),
                static_cast<double>(panel.pan_x()),
                static_cast<double>(panel.pan_y()));
    std::fflush(stdout);

    // ---- Window --------------------------------------------------------------
    cd::platform::WindowDesc wd {};
    wd.title  = "CHROMODYNAMIC — hello_behavior_designer (phase592)";
    wd.width  = 900U;
    wd.height = 640U;
    auto window_r = cd::platform::create_window(wd);
    if (!window_r.has_value())
    {
        std::fprintf(stderr, "[hello_behavior_designer] platform::create_window failed.\n");
        return 1;
    }
    auto& window = **window_r;

    // ---- RHI device ---------------------------------------------------------
    cd::rhi::vulkan::VulkanCreateInfo vci {};
    auto device_r = cd::rhi::vulkan::create_vulkan_device(vci);
    if (!device_r.has_value())
    {
        std::fprintf(stderr, "[hello_behavior_designer] create_vulkan_device failed.\n");
        return 2;
    }
    auto& device = **device_r;

    // ---- Renderer (swapchain) -----------------------------------------------
    cd::render::RendererDesc rd {};
    rd.device                   = &device;
    rd.swapchain.window_handle  = window.native_window_handle();
    rd.swapchain.display_handle = window.native_display_handle();
    rd.swapchain.extent         = { window.width(), window.height() };
    rd.swapchain.format         = cd::rhi::Format::kBGRA8Unorm;
    rd.frames_in_flight         = 2U;
    auto renderer_r = cd::render::Renderer::create(rd);
    if (!renderer_r.has_value())
    {
        std::fprintf(stderr, "[hello_behavior_designer] Renderer::create failed.\n");
        return 3;
    }
    auto& renderer = *renderer_r;

    // ---- UI Submitter — M3 W1A Route B inline GLSL --------------------------
    cd::ui::renderer_rhi::SubmitterCreateInfo sci {};
    sci.color_format = cd::rhi::Format::kBGRA8Unorm;
    sci.max_vertices = 16384U;
    sci.max_indices  = 65536U;
    auto sub_r = cd::ui::renderer_rhi::Submitter::create_with_inline_shader(device, sci);
    if (!sub_r.has_value())
    {
        std::fprintf(stderr,
                     "[hello_behavior_designer] Submitter::create_with_inline_shader failed.\n");
        return 4;
    }
    auto& submitter = *sub_r;
    std::printf("[hello_behavior_designer] Submitter ready (Route B / inline GLSL). ESC to exit.\n");
    std::fflush(stdout);

    // ---- Frame loop ----------------------------------------------------------
    cd::ui::renderer::DrawBatcher      batcher;
    std::vector<cd::platform::OSEvent> events;
    events.reserve(64);

    bool          needs_rebuild { false };
    std::uint32_t frame_idx     { 0U };

    while (true)
    {
        if (!runtime.should_continue(frame_idx))
            window.request_close();

        events.clear();
        if (!window.pump_events(events))
            break;

        for (const auto& e : events)
        {
            if (e.kind == cd::platform::OSEventKind::kKeyDown &&
                e.key  == cd::platform::KeyCode::kEscape)
            {
                window.request_close();
            }
            else if (e.kind == cd::platform::OSEventKind::kResize)
            {
                needs_rebuild = true;
            }
        }

        if (needs_rebuild)
        {
            if (window.width() == 0U || window.height() == 0U) { continue; }
            if (!renderer.recreate_swapchain(
                    { window.width(), window.height() }).has_value())
            {
                continue;
            }
            needs_rebuild = false;
        }

        // ---- CPU: emit DrawBatcher commands ----------------------------------
        //
        // The full-window rect is passed to BehaviorDesigner::draw() so the
        // panel fills the entire framebuffer (no DockSpace required).
        const cd::ui::widgets::Rect full_rect {
            0.0F,
            0.0F,
            static_cast<float>(window.width()),
            static_cast<float>(window.height())
        };

        batcher.begin_frame();
        panel.draw(batcher, widget_theme, full_rect);
        (void)submitter.upload(batcher);

        // ---- GPU: acquire swapchain image + render pass + record + present --
        auto frame_r = renderer.begin_frame();
        if (!frame_r.has_value())
        {
            if (frame_r.error().code ==
                static_cast<std::uint32_t>(
                    cd::render::render_errors::Code::kSwapchainOutOfDate))
            {
                needs_rebuild = true;
                continue;
            }
            std::fprintf(stderr, "[hello_behavior_designer] begin_frame failed.\n");
            return 5;
        }
        auto& frame = *frame_r;
        auto& cmd   = *frame.command_buffer;

        std::array<cd::rhi::ColorAttachmentInfo, 1> color_attach {
            cd::rhi::ColorAttachmentInfo {
                .view        = frame.swapchain_image_view,
                .load_op     = cd::rhi::LoadOp::kClear,
                .store_op    = cd::rhi::StoreOp::kStore,
                .clear_color = { .f32 = { 0.07F, 0.08F, 0.12F, 1.0F } }
            }
        };
        cd::rhi::RenderPassBeginInfo rp {};
        rp.render_area       = cd::rhi::Rect2D { { 0, 0 }, frame.extent };
        rp.color_attachments = color_attach;
        cmd.begin_render_pass(rp);
        cmd.set_viewport(cd::rhi::Viewport {
            0.0F, 0.0F,
            static_cast<float>(frame.extent.width),
            static_cast<float>(frame.extent.height),
            0.0F, 1.0F });
        cmd.set_scissor(cd::rhi::Rect2D { { 0, 0 }, frame.extent });

        // Submitter records the DrawBatcher's solid-quad draw commands into
        // the active render pass.
        submitter.record(cmd, frame.extent);

        cmd.end_render_pass();

        auto end_r = renderer.end_frame();
        if (!end_r.has_value())
        {
            if (end_r.error().code ==
                static_cast<std::uint32_t>(
                    cd::render::render_errors::Code::kSwapchainOutOfDate))
            {
                needs_rebuild = true;
                continue;
            }
            std::fprintf(stderr, "[hello_behavior_designer] end_frame failed.\n");
            return 6;
        }

        ++frame_idx;
    }

    renderer.wait_idle();

    std::printf("[hello_behavior_designer] clean exit (%u frames; batcher cmds last=%zu).\n",
                frame_idx, batcher.command_count());
    std::fflush(stdout);
    return 0;
}
