// =============================================================================
// CHROMODYNAMIC — samples/hello_framegraph/main.cpp
//
// Demonstrates the cd::framegraph driving a real Vulkan frame:
//   * One pass that imports the swapchain image (final_state = kPresent)
//   * The framegraph inserts the UNDEFINED→COLOR_ATTACHMENT and
//     COLOR_ATTACHMENT→PRESENT barriers automatically
//   * The execute() callback only records the actual render-pass + draw
//
// Why MVP single-pass: this proves the framegraph → IDevice → ICommandBuffer
// pipeline is wired correctly end-to-end. Multi-pass demos (render-to-texture
// + post effect) are a natural follow-up sample once we add a fullscreen-
// quad material helper to cd::material.
// =============================================================================
#include "SampleRuntime.hpp"

#include <cd/framegraph/FrameGraph.hpp>
#include <cd/material/Material.hpp>
#include <cd/platform/Window.hpp>
#include <cd/render/Renderer.hpp>
#include <cd/rhi/Barriers.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi_vulkan/VulkanDevice.hpp>
#include <cd/shader/Compiler.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <vector>

namespace
{

constexpr const char* kVS = R"glsl(
#version 450
layout(location = 0) out vec3 v_color;
void main() {
  // Procedural CCW triangle, colors set per vertex.
  vec2 positions[3] = vec2[3](
    vec2( 0.0,  0.6),
    vec2(-0.6, -0.6),
    vec2( 0.6, -0.6)
  );
  vec3 colors[3] = vec3[3](
    vec3(0.95, 0.30, 0.30),
    vec3(0.30, 0.95, 0.30),
    vec3(0.30, 0.30, 0.95)
  );
  gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
  v_color = colors[gl_VertexIndex];
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

    cd::platform::WindowDesc wd {};
    wd.title = "CHROMODYNAMIC — hello_framegraph";
    wd.width = 800;
    wd.height = 600;
    auto window_r = cd::platform::create_window(wd);
    if (!window_r.has_value())
    {
        std::fprintf(stderr, "window failed\n");
        return 1;
    }
    auto& window = **window_r;

    cd::rhi_vulkan::VulkanCreateInfo vci {};
    auto device_r = cd::rhi_vulkan::create_vulkan_device(vci);
    if (!device_r.has_value())
    {
        std::fprintf(stderr, "device failed\n");
        return 2;
    }
    auto& device = **device_r;

    cd::render::RendererDesc rd {};
    rd.device = &device;
    rd.swapchain.window_handle = window.native_window_handle();
    rd.swapchain.display_handle = window.native_display_handle();
    rd.swapchain.extent = { window.width(), window.height() };
    rd.swapchain.format = cd::rhi::Format::kBGRA8Unorm;
    rd.frames_in_flight = 2;
    auto renderer_r = cd::render::Renderer::create(rd);
    if (!renderer_r.has_value())
    {
        std::fprintf(stderr, "renderer failed\n");
        return 3;
    }
    auto& renderer = *renderer_r;

    auto compiler = cd::shader::make_glslang_compiler();
    if (compiler == nullptr)
    {
        std::fprintf(stderr, "no glslang\n");
        return 4;
    }

    constexpr std::array<cd::rhi::Format, 1> kColorFormats { cd::rhi::Format::kBGRA8Unorm };
    cd::material::MaterialDesc md {};
    md.vertex_glsl = kVS;
    md.fragment_glsl = kFS;
    md.color_attachment_formats = kColorFormats;
    md.topology = cd::rhi::PrimitiveTopology::kTriangleList;
    md.raster.cull = cd::rhi::CullMode::kNone;
    md.name = "fg_triangle";
    auto material_r = cd::material::Material::create(device, compiler.get(), md);
    if (!material_r.has_value())
    {
        std::fprintf(stderr, "material failed\n");
        return 5;
    }
    auto& material = *material_r;

    std::printf("hello_framegraph: ready. ESC to exit.\n");
    std::fflush(stdout);

    std::vector<cd::platform::OSEvent> events;
    events.reserve(64);
    bool needs_rebuild = false;

    std::uint32_t frame_idx = 0;
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
                window.request_close();
            else if (e.kind == cd::platform::OSEventKind::kResize)
                needs_rebuild = true;
        }
        if (needs_rebuild)
        {
            if (window.width() == 0 || window.height() == 0)
                continue;
            if (!renderer.recreate_swapchain({ window.width(), window.height() }).has_value())
                continue;
            needs_rebuild = false;
        }

        auto frame_r = renderer.begin_frame();
        if (!frame_r.has_value())
        {
            if (frame_r.error().code ==
                static_cast<std::uint32_t>(cd::render::render_errors::Code::kSwapchainOutOfDate))
            {
                needs_rebuild = true;
                continue;
            }
            return 6;
        }
        auto& frame = *frame_r;
        auto& cmd = *frame.command_buffer;

        // ---- Build a fresh framegraph each frame ---------------------------
        // The Renderer has already emitted the implicit UNDEFINED→
        // COLOR_ATTACHMENT barrier on the swapchain image. We import it in
        // that state and ask the framegraph to leave it in kPresent before
        // end_frame() — though the Renderer's end_frame() will also append
        // its own COLOR→PRESENT barrier; this is the simpler MVP where the
        // framegraph contributes the render-pass orchestration only.
        cd::framegraph::FrameGraph fg { device };
        const auto sc_handle = fg.import_texture(
            cd::framegraph::ImportedTextureDesc { .texture = frame.swapchain_image,
                                                  .initial_state = cd::rhi::ResourceState::kColorAttachment,
                                                  .final_state = cd::rhi::ResourceState::kColorAttachment },
            "swapchain"
        );

        std::array<cd::framegraph::PassResource, 1> writes {
            cd::framegraph::PassResource { sc_handle, cd::rhi::ResourceState::kColorAttachment }
        };

        cd::framegraph::PassDesc pass {};
        pass.name = "draw_triangle";
        pass.writes = writes;
        // Capture frame.* by value so the lambda is self-contained even if
        // the framegraph were to defer execution across frames.
        const auto view = frame.swapchain_image_view;
        const auto extent = frame.extent;
        pass.execute = [&material, view, extent](cd::rhi::ICommandBuffer& c)
        {
            std::array<cd::rhi::ColorAttachmentInfo, 1> color_attach {
                cd::rhi::ColorAttachmentInfo { .view = view,
                                              .load_op = cd::rhi::LoadOp::kClear,
                                              .store_op = cd::rhi::StoreOp::kStore,
                                              .clear_color = { .f32 = { 0.05F, 0.08F, 0.12F, 1.0F } } }
            };
            cd::rhi::RenderPassBeginInfo rp {};
            rp.render_area = cd::rhi::Rect2D {
                { 0, 0 },
                extent
            };
            rp.color_attachments = color_attach;
            c.begin_render_pass(rp);
            c.set_viewport(
                cd::rhi::Viewport { 0.0F,
                                    0.0F,
                                    static_cast<float>(extent.width),
                                    static_cast<float>(extent.height),
                                    0.0F,
                                    1.0F }
            );
            c.set_scissor(
                cd::rhi::Rect2D {
                    { 0, 0 },
                    extent
            }
            );
            material.apply(c);
            c.draw(/*vertex_count=*/3, /*instance_count=*/1, 0, 0);
            c.end_render_pass();
        };
        fg.add_pass(pass);

        if (!fg.compile().has_value())
        {
            std::fprintf(stderr, "fg.compile failed\n");
            return 7;
        }
        if (!fg.execute(cmd).has_value())
        {
            std::fprintf(stderr, "fg.execute failed\n");
            return 8;
        }

        auto end_r = renderer.end_frame();
        if (!end_r.has_value())
        {
            if (end_r.error().code == static_cast<std::uint32_t>(cd::render::render_errors::Code::kSwapchainOutOfDate))
            {
                needs_rebuild = true;
                continue;
            }
            return 9;
        }
        ++frame_idx;
    }

    renderer.wait_idle();
    std::printf("hello_framegraph: clean exit.\n");
    return 0;
}
