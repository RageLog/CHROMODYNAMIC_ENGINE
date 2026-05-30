// =============================================================================
// CHROMODYNAMIC — samples/ui/hello_ui/main.cpp
//
// Phase 1.5 + partial Phase 2 of ADR-20260530-ui-widget-library. End-to-
// end tie-together of the new cd::ui Phase-1/Phase-2 libraries:
//
//   * cd::ui_layout       (Flex layout solver)
//   * cd::ui_font         (stb_truetype atlas)
//   * cd::ui_renderer     (CPU DrawBatcher)
//   * cd::ui_renderer_rhi (RHI Submitter — ring vb/ib + descriptor set)
//   * cd::ui_input        (hit-test + focus chain + tab/shift-tab + modal)
//   * cd::ui_widgets      (Button / Slider / TextInput / ... catalog)
//
// Boot order per frame:
//   1. pump platform events; flatten into a cd::ui::widgets::InputState.
//   2. solve the Flex layout for the current viewport extent.
//   3. tick widgets (state transitions). Optional callbacks fire here.
//   4. begin_frame on the renderer; begin a colour-clear render pass.
//   5. draw widgets into the cd::ui::renderer::DrawBatcher.
//   6. submitter.upload(batcher) + submitter.record(cmd, extent).
//   7. end render pass; end_frame.
//
// Headless mode (NullDevice): when `--null` is passed (or when the Vulkan
// backend cannot be initialised), the sample still constructs the full
// layout + widget tree + batcher pipeline and reports what *would* be
// uploaded / recorded. This proves the ABI on hosts without a GPU and
// is the only path the CI smoke harness exercises.
//
// Live Vulkan validation (validation-layer clean) requires a desktop
// with a Vulkan ICD; queued for a session with hardware per the ADR.
// =============================================================================

#include "SampleRuntime.hpp"

#include <cd/platform/Window.hpp>
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
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{

namespace platform = cd::platform;
namespace rhi      = cd::rhi;
namespace ll       = cd::ui::layout;
namespace uf       = cd::ui::font;
namespace ur       = cd::ui::renderer;
namespace urr      = cd::ui::renderer_rhi;
namespace ui_in    = cd::ui::input;
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
    namespace fs = std::filesystem;
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

    // -- 3. Try Vulkan + window + Renderer; fall back to NullDevice ----------
    std::unique_ptr<platform::IWindow> window;
    std::unique_ptr<rhi::IDevice>      device;
    bool                               using_null { local.force_null };

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
        std::printf("hello_ui: booted Vulkan backend, window opened. ESC to exit.\n");
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

    std::uint32_t frame_idx { 0U };
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

        // Background panel (label area). Uses raw rect from the flex solver.
        {
            const auto lr = tree.tree.layout(tree.label);
            batcher.quad(lr.x, lr.y, lr.width, lr.height,
                         ur::Color { kTheme.surface.r, kTheme.surface.g,
                                     kTheme.surface.b, kTheme.surface.a });
            // Text would be drawn here once font glyph batching is wired.
            (void)font.is_loaded();
        }
        btn.draw(batcher, font.is_loaded() ? &font : nullptr, kTheme);
        sld.draw(batcher, font.is_loaded() ? &font : nullptr, kTheme);

        // -- Submit through ui_renderer_rhi --
        (void)submitter.upload(batcher);

        // Under NullDevice we have no swapchain / render pass; report and
        // continue. The Submitter::record call below is exercised on real
        // hardware once the queued GPU-validation session lands.
        if (using_null)
        {
            report_headless_frame(batcher, submitter, frame_idx);
        }
        else
        {
            // Live Vulkan path: would acquire swapchain, begin render pass,
            // call submitter.record(cmd, extent), and end the frame. The
            // full Renderer wiring is left for the GPU-validation session
            // per the brief; this commit's success criterion is compile
            // + boot cleanly. We still issue one record call against the
            // device's command buffer so the API surface is touched.
            auto cmd = device->create_command_buffer(rhi::QueueType::kGraphics);
            if (cmd != nullptr)
            {
                cmd->begin();
                submitter.record(*cmd, rhi::Extent2D { fb_w, fb_h });
                cmd->end();
            }
        }

        ++frame_idx;
        if (!window && cap_frames > 0U && frame_idx >= cap_frames)
            break;
        if (window && window->should_close())
            break;
    }

    std::printf("hello_ui: clean exit (%u frames; clicks=%d; slider=%.3f).\n",
                frame_idx, click_count, static_cast<double>(slider_val));
    std::fflush(stdout);
    return 0;
}
