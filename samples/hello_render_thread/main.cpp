// =============================================================================
// CHROMODYNAMIC — samples/hello_render_thread/main.cpp (S4.4)
//
// Same scene as hello_render_thread (spinning RGB cube + depth + push constants),
// but dispatches the renderer.end_frame() submit/present call to a
// worker thread via cd::render::AsyncSubmit. Demonstrates the "one
// frame deep" render-thread pattern:
//
//   while (loop) {
//       async.wait_idle();        // drain previous frame's submit
//       auto frame = renderer.begin_frame();
//       record(frame.command_buffer);
//       async.enqueue([&] { renderer.end_frame(); });
//   }
//
// Game logic for frame N+1 overlaps with GPU submit/present of N.
// The AsyncSubmit primitive runs a 1000-iteration stress test in its
// own unit suite; this sample is the visual confirmation that the
// pattern boots a real Vulkan swapchain without deadlock.
// =============================================================================
#include "SampleRuntime.hpp"

#include <cd/camera/Camera.hpp>
#include <cd/camera/OrbitController.hpp>
#include <cd/material/Material.hpp>
#include <cd/math/Matrix.hpp>
#include <cd/math/Quaternion.hpp>
#include <cd/math/Transform.hpp>
#include <cd/math/Vector.hpp>
#include <cd/platform/Window.hpp>
#include <cd/render/AsyncSubmit.hpp>
#include <cd/render/Renderer.hpp>
#include <cd/rhi/Barriers.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi_vulkan/VulkanDevice.hpp>
#include <cd/shader/Compiler.hpp>

#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <vector>

namespace
{

struct Vertex
{
    float pos[3];
    float color[3];
};

// 8-corner cube with per-vertex colors derived from position so adjacent
// faces blend smoothly across shared edges.
constexpr std::array<Vertex, 8> kVerts {
    {
     { { -0.5F, -0.5F, -0.5F }, { 0.0F, 0.0F, 0.0F } },  // 0
        { { 0.5F, -0.5F, -0.5F }, { 1.0F, 0.0F, 0.0F } },   // 1
        { { 0.5F, 0.5F, -0.5F }, { 1.0F, 1.0F, 0.0F } },    // 2
        { { -0.5F, 0.5F, -0.5F }, { 0.0F, 1.0F, 0.0F } },   // 3
        { { -0.5F, -0.5F, 0.5F }, { 0.0F, 0.0F, 1.0F } },   // 4
        { { 0.5F, -0.5F, 0.5F }, { 1.0F, 0.0F, 1.0F } },    // 5
        { { 0.5F, 0.5F, 0.5F }, { 1.0F, 1.0F, 1.0F } },     // 6
        { { -0.5F, 0.5F, 0.5F }, { 0.0F, 1.0F, 1.0F } },    // 7
    }
};

// 12 triangles × 3 indices. Wind order CCW from outside the cube; we
// disable culling anyway so it doesn't matter for the demo.
constexpr std::array<std::uint16_t, 36> kIndices {
    // back  (-Z)
    0,
    1,
    2,
    0,
    2,
    3,
    // front (+Z)
    4,
    6,
    5,
    4,
    7,
    6,
    // left  (-X)
    0,
    3,
    7,
    0,
    7,
    4,
    // right (+X)
    1,
    5,
    6,
    1,
    6,
    2,
    // bottom(-Y)
    0,
    4,
    5,
    0,
    5,
    1,
    // top   (+Y)
    3,
    2,
    6,
    3,
    6,
    7,
};

constexpr const char* kVS = R"glsl(
#version 450
layout(push_constant) uniform PC { mat4 mvp; } pc;
layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec3 in_color;
layout(location = 0) out vec3 v_color;
void main() {
  vec4 clip = pc.mvp * vec4(in_pos, 1.0);
  // Vulkan NDC Y is down. Our math matrix is built for Y-up — flip here
  // so positive Y in world maps to "up" on screen.
  clip.y = -clip.y;
  gl_Position = clip;
  v_color = in_color;
}
)glsl";

constexpr const char* kFS = R"glsl(
#version 450
layout(location = 0) in  vec3 v_color;
layout(location = 0) out vec4 out_color;
void main() { out_color = vec4(v_color, 1.0); }
)glsl";

[[nodiscard]] cd::rhi::BufferHandle
make_upload_buffer(cd::rhi::IDevice& dev, std::span<const std::byte> bytes, cd::rhi::BufferUsage usage)
{
    cd::rhi::BufferDesc bd {};
    bd.size = bytes.size();
    bd.usage = usage;
    bd.memory = cd::rhi::MemoryUsage::kCpuToGpu;
    auto r = dev.create_buffer(bd);
    if (!r.has_value())
        return {};
    if (auto u = dev.upload_buffer(*r, 0, bytes); !u.has_value())
    {
        dev.destroy_buffer(*r);
        return {};
    }
    return *r;
}

/// Per-frame depth resource. Lives outside the Renderer because the
/// Renderer owns only the color swapchain — depth is application policy.
struct DepthTarget
{
    cd::rhi::TextureHandle image {};
    cd::rhi::TextureViewHandle view {};
    cd::rhi::Extent2D extent {};

    void destroy(cd::rhi::IDevice& dev)
    {
        if (view.is_valid())
            dev.destroy_texture_view(view);
        if (image.is_valid())
            dev.destroy_texture(image);
        image = {};
        view = {};
        extent = {};
    }
};

[[nodiscard]] bool
create_depth_target(cd::rhi::IDevice& dev, cd::rhi::Extent2D size, cd::rhi::Format format, DepthTarget& out)
{
    out.destroy(dev);
    cd::rhi::TextureDesc td {};
    td.type = cd::rhi::TextureType::k2D;
    td.format = format;
    td.extent = { size.width, size.height, 1 };
    td.mip_levels = 1;
    td.array_layers = 1;
    td.usage = cd::rhi::TextureUsage::kDepthStencilAttachment;
    td.memory = cd::rhi::MemoryUsage::kGpuOnly;
    auto img = dev.create_texture(td);
    if (!img.has_value())
        return false;

    cd::rhi::TextureViewDesc vd {};
    vd.texture = *img;
    vd.type = cd::rhi::TextureType::k2D;
    vd.format = format;
    vd.base_mip = 0;
    vd.mip_count = 1;
    vd.base_layer = 0;
    vd.layer_count = 1;
    auto view = dev.create_texture_view(vd);
    if (!view.has_value())
    {
        dev.destroy_texture(*img);
        return false;
    }
    out.image = *img;
    out.view = *view;
    out.extent = size;
    return true;
}

}  // namespace

int main(int argc, char** argv)
{
    const cd::sample::Runtime runtime = cd::sample::parse_runtime(argc, argv);

    // ---- Window + device + renderer ---------------------------------------
    cd::platform::WindowDesc wd {};
    wd.title = "CHROMODYNAMIC — hello_render_thread (3D + depth + push constants)";
    wd.width = 1024;
    wd.height = 768;
    auto window_r = cd::platform::create_window(wd);
    if (!window_r.has_value())
    {
        std::fprintf(
            stderr,
            "window: %.*s\n",
            static_cast<int>(window_r.error().message.size()),
            window_r.error().message.data()
        );
        return 1;
    }
    auto& window = **window_r;

    cd::rhi_vulkan::VulkanCreateInfo vci {};
    auto device_r = cd::rhi_vulkan::create_vulkan_device(vci);
    if (!device_r.has_value())
    {
        std::fprintf(
            stderr,
            "device: %.*s\n",
            static_cast<int>(device_r.error().message.size()),
            device_r.error().message.data()
        );
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
        std::fprintf(
            stderr,
            "renderer: %.*s\n",
            static_cast<int>(renderer_r.error().message.size()),
            renderer_r.error().message.data()
        );
        return 3;
    }
    auto& renderer = *renderer_r;

    // ---- Depth target -----------------------------------------------------
    constexpr auto kDepthFormat = cd::rhi::Format::kD32Float;
    DepthTarget depth {};
    if (!create_depth_target(device, { window.width(), window.height() }, kDepthFormat, depth))
    {
        std::fprintf(stderr, "depth create failed\n");
        return 4;
    }
    bool depth_initialized_on_gpu = false;  // first-time UNDEFINED→DEPTH transition flag

    // ---- Geometry ---------------------------------------------------------
    const std::span<const std::byte> vb_bytes { reinterpret_cast<const std::byte*>(kVerts.data()),
                                                kVerts.size() * sizeof(Vertex) };
    const std::span<const std::byte> ib_bytes { reinterpret_cast<const std::byte*>(kIndices.data()),
                                                kIndices.size() * sizeof(std::uint16_t) };
    auto vb = make_upload_buffer(device, vb_bytes, cd::rhi::BufferUsage::kVertex);
    auto ib = make_upload_buffer(device, ib_bytes, cd::rhi::BufferUsage::kIndex);
    if (!vb.is_valid() || !ib.is_valid())
    {
        std::fprintf(stderr, "buffers\n");
        return 5;
    }

    // ---- Material with push constants + depth attachment declared ---------
    auto compiler = cd::shader::make_glslang_compiler();
    if (compiler == nullptr)
    {
        std::fprintf(stderr, "no glslang\n");
        return 6;
    }

    constexpr std::array<cd::rhi::VertexBinding, 1> kBindings {
        cd::rhi::VertexBinding { 0, sizeof(Vertex), false }
    };
    constexpr std::array<cd::rhi::VertexAttribute, 2> kAttrs {
        cd::rhi::VertexAttribute { 0, 0, cd::rhi::Format::kRGB32Float, offsetof(Vertex, pos)   },
        cd::rhi::VertexAttribute { 1, 0, cd::rhi::Format::kRGB32Float, offsetof(Vertex, color) }
    };
    constexpr std::array<cd::rhi::Format, 1> kColorFormats { cd::rhi::Format::kBGRA8Unorm };
    constexpr std::array<cd::rhi::PushConstantRange, 1> kPush {
        cd::rhi::PushConstantRange { .stages = cd::rhi::ShaderStage::kVertex,
                                    .offset = 0,
                                    .size = static_cast<std::uint32_t>(sizeof(cd::math::Mat4f)) }
    };

    cd::material::MaterialDesc md {};
    md.vertex_glsl = kVS;
    md.fragment_glsl = kFS;
    md.vertex_bindings = kBindings;
    md.vertex_attributes = kAttrs;
    md.color_attachment_formats = kColorFormats;
    md.depth_attachment_format = kDepthFormat;
    md.push_constants = kPush;
    md.topology = cd::rhi::PrimitiveTopology::kTriangleList;
    md.raster.cull = cd::rhi::CullMode::kNone;
    md.depth_stencil.depth_test = true;
    md.depth_stencil.depth_write = true;
    md.depth_stencil.depth_compare = cd::rhi::CompareOp::kLess;
    md.name = "cube";
    auto material_r = cd::material::Material::create(device, compiler.get(), md);
    if (!material_r.has_value())
    {
        std::fprintf(
            stderr,
            "material: %.*s\n",
            static_cast<int>(material_r.error().message.size()),
            material_r.error().message.data()
        );
        return 7;
    }
    auto& material = *material_r;

    // ---- Render thread primitive -----------------------------------------
    // One AsyncSubmit slot. enqueue() inside the loop hands the
    // renderer.end_frame() submit/present to the worker; the next
    // wait_idle() call (at the top of the next iteration) drains it.
    cd::render::AsyncSubmit async;

    std::printf("hello_render_thread: ready. AsyncSubmit owns end_frame(). ESC or close to exit.\n");
    std::fflush(stdout);

    // ---- Main loop --------------------------------------------------------
    std::vector<cd::platform::OSEvent> events;
    events.reserve(64);
    bool needs_rebuild = false;
    auto rebuild = [&]
    {
        // The swapchain re-create path must NOT overlap with the worker
        // thread — drain it first so no in-flight submit references
        // images about to be destroyed.
        async.wait_idle();
        if (window.width() == 0 || window.height() == 0)
            return false;
        if (!renderer.recreate_swapchain({ window.width(), window.height() }).has_value())
            return false;
        if (!create_depth_target(device, { window.width(), window.height() }, kDepthFormat, depth))
            return false;
        depth_initialized_on_gpu = false;
        needs_rebuild = false;
        return true;
    };

    const auto t_start = std::chrono::steady_clock::now();
    std::uint32_t frame_idx = 0;
    while (true)
    {
        // Drain the worker so the about-to-be-acquired Frame's slot is
        // free. Single-slot pipeline → wait_idle is the synchronisation
        // point between game logic frame N+1 and submit of frame N.
        async.wait_idle();

        // Headless mode: trigger window close after N frames so CI exits.
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
                needs_rebuild = true;
            }
        }
        if (needs_rebuild && !rebuild())
            continue;

        auto frame_r = renderer.begin_frame();
        if (!frame_r.has_value())
        {
            if (frame_r.error().code ==
                static_cast<std::uint32_t>(cd::render::render_errors::Code::kSwapchainOutOfDate))
            {
                needs_rebuild = true;
                continue;
            }
            std::fprintf(
                stderr,
                "begin_frame: %.*s\n",
                static_cast<int>(frame_r.error().message.size()),
                frame_r.error().message.data()
            );
            return 8;
        }
        auto& frame = *frame_r;
        auto& cmd = *frame.command_buffer;

        // First-time depth transition: UNDEFINED → DEPTH_WRITE. After that
        // begin_render_pass / end_render_pass keep the image in the right
        // layout (DEPTH_ATTACHMENT_OPTIMAL).
        if (!depth_initialized_on_gpu)
        {
            std::array<cd::rhi::TextureBarrier, 1> dbar {
                cd::rhi::TextureBarrier {
                                         .texture = depth.image,
                                         .from = cd::rhi::ResourceState::kUndefined,
                                         .to = cd::rhi::ResourceState::kDepthWrite,
                                         .range = { .base_mip = 0, .mip_count = 1, .base_layer = 0, .layer_count = 1 },
                                         }
            };
            cmd.barrier({}, dbar);
            depth_initialized_on_gpu = true;
        }

        // Render pass with color + depth.
        std::array<cd::rhi::ColorAttachmentInfo, 1> color_attach {
            cd::rhi::ColorAttachmentInfo {
                                          .view = frame.swapchain_image_view,
                                          .load_op = cd::rhi::LoadOp::kClear,
                                          .store_op = cd::rhi::StoreOp::kStore,
                                          .clear_color = { .f32 = { 0.06F, 0.07F, 0.10F, 1.0F } },
                                          }
        };
        cd::rhi::DepthStencilAttachmentInfo depth_attach {};
        depth_attach.view = depth.view;
        depth_attach.depth_load = cd::rhi::LoadOp::kClear;
        depth_attach.depth_store = cd::rhi::StoreOp::kStore;
        depth_attach.clear.depth = 1.0F;
        cd::rhi::RenderPassBeginInfo rp {};
        rp.render_area = cd::rhi::Rect2D {
            { 0, 0 },
            frame.extent
        };
        rp.color_attachments = color_attach;
        rp.depth_stencil = &depth_attach;
        cmd.begin_render_pass(rp);
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

        // MVP — spin around the Y axis, slight tilt for depth illusion.
        const float elapsed = std::chrono::duration<float>(std::chrono::steady_clock::now() - t_start).count();
        const float angle = elapsed * 1.2F;                                   // rad/s
        cd::math::Transformf model_xf;
        model_xf.rotation = cd::math::Quatf { std::sin(angle * 0.5F) * 0.0F,  // x
                                              std::sin(angle * 0.5F),         // y
                                              std::sin(angle * 0.5F) * 0.0F,  // z
                                              std::cos(angle * 0.5F) };       // w
        const cd::math::Mat4f model = cd::math::to_mat4(model_xf);
        // Fixed framing — the cube spins, the camera does not. Re-derived
        // every frame (cheap) so any future per-frame Camera mutation just
        // works without restructuring.
        cd::camera::Camera cam {};
        cam.eye = { 2.5F, 1.6F, 2.5F };
        cam.target = { 0.0F, 0.0F, 0.0F };
        cam.fov_y = 1.0F;
        cam.near_z = 0.1F;
        cam.far_z = 100.0F;
        const float aspect = static_cast<float>(frame.extent.width) / static_cast<float>(frame.extent.height);
        const cd::math::Mat4f mvp = cd::camera::view_projection(cam, aspect) * model;

        material.apply(cmd);
        cmd.push_constants(
            material.pipeline_layout(),
            cd::rhi::ShaderStage::kVertex,
            /*offset=*/0,
            static_cast<std::uint32_t>(sizeof(mvp)),
            &mvp
        );
        cmd.bind_vertex_buffer(0, vb, 0);
        cmd.bind_index_buffer(ib, 0, cd::rhi::IndexType::kUInt16);
        cmd.draw_indexed(
            static_cast<std::uint32_t>(kIndices.size()),
            /*instance_count=*/1,
            0,
            0,
            0
        );
        cmd.end_render_pass();

        // Dispatch the submit + present to the render thread. The next
        // iteration's wait_idle() will pick up its outcome via a shared
        // atomic; for this sample we treat swapchain-out-of-date as a
        // mainline retry by latching the flag from the worker.
        std::atomic<bool> needs_rebuild_from_worker { false };
        async.enqueue(
            [&]
            {
                auto end_r = renderer.end_frame();
                if (!end_r.has_value())
                {
                    if (end_r.error().code
                        == static_cast<std::uint32_t>(cd::render::render_errors::Code::kSwapchainOutOfDate))
                    {
                        needs_rebuild_from_worker.store(true, std::memory_order_release);
                    }
                    else
                    {
                        std::fprintf(
                            stderr,
                            "end_frame: %.*s\n",
                            static_cast<int>(end_r.error().message.size()),
                            end_r.error().message.data()
                        );
                    }
                }
            }
        );
        // Drain immediately to capture the rebuild flag deterministically
        // for the next iteration. (Pipelining beyond one frame requires
        // an N-deep ring; that's Phase 5.)
        async.wait_idle();
        if (needs_rebuild_from_worker.load(std::memory_order_acquire))
            needs_rebuild = true;

        ++frame_idx;
    }

    async.wait_idle();
    renderer.wait_idle();
    depth.destroy(device);
    device.destroy_buffer(ib);
    device.destroy_buffer(vb);
    std::printf("hello_render_thread: clean exit (%llu submits via worker).\n",
                static_cast<unsigned long long>(async.completion_count()));
    return 0;
}
