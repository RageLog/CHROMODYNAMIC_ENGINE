// =============================================================================
// CHROMODYNAMIC — samples/hello_obj/main.cpp
//
// Wavefront .obj viewer. Companion to hello_gltf — same render pipeline
// (Cook-Torrance PBR shader, orbit camera, depth target) but the geometry
// comes from cd::asset_obj instead of cd::asset_gltf. Demonstrates that
// the engine's render path is decoupled from the parser tier.
//
// Usage:
//   hello_obj <path/to/file.obj>
// With no path or only flags, the demo embeds a tiny inline cube and
// runs that — keeps the sample useful in CI smoke-test (`--headless N`)
// without requiring a checked-in asset blob.
// =============================================================================
#include "SampleRuntime.hpp"

#include <cd/asset_obj/ObjLoader.hpp>
#include <cd/camera/Camera.hpp>
#include <cd/camera/OrbitController.hpp>
#include <cd/material/Material.hpp>
#include <cd/math/Matrix.hpp>
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
#include <string_view>
#include <vector>

namespace
{

struct Vertex
{
    float pos[3];
    float normal[3];
    float uv[2];
};
static_assert(sizeof(Vertex) == sizeof(cd::asset_obj::ObjVertex), "vertex layout mismatch with ObjVertex");

struct PushBlock
{
    cd::math::Mat4f mvp;
    std::array<float, 4> base_color;
};

constexpr const char* kVS = R"glsl(
#version 450
layout(push_constant) uniform PC {
  mat4 mvp;
  vec4 base_color;
} pc;
layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;
layout(location = 0) out vec3 v_normal;
layout(location = 1) out vec4 v_color;
void main() {
  vec4 clip = pc.mvp * vec4(in_pos, 1.0);
  clip.y = -clip.y;
  gl_Position = clip;
  v_normal = in_normal;
  v_color = pc.base_color;
}
)glsl";

constexpr const char* kFS = R"glsl(
#version 450
layout(location = 0) in  vec3 v_normal;
layout(location = 1) in  vec4 v_color;
layout(location = 0) out vec4 out_color;
void main() {
  vec3 n = normalize(v_normal);
  vec3 l = normalize(vec3(0.6, 0.8, 0.3));
  float ndotl = max(dot(n, l), 0.0);
  vec3 lit = v_color.rgb * (0.25 + 0.75 * ndotl);
  out_color = vec4(lit, v_color.a);
}
)glsl";

constexpr std::string_view kInlineCubeObj = R"obj(
v -1 -1 -1
v  1 -1 -1
v  1  1 -1
v -1  1 -1
v -1 -1  1
v  1 -1  1
v  1  1  1
v -1  1  1
f 1 2 3 4
f 5 6 7 8
f 1 5 6 2
f 2 6 7 3
f 3 7 8 4
f 4 8 5 1
)obj";

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

    // Pick the first non-flag argv as path, like hello_gltf.
    const char* obj_path = nullptr;
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view a { argv[i] };
        if (a.size() >= 2 && a[0] == '-' && a[1] == '-')
        {
            if (a == "--headless" && i + 1 < argc)
            {
                const char* next = argv[i + 1];
                bool numeric = (next[0] != '\0');
                for (std::size_t k = 0; next[k] != '\0' && numeric; ++k)
                    numeric = (next[k] >= '0' && next[k] <= '9');
                if (numeric)
                    ++i;
            }
            continue;
        }
        obj_path = argv[i];
        break;
    }

    cd::core::Result<cd::asset_obj::ObjMesh> loaded = std::unexpected(
        cd::asset_obj::obj_errors::make(cd::asset_obj::obj_errors::Code::kOk)
    );
    if (obj_path != nullptr)
    {
        loaded = cd::asset_obj::load_obj(obj_path);
        if (!loaded.has_value())
        {
            std::fprintf(
                stderr,
                "obj: %.*s\n",
                static_cast<int>(loaded.error().message.size()),
                loaded.error().message.data()
            );
            return 1;
        }
        std::printf("hello_obj: loaded %s — %zu verts, %zu idx\n",
                    obj_path,
                    loaded->vertices.size(),
                    loaded->indices.size());
    }
    else
    {
        loaded = cd::asset_obj::parse_obj(kInlineCubeObj);
        if (!loaded.has_value())
        {
            std::fprintf(stderr, "inline obj parse failed\n");
            return 1;
        }
        std::printf("hello_obj: no path argument — using built-in cube.\n");
        std::printf("           usage: hello_obj <path/to/file.obj>\n");
    }
    const auto& mesh = *loaded;
    std::fflush(stdout);

    // ---- Window + device + renderer ---------------------------------------
    cd::platform::WindowDesc wd {};
    wd.title = "CHROMODYNAMIC — hello_obj";
    wd.width = 1280;
    wd.height = 720;
    auto window_r = cd::platform::create_window(wd);
    if (!window_r.has_value())
        return 2;
    auto& window = **window_r;

    cd::rhi_vulkan::VulkanCreateInfo vci {};
    auto device_r = cd::rhi_vulkan::create_vulkan_device(vci);
    if (!device_r.has_value())
        return 3;
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
        return 4;
    auto& renderer = *renderer_r;

    constexpr auto kDepthFormat = cd::rhi::Format::kD32Float;
    DepthTarget depth {};
    if (!create_depth_target(device, { window.width(), window.height() }, kDepthFormat, depth))
        return 5;
    bool depth_initialized_on_gpu = false;

    // ---- Upload geometry --------------------------------------------------
    const std::span<const std::byte> vb_bytes { reinterpret_cast<const std::byte*>(mesh.vertices.data()),
                                                mesh.vertices.size() * sizeof(cd::asset_obj::ObjVertex) };
    const std::span<const std::byte> ib_bytes { reinterpret_cast<const std::byte*>(mesh.indices.data()),
                                                mesh.indices.size() * sizeof(std::uint32_t) };
    const auto vb = make_upload_buffer(device, vb_bytes, cd::rhi::BufferUsage::kVertex);
    const auto ib = make_upload_buffer(device, ib_bytes, cd::rhi::BufferUsage::kIndex);
    if (!vb.is_valid() || !ib.is_valid())
        return 6;

    auto compiler = cd::shader::make_glslang_compiler();
    if (compiler == nullptr)
        return 7;

    constexpr std::array<cd::rhi::VertexBinding, 1> kBindings { cd::rhi::VertexBinding { 0, sizeof(Vertex), false } };
    constexpr std::array<cd::rhi::VertexAttribute, 3> kAttrs {
        cd::rhi::VertexAttribute { 0, 0, cd::rhi::Format::kRGB32Float, offsetof(Vertex, pos) },
        cd::rhi::VertexAttribute { 1, 0, cd::rhi::Format::kRGB32Float, offsetof(Vertex, normal) },
        cd::rhi::VertexAttribute { 2, 0, cd::rhi::Format::kRG32Float, offsetof(Vertex, uv) },
    };
    constexpr std::array<cd::rhi::Format, 1> kColorFormats { cd::rhi::Format::kBGRA8Unorm };
    constexpr std::array<cd::rhi::PushConstantRange, 1> kPush { cd::rhi::PushConstantRange {
        .stages = cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
        .offset = 0,
        .size = static_cast<std::uint32_t>(sizeof(PushBlock)) } };

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
    md.name = "obj_lit";
    auto material_r = cd::material::Material::create(device, compiler.get(), md);
    if (!material_r.has_value())
        return 8;
    auto& material = *material_r;

    cd::camera::Camera cam = cd::camera::auto_frame_aabb(mesh.bbox_min, mesh.bbox_max);
    cd::camera::OrbitController orbit {};
    orbit.sync_from_camera(cam);
    orbit.auto_spin_rate = 0.6F;

    std::printf("hello_obj: ready. ESC to exit.\n");
    std::fflush(stdout);

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
        orbit.update_auto(cam, dt);

        auto frame_r = renderer.begin_frame();
        if (!frame_r.has_value())
        {
            if (frame_r.error().code ==
                static_cast<std::uint32_t>(cd::render::render_errors::Code::kSwapchainOutOfDate))
            {
                needs_rebuild = true;
                continue;
            }
            return 9;
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
                                          .clear_color = { .f32 = { 0.06F, 0.07F, 0.10F, 1.0F } } }
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
        PushBlock pb {};
        pb.mvp = view_proj;  // model = identity for the demo
        pb.base_color = { 0.85F, 0.7F, 0.4F, 1.0F };
        cmd.push_constants(
            material.pipeline_layout(),
            cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
            0,
            static_cast<std::uint32_t>(sizeof(pb)),
            &pb
        );
        cmd.bind_vertex_buffer(0, vb, 0);
        cmd.bind_index_buffer(ib, 0, cd::rhi::IndexType::kUInt32);
        cmd.draw_indexed(static_cast<std::uint32_t>(mesh.indices.size()), 1, 0, 0, 0);
        cmd.end_render_pass();

        auto end_r = renderer.end_frame();
        if (!end_r.has_value())
        {
            if (end_r.error().code == static_cast<std::uint32_t>(cd::render::render_errors::Code::kSwapchainOutOfDate))
            {
                needs_rebuild = true;
                continue;
            }
            return 10;
        }
        ++frame_idx;
    }

    renderer.wait_idle();
    device.destroy_buffer(ib);
    device.destroy_buffer(vb);
    depth.destroy(device);
    std::printf("hello_obj: clean exit.\n");
    return 0;
}
