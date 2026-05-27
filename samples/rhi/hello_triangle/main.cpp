// =============================================================================
// CHROMODYNAMIC — samples/hello_triangle/main.cpp
//
// End-to-end smoke test: open a platform window, create a Vulkan device
// against it, compile the canonical orange-triangle vertex+fragment shader
// at runtime via glslang, build a Material, drive a per-frame draw loop.
//
// Run with: build/<preset>/bin/Debug/hello_triangle
// Close with the window's X button or by pressing Esc.
// =============================================================================
#include "GoldenCapture.hpp"
#include "SampleRuntime.hpp"

#include <cd/material/Material.hpp>
#include <cd/platform/Window.hpp>
#include <cd/render/Renderer.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi_vulkan/VulkanDevice.hpp>
#include <cd/shader/Compiler.hpp>

#include <array>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace
{

constexpr const char* kVS = R"glsl(
#version 450
const vec2 P[3] = vec2[](vec2(0.0, -0.5), vec2(0.5, 0.5), vec2(-0.5, 0.5));
const vec3 C[3] = vec3[](vec3(1.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0), vec3(0.0, 0.0, 1.0));
layout(location = 0) out vec3 v_color;
void main() {
  gl_Position = vec4(P[gl_VertexIndex], 0.0, 1.0);
  v_color = C[gl_VertexIndex];
}
)glsl";

constexpr const char* kFS = R"glsl(
#version 450
layout(location = 0) in  vec3 v_color;
layout(location = 0) out vec4 out_color;
void main() { out_color = vec4(v_color, 1.0); }
)glsl";

}  // namespace

int main(int argc, char** argv)
{
    const cd::sample::Runtime runtime = cd::sample::parse_runtime(argc, argv);

    // ---- Platform window ---------------------------------------------------
    cd::platform::WindowDesc wd {};
    wd.title = "CHROMODYNAMIC — hello_triangle";
    wd.width = 800;
    wd.height = 600;
    wd.visible = true;
    auto window_r = cd::platform::create_window(wd);
    if (!window_r.has_value())
    {
        std::fprintf(
            stderr,
            "create_window failed: %.*s\n",
            static_cast<int>(window_r.error().message.size()),
            window_r.error().message.data()
        );
        return 1;
    }
    auto& window = **window_r;

    // ---- Vulkan device -----------------------------------------------------
    cd::rhi_vulkan::VulkanCreateInfo vci {};
    vci.enable_validation = false;
    auto device_r = cd::rhi_vulkan::create_vulkan_device(vci);
    if (!device_r.has_value())
    {
        std::fprintf(
            stderr,
            "create_vulkan_device failed: %.*s\n",
            static_cast<int>(device_r.error().message.size()),
            device_r.error().message.data()
        );
        return 2;
    }
    auto& device = **device_r;

    // ---- Renderer (swapchain + per-frame ring) -----------------------------
    cd::render::RendererDesc rd {};
    rd.device = &device;
    rd.swapchain.window_handle = window.native_window_handle();
    rd.swapchain.display_handle = window.native_display_handle();
    rd.swapchain.extent = { window.width(), window.height() };
    rd.swapchain.image_count = 2;
    rd.swapchain.format = cd::rhi::Format::kBGRA8Unorm;
    rd.frames_in_flight = 2;
    auto renderer_r = cd::render::Renderer::create(rd);
    if (!renderer_r.has_value())
    {
        std::fprintf(
            stderr,
            "Renderer::create failed: %.*s\n",
            static_cast<int>(renderer_r.error().message.size()),
            renderer_r.error().message.data()
        );
        return 3;
    }
    auto& renderer = *renderer_r;

    // ---- Compile shaders + build Material ----------------------------------
    auto compiler = cd::shader::make_glslang_compiler();
    if (compiler == nullptr)
    {
        std::fprintf(stderr, "engine built without CD_ENABLE_GLSLANG\n");
        return 4;
    }
    std::array<cd::rhi::Format, 1> color_formats { renderer.swapchain_format() };
    cd::material::MaterialDesc md {};
    md.vertex_glsl = kVS;
    md.fragment_glsl = kFS;
    md.color_attachment_formats = color_formats;
    md.topology = cd::rhi::PrimitiveTopology::kTriangleList;
    // Disable back-face culling: Vulkan's NDC is Y-down so a vertex list
    // written for the conventional "math" axis (Y-up) ends up clockwise,
    // which would be culled with the default kBack + kCounterClockwise
    // pair. kNone keeps the demo orientation-agnostic.
    md.raster.cull = cd::rhi::CullMode::kNone;
    md.name = "triangle";
    auto material_r = cd::material::Material::create(device, compiler.get(), md);
    if (!material_r.has_value())
    {
        std::fprintf(
            stderr,
            "Material::create failed: %.*s\n",
            static_cast<int>(material_r.error().message.size()),
            material_r.error().message.data()
        );
        return 5;
    }
    auto& material = *material_r;

    std::printf("hello_triangle: ready. Press Esc or close the window to exit.\n");
    std::fflush(stdout);

    // ---- Main loop ---------------------------------------------------------
    std::vector<cd::platform::OSEvent> events;
    events.reserve(64);
    bool needs_swapchain_rebuild = false;
    auto try_rebuild_swapchain = [&]
    {
        // Skip the rebuild while the window has zero extent (minimized) —
        // recreate_swapchain rejects (0,0) and there's nothing to render
        // anyway. We try again on the next pump_events tick when WM_SIZE
        // delivers a real size.
        if (window.width() == 0 || window.height() == 0)
            return false;
        cd::rhi::Extent2D ext { window.width(), window.height() };
        auto r = renderer.recreate_swapchain(ext);
        if (!r.has_value())
        {
            std::fprintf(
                stderr,
                "recreate_swapchain failed: %.*s\n",
                static_cast<int>(r.error().message.size()),
                r.error().message.data()
            );
            return false;
        }
        needs_swapchain_rebuild = false;
        return true;
    };

    std::uint32_t frame_idx = 0;
    cd::sample::GoldenState golden_state {};
    while (true)
    {
        if (!runtime.should_continue(frame_idx))
            window.request_close();
        events.clear();
        if (!window.pump_events(events))
            break;
        for (const auto& e : events)
        {
            if (e.kind == cd::platform::OSEventKind::kKeyDown && e.key == cd::platform::KeyCode::kEscape)
            {
                window.request_close();
            }
            else if (e.kind == cd::platform::OSEventKind::kResize)
            {
                // Defer the actual recreate until just before begin_frame: the
                // user may still be dragging the edge, so we'd otherwise churn
                // the swapchain on every WM_SIZE.
                needs_swapchain_rebuild = true;
            }
        }
        if (needs_swapchain_rebuild && !try_rebuild_swapchain())
        {
            // Minimized / can't rebuild — skip this frame entirely.
            continue;
        }

        auto frame_r = renderer.begin_frame();
        if (!frame_r.has_value())
        {
            // kSwapchainOutOfDate can fire even between pump_events ticks
            // (Win32 sometimes delays WM_SIZE behind paint events). Treat it
            // as a hint to rebuild and retry on the next loop iteration.
            if (frame_r.error().code ==
                static_cast<std::uint32_t>(cd::render::render_errors::Code::kSwapchainOutOfDate))
            {
                needs_swapchain_rebuild = true;
                continue;
            }
            std::fprintf(
                stderr,
                "begin_frame failed: %.*s\n",
                static_cast<int>(frame_r.error().message.size()),
                frame_r.error().message.data()
            );
            return 6;
        }
        auto& frame = *frame_r;
        auto& cmd = *frame.command_buffer;

        // Begin dynamic-rendering pass on the swapchain image. The
        // Renderer already transitioned it to COLOR_ATTACHMENT and emitted
        // the begin-frame barrier — we just need to record draw commands.
        std::array<cd::rhi::ColorAttachmentInfo, 1> attach {
            cd::rhi::ColorAttachmentInfo {
                                          .view = frame.swapchain_image_view,
                                          .load_op = cd::rhi::LoadOp::kClear,
                                          .store_op = cd::rhi::StoreOp::kStore,
                                          .clear_color = { .f32 = { 0.04F, 0.05F, 0.08F, 1.0F } },
                                          }
        };
        cd::rhi::RenderPassBeginInfo rp {};
        rp.render_area = cd::rhi::Rect2D {
            { 0,                  0                   },
            { frame.extent.width, frame.extent.height }
        };
        rp.color_attachments = attach;
        cmd.begin_render_pass(rp);

        // Viewport + scissor must be set each frame because the pipeline
        // declares them as dynamic state.
        cmd.set_viewport(
            cd::rhi::Viewport { 0.0F,
                                0.0F,
                                static_cast<float>(frame.extent.width),
                                static_cast<float>(frame.extent.height),
                                0.0F,
                                1.0F }
        );
        cmd.set_scissor(
            cd::rhi::Rect2D {
                { 0, 0 },
                frame.extent
        }
        );

        material.apply(cmd);
        cmd.draw(/*vertex_count=*/3,
                 /*instance_count=*/1,
                 /*first_vertex=*/0,
                 /*first_instance=*/0);
        cmd.end_render_pass();

        if (runtime.is_golden_frame(frame_idx))
        {
            (void)cd::sample::schedule_golden_capture(
                device, cmd, frame.swapchain_image, frame.extent, golden_state);
        }

        auto end_r = renderer.end_frame();
        if (!end_r.has_value())
        {
            if (end_r.error().code == static_cast<std::uint32_t>(cd::render::render_errors::Code::kSwapchainOutOfDate))
            {
                // Present saw an out-of-date swapchain — schedule a rebuild for
                // the next frame instead of bailing.
                needs_swapchain_rebuild = true;
                continue;
            }
            std::fprintf(
                stderr,
                "end_frame failed: %.*s\n",
                static_cast<int>(end_r.error().message.size()),
                end_r.error().message.data()
            );
            return 7;
        }
        ++frame_idx;
    }

    renderer.wait_idle();

    int golden_rc = 0;
    if (golden_state.armed)
        golden_rc = cd::sample::finish_golden_capture(device, runtime, golden_state);

    std::printf("hello_triangle: clean exit.\n");
    return golden_rc;
}
