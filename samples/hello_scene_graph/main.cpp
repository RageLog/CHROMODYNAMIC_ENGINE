// =============================================================================
// CHROMODYNAMIC — samples/hello_scene_graph/main.cpp
//
// Visual demo of cd::scene + cd::ecs nested transforms:
//   * A "sun"  cube at the origin slowly spinning
//   * A "planet" cube orbiting the sun, attached as a child node so
//     scene.update_transforms() composes its world matrix from
//     world_sun · local_planet automatically
//   * A "moon" cube orbiting the planet, attached as a grandchild — its
//     world matrix is world_sun · local_planet · local_moon
//
// Confirms end-to-end that the existing cd::ecs::World + cd::scene::Scene
// stack (already covered by 9 unit tests in engine/world/scene/tests) hangs
// together with the render pipeline and cd::camera helpers.
// =============================================================================
#include "SampleRuntime.hpp"

#include <cd/camera/Camera.hpp>
#include <cd/camera/OrbitController.hpp>
#include <cd/ecs/World.hpp>
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
#include <cd/scene/Scene.hpp>
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

// Unit cube — same geometry as hello_cube, but colors are per-instance
// (via push-constant `tint`) so one VB serves all three bodies. Every
// vertex carries (1,1,1) so the tint passes through unchanged; the earlier
// hand-rolled per-vertex-position gradient was making the corner at
// (-0.5, -0.5, -0.5) interpolate to BLACK and showed up as dark wedges on
// every face touching vertex 0.
constexpr std::array<Vertex, 8> kVerts {
    {
     { { -0.5F, -0.5F, -0.5F }, { 1.0F, 1.0F, 1.0F } },
     { { 0.5F, -0.5F, -0.5F }, { 1.0F, 1.0F, 1.0F } },
     { { 0.5F, 0.5F, -0.5F }, { 1.0F, 1.0F, 1.0F } },
     { { -0.5F, 0.5F, -0.5F }, { 1.0F, 1.0F, 1.0F } },
     { { -0.5F, -0.5F, 0.5F }, { 1.0F, 1.0F, 1.0F } },
     { { 0.5F, -0.5F, 0.5F }, { 1.0F, 1.0F, 1.0F } },
     { { 0.5F, 0.5F, 0.5F }, { 1.0F, 1.0F, 1.0F } },
     { { -0.5F, 0.5F, 0.5F }, { 1.0F, 1.0F, 1.0F } },
     }
};

constexpr std::array<std::uint16_t, 36> kIndices {
    0, 1, 2, 0, 2, 3,  // back
    4, 6, 5, 4, 7, 6,  // front
    0, 3, 7, 0, 7, 4,  // left
    1, 5, 6, 1, 6, 2,  // right
    0, 4, 5, 0, 5, 1,  // bottom
    3, 2, 6, 3, 6, 7,  // top
};

constexpr const char* kVS = R"glsl(
#version 450
layout(push_constant) uniform PC {
  mat4 mvp;
  vec4 tint;     // per-body color multiplier
} pc;
layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec3 in_color;
layout(location = 0) out vec3 v_color;
void main() {
  vec4 clip = pc.mvp * vec4(in_pos, 1.0);
  clip.y = -clip.y;
  gl_Position = clip;
  v_color = in_color * pc.tint.rgb;
}
)glsl";

constexpr const char* kFS = R"glsl(
#version 450
layout(location = 0) in  vec3 v_color;
layout(location = 0) out vec4 out_color;
void main() { out_color = vec4(v_color, 1.0); }
)glsl";

struct PushBlock
{
    cd::math::Mat4f mvp;
    std::array<float, 4> tint;
};

/// Per-renderable component. Holds the per-instance tint and a uniform
/// scale (the scene-graph LocalTransform's `scale` field handles dimensional
/// scaling; this is for shader-side color, not size).
struct Renderable
{
    std::array<float, 4> tint { 1.0F, 1.0F, 1.0F, 1.0F };
};

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

}  // namespace

int main(int argc, char** argv)
{
    const cd::sample::Runtime runtime = cd::sample::parse_runtime(argc, argv);

    // ---- Window + device + renderer ---------------------------------------
    cd::platform::WindowDesc wd {};
    wd.title = "CHROMODYNAMIC — hello_scene_graph (nested transforms)";
    wd.width = 1280;
    wd.height = 720;
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

    // ---- Depth + cube geometry --------------------------------------------
    constexpr auto kDepthFormat = cd::rhi::Format::kD32Float;
    DepthTarget depth {};
    if (!create_depth_target(device, { window.width(), window.height() }, kDepthFormat, depth))
        return 4;
    bool depth_initialized_on_gpu = false;

    const std::span<const std::byte> vb_bytes { reinterpret_cast<const std::byte*>(kVerts.data()),
                                                kVerts.size() * sizeof(Vertex) };
    const std::span<const std::byte> ib_bytes { reinterpret_cast<const std::byte*>(kIndices.data()),
                                                kIndices.size() * sizeof(std::uint16_t) };
    const auto vb = make_upload_buffer(device, vb_bytes, cd::rhi::BufferUsage::kVertex);
    const auto ib = make_upload_buffer(device, ib_bytes, cd::rhi::BufferUsage::kIndex);
    if (!vb.is_valid() || !ib.is_valid())
        return 5;

    // ---- Material ----------------------------------------------------------
    auto compiler = cd::shader::make_glslang_compiler();
    if (compiler == nullptr)
        return 6;

    constexpr std::array<cd::rhi::VertexBinding, 1> kBindings {
        cd::rhi::VertexBinding { 0, sizeof(Vertex), false }
    };
    constexpr std::array<cd::rhi::VertexAttribute, 2> kAttrs {
        cd::rhi::VertexAttribute { 0, 0, cd::rhi::Format::kRGB32Float, offsetof(Vertex, pos)   },
        cd::rhi::VertexAttribute { 1, 0, cd::rhi::Format::kRGB32Float, offsetof(Vertex, color) },
    };
    constexpr std::array<cd::rhi::Format, 1> kColorFormats { cd::rhi::Format::kBGRA8Unorm };
    constexpr std::array<cd::rhi::PushConstantRange, 1> kPush {
        cd::rhi::PushConstantRange { .stages = cd::rhi::ShaderStage::kVertex,
                                    .offset = 0,
                                    .size = static_cast<std::uint32_t>(sizeof(PushBlock)) }
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
    md.name = "scene_graph_body";
    auto material_r = cd::material::Material::create(device, compiler.get(), md);
    if (!material_r.has_value())
        return 7;
    auto& material = *material_r;

    // ---- Scene graph: sun → planet → moon --------------------------------
    cd::ecs::World world;
    cd::scene::Scene scene { world };

    const auto sun = scene.create_node();
    world.emplace<Renderable>(
        sun,
        Renderable {
            { 1.0F, 0.9F, 0.4F, 1.0F }
    }
    );
    // Sun's LocalTransform stays at the origin; its `scale` makes it big.
    if (auto* lt = scene.local(sun))
        lt->value.scale = { 0.8F, 0.8F, 0.8F };

    const auto planet = scene.create_node();
    world.emplace<Renderable>(
        planet,
        Renderable {
            { 0.3F, 0.5F, 1.0F, 1.0F }
    }
    );
    if (auto* lt = scene.local(planet))
        lt->value.scale = { 0.45F, 0.45F, 0.45F };
    scene.attach(planet, sun);

    const auto moon = scene.create_node();
    world.emplace<Renderable>(
        moon,
        Renderable {
            { 0.85F, 0.85F, 0.9F, 1.0F }
    }
    );
    if (auto* lt = scene.local(moon))
        lt->value.scale = { 0.2F, 0.2F, 0.2F };
    scene.attach(moon, planet);

    std::printf(
        "hello_scene_graph: sun=(%u,%u) planet=(%u,%u) moon=(%u,%u)\n",
        sun.id,
        sun.generation,
        planet.id,
        planet.generation,
        moon.id,
        moon.generation
    );
    std::printf("hello_scene_graph: ready. ESC to exit.\n");
    std::fflush(stdout);

    // ---- Camera + orbit controller --------------------------------------
    cd::camera::Camera cam {};
    cam.eye = { 8.0F, 5.0F, 8.0F };
    cam.target = { 0.0F, 0.0F, 0.0F };
    cam.fov_y = 1.0F;
    cam.near_z = 0.1F;
    cam.far_z = 100.0F;
    cd::camera::OrbitController orbit {};
    orbit.sync_from_camera(cam);
    orbit.auto_spin_rate = 0.15F;  // slow drift so the user can read the hierarchy.

    // ---- Main loop --------------------------------------------------------
    std::vector<cd::platform::OSEvent> events;
    events.reserve(64);
    bool needs_rebuild = false;
    auto rebuild = [&]
    {
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

    auto t_prev = std::chrono::steady_clock::now();
    float t_total = 0.0F;
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
        if (needs_rebuild && !rebuild())
            continue;

        const auto t_now = std::chrono::steady_clock::now();
        const float dt = std::chrono::duration<float>(t_now - t_prev).count();
        t_prev = t_now;
        t_total += dt;
        orbit.update_auto(cam, dt);

        // ---- Animate the scene graph -------------------------------------
        // Sun spins lazily in place.
        if (auto* sl = scene.local(sun))
        {
            const float a = t_total * 0.4F;
            sl->value.rotation = { 0.0F, std::sin(a * 0.5F), 0.0F, std::cos(a * 0.5F) };
        }
        // Planet orbits the sun (radius 3) while spinning.
        if (auto* pl = scene.local(planet))
        {
            const float a = t_total * 0.9F;
            pl->value.position = { std::cos(a) * 3.0F, 0.0F, std::sin(a) * 3.0F };
            const float spin = t_total * 1.8F;
            pl->value.rotation = { 0.0F, std::sin(spin * 0.5F), 0.0F, std::cos(spin * 0.5F) };
        }
        // Moon orbits the planet (radius 1) in its LOCAL frame — the scene
        // graph propagation does the rest.
        if (auto* ml = scene.local(moon))
        {
            const float a = t_total * 2.5F;
            ml->value.position = { std::cos(a) * 1.0F, 0.0F, std::sin(a) * 1.0F };
        }
        scene.update_transforms();

        // ---- Render --------------------------------------------------------
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
            cd::rhi::ColorAttachmentInfo {
                                          .view = frame.swapchain_image_view,
                                          .load_op = cd::rhi::LoadOp::kClear,
                                          .store_op = cd::rhi::StoreOp::kStore,
                                          .clear_color = { .f32 = { 0.04F, 0.05F, 0.08F, 1.0F } },
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

        const float aspect = static_cast<float>(frame.extent.width) / static_cast<float>(frame.extent.height);
        const cd::math::Mat4f view_proj = cd::camera::view_projection(cam, aspect);

        material.apply(cmd);
        cmd.bind_vertex_buffer(0, vb, 0);
        cmd.bind_index_buffer(ib, 0, cd::rhi::IndexType::kUInt16);

        // One draw per renderable. Combine view_proj with the world matrix
        // the scene graph computed for each entity.
        world.for_each<Renderable>(
            [&](cd::ecs::Entity e, Renderable& r)
            {
                const auto* wt = scene.world_transform(e);
                if (wt == nullptr)
                    return;
                PushBlock pb {};
                pb.mvp = view_proj * wt->matrix;
                pb.tint = r.tint;
                cmd.push_constants(
                    material.pipeline_layout(),
                    cd::rhi::ShaderStage::kVertex,
                    0,
                    static_cast<std::uint32_t>(sizeof(pb)),
                    &pb
                );
                cmd.draw_indexed(static_cast<std::uint32_t>(kIndices.size()), 1, 0, 0, 0);
            }
        );
        cmd.end_render_pass();

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
    device.destroy_buffer(ib);
    device.destroy_buffer(vb);
    depth.destroy(device);
    std::printf("hello_scene_graph: clean exit.\n");
    return 0;
}
