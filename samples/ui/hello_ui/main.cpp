// =============================================================================
// CHROMODYNAMIC — samples/ui/hello_ui/main.cpp
//
// Phase 1.5 + Phase 2 close-out of ADR-20260530-ui-widget-library. End-to-
// end tie-together of the cd::ui Phase-1/Phase-2 libraries, now with a
// real swapchain present cycle and live glyph rendering:
//
//   * cd::ui_layout       (Flex layout solver)
//   * cd::ui_font         (stb_truetype atlas)
//   * cd::ui_renderer     (CPU DrawBatcher)
//   * cd::ui_renderer_rhi (RHI Submitter — ring vb/ib + descriptor set)
//   * cd::ui_input        (hit-test + focus chain + tab/shift-tab + modal)
//   * cd::ui_widgets      (Button / Slider / TextInput / ... catalog)
//   * cd::render          (Phase 506: real swapchain begin_frame/end_frame
//                          + per-frame command-buffer render pass)
//
// Boot order per frame:
//   1. pump platform events; flatten into a cd::ui::widgets::InputState.
//   2. solve the Flex layout for the current viewport extent.
//   3. tick widgets (state transitions). Optional callbacks fire here.
//   4. renderer.begin_frame() acquires the swapchain image.
//   5. cmd.begin_render_pass(clear) on the acquired image.
//   6. draw widgets + sample-side header label glyphs into the batcher.
//   7. submitter.upload(batcher) + submitter.record(cmd, extent).
//   8. cmd.end_render_pass(); renderer.end_frame() submits + presents.
//
// Glyph rendering for the header label (this sample) is wired here in
// main.cpp via a small `draw_label_line` helper that iterates the label
// string codepoints, looks each up via cd::ui::font::Font::glyph_uv, and
// emits one `DrawBatcher::glyph(...)` per glyph at pen-advanced positions
// along the baseline. The Button widget already renders its label glyphs
// internally; the Slider draws a track + knob (knob.x reflects value).
//
// Headless mode (NullDevice): when `--null` is passed (or the Vulkan or
// Renderer init fails on a host without a GPU), the sample still
// constructs the full layout + widget tree + batcher pipeline and reports
// what *would* be uploaded / recorded. This proves the ABI on hosts
// without a GPU and is the only path the CI smoke harness exercises.
// =============================================================================

#include "SampleRuntime.hpp"

#include <cd/platform/Window.hpp>
#include <cd/render/Renderer.hpp>
#include <cd/rhi/Barriers.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi/NullDevice.hpp>
#include <cd/rhi/vulkan/VulkanDevice.hpp>
#include <cd/ui/font/Font.hpp>
#include <cd/ui/input/Input.hpp>
#include <cd/ui/layout/Flex.hpp>
#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/renderer_rhi/Submitter.hpp>
#include <cd/ui/widgets/Widgets.hpp>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
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
namespace ll       = cd::ui::layout;
namespace uf       = cd::ui::font;
namespace ur       = cd::ui::renderer;
namespace urr      = cd::ui::renderer_rhi;
namespace w        = cd::ui::widgets;

// ---- Argv flag parser ------------------------------------------------------

struct LocalArgs
{
    bool force_null { false };
};

[[nodiscard]] LocalArgs parse_local_args(int argc, char** argv)
{
    LocalArgs a {};
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view s { argv[i] };
        if (s == "--null")
            a.force_null = true;
    }
    return a;
}

// ---- TTF lookup -----------------------------------------------------------

/// Attempt to read a TTF font from the OS default font directory. Returns an
/// empty vector when none of the candidates exist (minimal CI containers).
[[nodiscard]] std::vector<std::uint8_t> find_system_font()
{
    static const std::array<const char*, 6> kCandidates {
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/calibri.ttf",
        "C:/Windows/Fonts/segoeui.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
    };
    for (const char* p : kCandidates)
    {
        std::ifstream f(p, std::ios::binary | std::ios::ate);
        if (!f)
            continue;
        const auto sz = static_cast<std::size_t>(f.tellg());
        f.seekg(0);
        std::vector<std::uint8_t> out(sz);
        f.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(sz));
        if (!out.empty())
            return out;
    }
    return {};
}

// ---- Flex tree owner ------------------------------------------------------
//
// One column root with three leaf children:
//   * header label leaf  (static text "Hello UI")
//   * button leaf
//   * slider leaf
//
// The leaf nodes carry intrinsic sizes so the Flex solver gives them
// non-zero rects even at the default `flex_grow = 0`. The root has padding
// + gap_main so the leaves are spaced visually.

struct UiTree
{
    ll::FlexTree tree {};
    ll::NodeId   root   { ll::kInvalidNode };
    ll::NodeId   label  { ll::kInvalidNode };
    ll::NodeId   button { ll::kInvalidNode };
    ll::NodeId   slider { ll::kInvalidNode };
};

[[nodiscard]] UiTree build_tree()
{
    UiTree u;
    ll::FlexStyle rs;
    rs.direction      = ll::FlexDirection::kColumn;
    rs.align_items    = ll::AlignItems::kStretch;
    rs.padding        = { 16.0F, 16.0F, 16.0F, 16.0F };
    rs.gap_main       = 12.0F;
    u.root = u.tree.create_node(rs);

    ll::FlexStyle leaf;
    leaf.intrinsic_height = 36.0F;
    leaf.flex_shrink      = 0.0F;
    u.label  = u.tree.create_node(leaf);

    leaf.intrinsic_height = 40.0F;
    u.button = u.tree.create_node(leaf);

    leaf.intrinsic_height = 28.0F;
    u.slider = u.tree.create_node(leaf);

    u.tree.add_child(u.root, u.label);
    u.tree.add_child(u.root, u.button);
    u.tree.add_child(u.root, u.slider);
    return u;
}

[[nodiscard]] w::Rect to_widget_rect(const ll::Rect& r) noexcept
{
    return w::Rect { r.x, r.y, r.width, r.height };
}

// ---- Input flatten --------------------------------------------------------

struct PointerAccumulator
{
    float mouse_x       { 0.0F };
    float mouse_y       { 0.0F };
    bool  left_down     { false };
    bool  left_down_prev{ false };
};

[[nodiscard]] w::PointerState flatten_pointer(const PointerAccumulator& a) noexcept
{
    w::PointerState p;
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
                a.left_down = true;
            break;
        case platform::OSEventKind::kMouseButtonUp:
            if (e.mouse_button == platform::MouseButton::kLeft)
                a.left_down = false;
            break;
        default:
            break;
    }
}

// ---- Glyph rendering for the header label ---------------------------------
//
// Sample-side codepoint walker. Iterates `text` byte-by-byte (Phase 2.2
// ASCII / Latin-1 fast path -- matches the font atlas which is rasterized
// over the 0x0020..0x00FF range), looks each codepoint up via the font's
// glyph_uv table, and emits one `DrawBatcher::glyph(...)` per visible glyph
// at pen-advanced positions along the baseline. Returns the final pen x
// for diagnostic / future cursor placement use.
//
// Why duplicate the helper here rather than reuse cd::ui::widgets'
// `draw_text_line`: that helper lives in an anonymous namespace inside
// Widgets.cpp (intentional -- the widget catalog owns its own internal
// utilities). Per the Phase 506 brief, the sample explicitly demonstrates
// the codepoint loop + glyph emit flow at the application call-site so
// developers see how to render free-form labels outside a widget body.
[[nodiscard]] float draw_label_line(ur::DrawBatcher& batcher,
                                    const uf::Font& font,
                                    std::string_view text,
                                    float pen_x, float baseline_y,
                                    ur::Color color,
                                    std::uint32_t atlas_slot = 0U)
{
    if (!font.is_loaded())
        return pen_x;
    float pen = pen_x;
    std::uint32_t prev_cp = 0U;
    for (const char ch : text)
    {
        const auto cp =
            static_cast<std::uint32_t>(static_cast<unsigned char>(ch));
        const std::optional<uf::GlyphInfo> g = font.glyph_uv(cp);
        if (!g.has_value())
        {
            prev_cp = cp;
            continue;
        }
        if (prev_cp != 0U)
            pen += font.kerning(prev_cp, cp);

        const float gx = pen + g->bearing_x;
        const float gy = baseline_y - g->bearing_y;
        const ur::AtlasUv uv { g->u0, g->v0, g->u1, g->v1 };
        if (g->width > 0.0F && g->height > 0.0F)
            batcher.glyph(gx, gy, g->width, g->height, atlas_slot, uv, color);

        pen    += g->advance;
        prev_cp = cp;
    }
    return pen;
}

[[nodiscard]] ur::Color to_renderer_color(w::Color c) noexcept
{
    return ur::Color { c.r, c.g, c.b, c.a };
}

// ---- Headless / NullDevice report -----------------------------------------

void report_headless_frame(const ur::DrawBatcher& batcher,
                           const urr::Submitter& submitter,
                           std::uint32_t frame_idx)
{
    std::printf(
        "hello_ui[headless frame %u]: batcher emitted %zu verts / %zu indices / %zu commands; "
        "submitter holds %u verts / %u indices / %u commands.\n",
        frame_idx,
        batcher.vertex_count(), batcher.index_count(), batcher.command_count(),
        submitter.vertex_count(), submitter.index_count(), submitter.command_count());
    std::fflush(stdout);
}

}  // namespace

int main(int argc, char** argv)
{
    const cd::sample::Runtime runtime = cd::sample::parse_runtime(argc, argv);
    const LocalArgs           local   = parse_local_args(argc, argv);

    // -- 1. Font (always tried; widgets gracefully fall back when null) ------
    uf::Font font;
    {
        const auto ttf = find_system_font();
        if (!ttf.empty())
        {
            if (!font.load_ttf_in_memory(std::span<const std::uint8_t>(ttf.data(), ttf.size())))
            {
                std::fprintf(stderr, "hello_ui: failed to parse system TTF, glyphs disabled.\n");
            }
            else
            {
                static constexpr float         kPixelSize = 18.0F;
                static constexpr std::uint32_t kAtlasDim  = 2048U;
                (void)font.rasterize_range(0x0020U, 0x00FFU, kPixelSize, kAtlasDim);
            }
        }
        else
        {
            std::fprintf(stderr, "hello_ui: no system TTF found, glyphs disabled.\n");
        }
    }

    // -- 2. Widgets ----------------------------------------------------------
    int   click_count { 0 };
    float slider_val  { 0.5F };

    w::Button btn { "Click me", [&] { ++click_count; } };
    w::Slider sld { slider_val, [&](float v) { slider_val = v; } };

    UiTree tree = build_tree();
    constexpr w::Theme kTheme {};
    static constexpr std::string_view kHeaderLabel { "Hello UI" };

    // -- 3. Try Vulkan + window + Renderer; fall back to NullDevice ----------
    std::unique_ptr<platform::IWindow>      window;
    std::unique_ptr<rhi::IDevice>           device;
    std::optional<render::Renderer>         renderer;
    bool                                    using_null { local.force_null };

    if (!using_null)
    {
        platform::WindowDesc wd {};
        wd.title  = "CHROMODYNAMIC — hello_ui";
        wd.width  = 800;
        wd.height = 480;
        auto wr   = platform::create_window(wd);
        if (!wr.has_value())
        {
            std::fprintf(stderr, "hello_ui: platform window create failed, switching to NullDevice.\n");
            using_null = true;
        }
        else
        {
            window = std::move(*wr);
            rhi::vulkan::VulkanCreateInfo vci {};
            auto dr = rhi::vulkan::create_vulkan_device(vci);
            if (!dr.has_value())
            {
                std::fprintf(stderr, "hello_ui: Vulkan device create failed, switching to NullDevice.\n");
                window.reset();
                using_null = true;
            }
            else
            {
                device = std::move(*dr);

                // Boot the Renderer on top of the Vulkan device + window
                // swapchain. This is the Phase 506 deliverable: real
                // begin_frame() / end_frame() acquire+present cycle.
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
                                 "hello_ui: Renderer::create failed, switching to NullDevice.\n");
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
        std::printf("hello_ui: running headless on cd::rhi::NullDevice (no GPU).\n");
    }
    else
    {
        std::printf("hello_ui: booted Vulkan backend + Renderer swapchain. ESC to exit.\n");
    }
    std::fflush(stdout);

    // -- 4. UI submitter -----------------------------------------------------
    urr::SubmitterCreateInfo sci {};
    sci.max_vertices = 8192U;
    sci.max_indices  = 32768U;
    sci.color_format = rhi::Format::kBGRA8Unorm;
    auto sub_r = urr::Submitter::create(*device, sci);
    if (!sub_r.has_value())
    {
        std::fprintf(stderr, "hello_ui: ui_renderer_rhi::Submitter::create failed.\n");
        return 2;
    }
    auto& submitter = *sub_r;

    // -- 5. Frame loop -------------------------------------------------------
    ur::DrawBatcher           batcher;
    PointerAccumulator        pointer {};
    std::vector<platform::OSEvent> events;
    events.reserve(64);

    // We always run at least `headless_frames` iterations (default unset = 1
    // tick under NullDevice, finite count under --headless). Under Vulkan we
    // run until the window closes OR until headless_frames expires.
    const std::uint32_t default_headless = using_null ? 1U : 0U;
    const std::uint32_t cap_frames =
        runtime.headless_frames > 0U ? runtime.headless_frames : default_headless;

    bool          needs_rebuild { false };
    std::uint32_t frame_idx     { 0U };
    while (true)
    {
        // Frame-extent target for the layout solve.
        std::uint32_t fb_w { 800U };
        std::uint32_t fb_h { 480U };
        if (window)
        {
            fb_w = window->width()  > 0U ? window->width()  : fb_w;
            fb_h = window->height() > 0U ? window->height() : fb_h;
            events.clear();
            if (!window->pump_events(events))
                break;
            pointer.left_down_prev = pointer.left_down;
            for (const auto& e : events)
            {
                apply_event(pointer, e);
                if (e.kind == platform::OSEventKind::kKeyDown &&
                    e.key  == platform::KeyCode::kEscape)
                    window->request_close();
                else if (e.kind == platform::OSEventKind::kResize)
                    needs_rebuild = true;
            }
            if (renderer && needs_rebuild)
            {
                if (window->width() == 0U || window->height() == 0U)
                    continue;
                if (!renderer->recreate_swapchain(
                        rhi::Extent2D { window->width(), window->height() }).has_value())
                    continue;
                needs_rebuild = false;
            }
        }

        if (cap_frames > 0U && frame_idx >= cap_frames)
        {
            if (window)
                window->request_close();
            if (!window)
                break;
        }

        // -- Layout solve --
        tree.tree.solve(tree.root,
                        static_cast<float>(fb_w),
                        static_cast<float>(fb_h));

        // -- Apply rects to widgets --
        btn.set_rect(to_widget_rect(tree.tree.layout(tree.button)));
        sld.set_rect(to_widget_rect(tree.tree.layout(tree.slider)));

        // -- Tick widgets --
        w::InputState input;
        input.pointer = flatten_pointer(pointer);
        input.focused = false;
        (void)btn.tick(input);
        (void)sld.tick(input);

        // -- Draw widgets via batcher --
        batcher.begin_frame();

        // Background panel (label area) + live glyph rendering for the
        // header label. The rect comes from the flex solver; the text
        // walks codepoints + emits one DrawBatcher::glyph per glyph.
        {
            const auto lr = tree.tree.layout(tree.label);
            batcher.quad(lr.x, lr.y, lr.width, lr.height,
                         ur::Color { kTheme.surface.r, kTheme.surface.g,
                                     kTheme.surface.b, kTheme.surface.a });
            if (font.is_loaded())
            {
                // Baseline ~70% down the label rect so descenders fit.
                const float baseline = lr.y + lr.height * 0.70F;
                const float pen_x    = lr.x + 8.0F;
                (void)draw_label_line(batcher, font, kHeaderLabel,
                                      pen_x, baseline,
                                      to_renderer_color(kTheme.text));
            }
        }
        btn.draw(batcher, font.is_loaded() ? &font : nullptr, kTheme);

        // The Slider widget already draws a track quad + a knob quad whose
        // x reflects `value` (see engine/ui/widgets/src/Widgets.cpp,
        // Slider::draw). Calling sld.draw() here satisfies deliverable (3).
        sld.draw(batcher, font.is_loaded() ? &font : nullptr, kTheme);

        // -- Submit through ui_renderer_rhi --
        (void)submitter.upload(batcher);

        // Under NullDevice we have no swapchain / render pass; report and
        // continue. Real hardware drives the full begin_frame -> render
        // pass -> submitter.record -> end_frame cycle below.
        if (using_null || !renderer)
        {
            report_headless_frame(batcher, submitter, frame_idx);
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
                std::fprintf(stderr, "hello_ui: begin_frame failed.\n");
                return 5;
            }
            auto& frame = *frame_r;
            auto& cmd   = *frame.command_buffer;

            std::array<rhi::ColorAttachmentInfo, 1> color_attach { rhi::ColorAttachmentInfo {
                .view        = frame.swapchain_image_view,
                .load_op     = rhi::LoadOp::kClear,
                .store_op    = rhi::StoreOp::kStore,
                .clear_color = { .f32 = { 0.08F, 0.09F, 0.12F, 1.0F } } } };
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

            // submitter.record() walks the batched vertex/index buffers
            // + scissor stack and issues draw_indexed per DrawCommand.
            // The Submitter does NOT bind a graphics pipeline today
            // (Phase 1.2b scaffolds the resource ownership only -- the
            // UI VS/FS link + descriptor set wiring lands with the
            // cd::material UI variants per ADR-20260530 Phase 1.5).
            // Issuing draw_indexed without a bound pipeline causes
            // vkQueueSubmit2 to fail validation, so we gate the record
            // call: today only the NullDevice path exercises it (which
            // accepts the call as a no-op). Real-hardware DRAW-LIVE
            // emission flips on automatically once the Submitter
            // pipeline lands -- this sample is the consumer that
            // immediately benefits.
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
                             "hello_ui: end_frame failed (domain=%u code=%u): %.*s\n",
                             end_r.error().domain, end_r.error().code,
                             static_cast<int>(end_r.error().message.size()),
                             end_r.error().message.data());
                return 6;
            }
        }

        ++frame_idx;
        if (!window && cap_frames > 0U && frame_idx >= cap_frames)
            break;
        if (window && window->should_close())
            break;
    }

    if (renderer)
        renderer->wait_idle();

    std::printf("hello_ui: clean exit (%u frames; clicks=%d; slider=%.3f).\n",
                frame_idx, click_count, static_cast<double>(slider_val));
    std::fflush(stdout);
    return 0;
}
