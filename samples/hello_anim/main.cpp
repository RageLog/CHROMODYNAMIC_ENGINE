// =============================================================================
// CHROMODYNAMIC — samples/hello_anim
//
// Drives a cube's transform from a cd::anim::AnimationClip + AnimationPlayer.
// 4 keyframes over 4 seconds — translate along the perimeter of a square,
// each corner with a 90° rotation around Y and a slight scale pulse. Loop
// mode kLoop closes the cycle so the cube returns to the start every cycle.
//
// Exercises: cd::anim::AnimationClip / AnimationPlayer.update(dt) → live
// cd::math::Transformf → model matrix push-constant. No skinning yet;
// per-bone tracks are a follow-up sprint (see Phase 4 closure ADR S4.2.b).
// =============================================================================
#include "GoldenCapture.hpp"
#include "SampleRuntime.hpp"

#include <cd/anim/Animation.hpp>
#include <cd/camera/Camera.hpp>
#include <cd/material/Material.hpp>
#include <cd/math/Matrix.hpp>
#include <cd/math/Quaternion.hpp>
#include <cd/math/Transform.hpp>
#include <cd/math/Vector.hpp>
#include <cd/platform/Window.hpp>
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

constexpr std::array<Vertex, 8> kVerts {
    {
     { { -0.4F, -0.4F, -0.4F }, { 0.20F, 0.40F, 1.00F } },
     { { 0.4F, -0.4F, -0.4F }, { 1.00F, 0.40F, 0.20F } },
     { { 0.4F, 0.4F, -0.4F }, { 0.95F, 0.85F, 0.10F } },
     { { -0.4F, 0.4F, -0.4F }, { 0.20F, 0.85F, 0.30F } },
     { { -0.4F, -0.4F, 0.4F }, { 0.40F, 0.20F, 0.85F } },
     { { 0.4F, -0.4F, 0.4F }, { 0.85F, 0.20F, 0.50F } },
     { { 0.4F, 0.4F, 0.4F }, { 0.90F, 0.90F, 0.90F } },
     { { -0.4F, 0.4F, 0.4F }, { 0.10F, 0.95F, 0.85F } },
     }
};

constexpr std::array<std::uint16_t, 36> kIndices {
    0, 1, 2, 0, 2, 3, 4, 6, 5, 4, 7, 6, 0, 3, 7, 0, 7, 4, 1, 5, 6, 1, 6, 2, 0, 4, 5, 0, 5, 1, 3, 2, 6, 3, 6, 7,
};

constexpr const char* kVS = R"glsl(
#version 450
layout(push_constant) uniform PC { mat4 mvp; } pc;
layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec3 in_color;
layout(location = 0) out vec3 v_color;
void main() {
  vec4 clip = pc.mvp * vec4(in_pos, 1.0);
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

struct DepthTarget
{
    cd::rhi::TextureHandle image {};
    cd::rhi::TextureViewHandle view {};

    void destroy(cd::rhi::IDevice& dev)
    {
        if (view.is_valid())
            dev.destroy_texture_view(view);
        if (image.is_valid())
            dev.destroy_texture(image);
        image = {};
        view = {};
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
    return true;
}

/// Build a 4-corner square-perimeter clip:
///   t=0: pos=(-1,0,0)  scale=1.0
///   t=1: pos=( 0,1,0)  scale=1.2 + 90° Y rotation
///   t=2: pos=( 1,0,0)  scale=1.0 + 180°
///   t=3: pos=( 0,-1,0) scale=1.2 + 270°
///   t=4: pos=(-1,0,0)  scale=1.0 + 360°
[[nodiscard]] cd::anim::AnimationClip make_square_clip()
{
    constexpr float kPi = 3.14159265358979F;
    auto quat_y = [](float deg)
    {
        const float half = deg * kPi / 180.0F * 0.5F;
        return cd::math::Quatf { 0.0F, std::sin(half), 0.0F, std::cos(half) };
    };
    std::vector<cd::anim::Keyframe> frames;
    frames.reserve(5);
    auto push = [&](float t, cd::math::Vec3f pos, float scale_uniform, float deg)
    {
        cd::anim::Keyframe k;
        k.time = t;
        k.value.position = pos;
        k.value.scale = { scale_uniform, scale_uniform, scale_uniform };
        k.value.rotation = quat_y(deg);
        frames.push_back(k);
    };
    push(0.0F, { -1.0F, 0.0F, 0.0F }, 1.0F, 0.0F);
    push(1.0F, { 0.0F, 1.0F, 0.0F }, 1.2F, 90.0F);
    push(2.0F, { 1.0F, 0.0F, 0.0F }, 1.0F, 180.0F);
    push(3.0F, { 0.0F, -1.0F, 0.0F }, 1.2F, 270.0F);
    push(4.0F, { -1.0F, 0.0F, 0.0F }, 1.0F, 360.0F);
    return cd::anim::AnimationClip { std::move(frames) };
}

}  // namespace

int main(int argc, char** argv)
{
    const cd::sample::Runtime runtime = cd::sample::parse_runtime(argc, argv);

    cd::platform::WindowDesc wd {};
    wd.title = "CHROMODYNAMIC — hello_anim (keyframe-driven cube)";
    wd.width = 1024;
    wd.height = 768;
    auto window_r = cd::platform::create_window(wd);
    if (!window_r.has_value())
        return 1;
    auto& window = **window_r;

    cd::rhi_vulkan::VulkanCreateInfo vci {};
    auto device_r = cd::rhi_vulkan::create_vulkan_device(vci);
    if (!device_r.has_value())
        return 2;
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
        return 3;
    auto& renderer = *renderer_r;

    constexpr auto kDepthFormat = cd::rhi::Format::kD32Float;
    DepthTarget depth {};
    if (!create_depth_target(device, { window.width(), window.height() }, kDepthFormat, depth))
        return 4;
    bool depth_initialized_on_gpu = false;

    const std::span<const std::byte> vb_bytes { reinterpret_cast<const std::byte*>(kVerts.data()),
                                                kVerts.size() * sizeof(Vertex) };
    const std::span<const std::byte> ib_bytes { reinterpret_cast<const std::byte*>(kIndices.data()),
                                                kIndices.size() * sizeof(std::uint16_t) };
    auto vb = make_upload_buffer(device, vb_bytes, cd::rhi::BufferUsage::kVertex);
    auto ib = make_upload_buffer(device, ib_bytes, cd::rhi::BufferUsage::kIndex);
    if (!vb.is_valid() || !ib.is_valid())
        return 5;

    auto compiler = cd::shader::make_glslang_compiler();
    if (compiler == nullptr)
        return 6;

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
    // The vertex shader applies `clip.y = -clip.y` (Vulkan NDC Y-flip),
    // which inverts triangle winding from CCW (as authored in kIndices)
    // to CW after the perspective divide. Pipeline default
    // front_face = kCounterClockwise + cull = kBack would then drop the
    // camera-facing faces and leave only the back faces visible
    // ("see-through cube" — BUG #5 in v1.0 rollback). hello_cube avoids
    // this by setting kNone; we match.
    md.raster.cull = cd::rhi::CullMode::kNone;
    md.depth_stencil.depth_test = true;
    md.depth_stencil.depth_write = true;
    md.depth_stencil.depth_compare = cd::rhi::CompareOp::kLess;
    md.name = "hello_anim/cube";
    auto mat_r = cd::material::Material::create(device, compiler.get(), md);
    if (!mat_r.has_value())
        return 7;
    auto& material = *mat_r;

    // ---- cd::anim setup ---------------------------------------------------
    const auto clip = make_square_clip();
    cd::anim::AnimationPlayer player { &clip };
    player.set_loop_mode(cd::anim::LoopMode::kLoop);
    player.set_speed(1.0F);
    player.play();

    std::printf("hello_anim: ready. cd::anim drives the cube transform. ESC to exit.\n");
    std::fflush(stdout);

    std::vector<cd::platform::OSEvent> events;
    events.reserve(64);
    bool needs_rebuild = false;
    auto t_prev = std::chrono::steady_clock::now();
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
            if (!create_depth_target(device, { window.width(), window.height() }, kDepthFormat, depth))
                continue;
            depth_initialized_on_gpu = false;
            needs_rebuild = false;
        }

        // Tick the animation. In headless mode (--no-spin) we force dt=0 to
        // pin the cube on its first keyframe for deterministic golden-image
        // tests later.
        const auto t_now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(t_now - t_prev).count();
        t_prev = t_now;
        if (runtime.no_spin)
            dt = 0.0F;
        const cd::math::Transformf pose = player.update(dt);
        const cd::math::Mat4f model = cd::math::to_mat4(pose);

        auto frame_r = renderer.begin_frame();
        if (!frame_r.has_value())
        {
            if (frame_r.error().code ==
                static_cast<std::uint32_t>(cd::render::render_errors::Code::kSwapchainOutOfDate))
            {
                needs_rebuild = true;
                continue;
            }
            return 8;
        }
        auto& frame = *frame_r;
        auto& cmd = *frame.command_buffer;

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

        std::array<cd::rhi::ColorAttachmentInfo, 1> color_attach {
            cd::rhi::ColorAttachmentInfo { .view = frame.swapchain_image_view,
                                          .load_op = cd::rhi::LoadOp::kClear,
                                          .store_op = cd::rhi::StoreOp::kStore,
                                          .clear_color = { .f32 = { 0.10F, 0.10F, 0.12F, 1.0F } } }
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

        cd::camera::Camera cam {};
        cam.eye = { 0.0F, 1.5F, 4.5F };
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
        cmd.draw_indexed(static_cast<std::uint32_t>(kIndices.size()), 1, 0, 0, 0);

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
                needs_rebuild = true;
                continue;
            }
            return 9;
        }
        ++frame_idx;
    }

    renderer.wait_idle();

    int golden_rc = 0;
    if (golden_state.armed)
        golden_rc = cd::sample::finish_golden_capture(device, runtime, golden_state);

    depth.destroy(device);
    device.destroy_buffer(vb);
    device.destroy_buffer(ib);
    std::printf("hello_anim: clean exit (%u frames, t=%.2fs).\n", frame_idx, static_cast<double>(player.time()));
    return golden_rc;
}
