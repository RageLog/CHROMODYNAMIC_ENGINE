// =============================================================================
// CHROMODYNAMIC — samples/hello_cooked/main.cpp
//
// End-to-end runtime test of the cook → load → render pipeline:
//   1. (Build step) `cd_cook_mesh -i model.obj -o model.cdmesh`
//   2. (Runtime) hello_cooked .cdmesh path → cd::asset_cdmesh::load →
//      memcpy the byte blobs straight into GPU vertex/index buffers.
//
// The sample writes its own .cdmesh in a temp dir at startup (from an
// inline cube .obj parsed through cd::asset_obj + saved through
// cd::asset_cdmesh) so it always has something to draw without needing a
// pre-built asset.
//
// Usage:
//   hello_cooked [<path.cdmesh>]
//   hello_cooked --headless 3
// =============================================================================
#include "SampleRuntime.hpp"

#include <cd/asset_cdmesh/CdMesh.hpp>
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
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{

struct PushBlock
{
    cd::math::Mat4f mvp;
    std::array<float, 4> base_color;
};

constexpr const char* kVS = R"glsl(
#version 450
layout(push_constant) uniform PC { mat4 mvp; vec4 tint; } pc;
layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;
layout(location = 0) out vec3 v_normal;
void main() {
  vec4 clip = pc.mvp * vec4(in_pos, 1.0);
  clip.y = -clip.y;
  gl_Position = clip;
  v_normal = in_normal;
}
)glsl";

constexpr const char* kFS = R"glsl(
#version 450
layout(push_constant) uniform PC { mat4 mvp; vec4 tint; } pc;
layout(location = 0) in  vec3 v_normal;
layout(location = 0) out vec4 out_color;
void main() {
  vec3 n = normalize(v_normal);
  vec3 l = normalize(vec3(0.6, 0.8, 0.3));
  float ndotl = max(dot(n, l), 0.0);
  out_color = vec4(pc.tint.rgb * (0.25 + 0.75 * ndotl), 1.0);
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

[[nodiscard]] std::string make_self_cooked_cube()
{
    // 1. Parse inline cube obj.
    auto obj = cd::asset_obj::parse_obj(kInlineCubeObj);
    if (!obj.has_value())
        return {};
    // 2. Pick a unique temp path so repeated runs don't collide.
    static std::atomic<std::uint64_t> seq { 0 };
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() /
                      ("cd_cooked_cube_" + std::to_string(static_cast<std::uint64_t>(stamp)) + "_" +
                       std::to_string(seq.fetch_add(1)) + ".cdmesh");
    // 3. Cook.
    cd::asset_cdmesh::SaveDesc d {};
    d.vertices = { reinterpret_cast<const std::uint8_t*>(obj->vertices.data()),
                   obj->vertices.size() * sizeof(cd::asset_obj::ObjVertex) };
    d.indices = { reinterpret_cast<const std::uint8_t*>(obj->indices.data()),
                  obj->indices.size() * sizeof(std::uint32_t) };
    d.vertex_count = static_cast<std::uint32_t>(obj->vertices.size());
    d.index_count = static_cast<std::uint32_t>(obj->indices.size());
    d.vertex_stride = sizeof(cd::asset_obj::ObjVertex);
    d.index_stride = 4;
    d.bbox_min = obj->bbox_min;
    d.bbox_max = obj->bbox_max;
    if (!cd::asset_cdmesh::save(path.string(), d).has_value())
        return {};
    return path.string();
}

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

    // ---- Pick path: explicit argv > self-cooked fallback -------------------
    std::string path;
    bool self_cooked = false;
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
        path = argv[i];
        break;
    }
    if (path.empty())
    {
        path = make_self_cooked_cube();
        if (path.empty())
        {
            std::fprintf(stderr, "hello_cooked: failed to self-cook fallback cube\n");
            return 1;
        }
        self_cooked = true;
        std::printf("hello_cooked: self-cooked %s\n", path.c_str());
    }

    // ---- Load .cdmesh -----------------------------------------------------
    auto cooked = cd::asset_cdmesh::load(path);
    if (!cooked.has_value())
    {
        std::fprintf(
            stderr,
            "hello_cooked: load failed: %.*s\n",
            static_cast<int>(cooked.error().message.size()),
            cooked.error().message.data()
        );
        return 2;
    }
    const auto& mesh = *cooked;
    std::printf(
        "hello_cooked: loaded %s (verts=%u stride=%u idx=%u stride=%u)\n",
        path.c_str(),
        mesh.vertex_count,
        mesh.vertex_stride,
        mesh.index_count,
        mesh.index_stride
    );
    std::fflush(stdout);

    // ---- Window + device + renderer ---------------------------------------
    cd::platform::WindowDesc wd {};
    wd.title = "CHROMODYNAMIC — hello_cooked (cdmesh roundtrip)";
    wd.width = 1024;
    wd.height = 768;
    auto window_r = cd::platform::create_window(wd);
    if (!window_r.has_value())
        return 3;
    auto& window = **window_r;

    cd::rhi_vulkan::VulkanCreateInfo vci {};
    auto device_r = cd::rhi_vulkan::create_vulkan_device(vci);
    if (!device_r.has_value())
        return 4;
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
        return 5;
    auto& renderer = *renderer_r;

    constexpr auto kDepthFormat = cd::rhi::Format::kD32Float;
    DepthTarget depth {};
    if (!create_depth_target(device, { window.width(), window.height() }, kDepthFormat, depth))
        return 6;
    bool depth_initialized_on_gpu = false;

    // ---- Upload geometry STRAIGHT from cdmesh blobs -----------------------
    const std::span<const std::byte> vb_bytes { reinterpret_cast<const std::byte*>(mesh.vertex_blob.data()),
                                                mesh.vertex_blob.size() };
    const std::span<const std::byte> ib_bytes { reinterpret_cast<const std::byte*>(mesh.index_blob.data()),
                                                mesh.index_blob.size() };
    const auto vb = make_upload_buffer(device, vb_bytes, cd::rhi::BufferUsage::kVertex);
    const auto ib = make_upload_buffer(device, ib_bytes, cd::rhi::BufferUsage::kIndex);
    if (!vb.is_valid() || !ib.is_valid())
        return 7;

    auto compiler = cd::shader::make_glslang_compiler();
    if (compiler == nullptr)
        return 8;

    // Vertex format matches cd::asset_cdmesh::CdVertexStd / ObjVertex /
    // GltfVertex byte-for-byte (32B: vec3 pos + vec3 normal + vec2 uv).
    struct Vtx
    {
        float pos[3];
        float normal[3];
        float uv[2];
    };
    static_assert(sizeof(Vtx) == 32);

    constexpr std::array<cd::rhi::VertexBinding, 1> kBindings { cd::rhi::VertexBinding { 0, sizeof(Vtx), false } };
    constexpr std::array<cd::rhi::VertexAttribute, 3> kAttrs {
        cd::rhi::VertexAttribute { 0, 0, cd::rhi::Format::kRGB32Float, offsetof(Vtx, pos) },
        cd::rhi::VertexAttribute { 1, 0, cd::rhi::Format::kRGB32Float, offsetof(Vtx, normal) },
        cd::rhi::VertexAttribute { 2, 0, cd::rhi::Format::kRG32Float, offsetof(Vtx, uv) },
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
    md.name = "cooked";
    auto material_r = cd::material::Material::create(device, compiler.get(), md);
    if (!material_r.has_value())
        return 9;
    auto& material = *material_r;

    cd::camera::Camera cam = cd::camera::auto_frame_aabb(mesh.bbox_min, mesh.bbox_max);
    cd::camera::OrbitController orbit {};
    orbit.sync_from_camera(cam);
    orbit.auto_spin_rate = 0.6F;

    std::printf("hello_cooked: ready. ESC to exit.\n");
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
            return 10;
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
                                          .clear_color = { .f32 = { 0.05F, 0.07F, 0.10F, 1.0F } } }
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
        pb.mvp = view_proj;
        pb.base_color = { 0.35F, 0.85F, 0.55F, 1.0F };
        cmd.push_constants(
            material.pipeline_layout(),
            cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
            0,
            static_cast<std::uint32_t>(sizeof(pb)),
            &pb
        );
        cmd.bind_vertex_buffer(0, vb, 0);
        cmd.bind_index_buffer(ib, 0, cd::rhi::IndexType::kUInt32);
        cmd.draw_indexed(mesh.index_count, 1, 0, 0, 0);
        cmd.end_render_pass();

        auto end_r = renderer.end_frame();
        if (!end_r.has_value())
        {
            if (end_r.error().code == static_cast<std::uint32_t>(cd::render::render_errors::Code::kSwapchainOutOfDate))
            {
                needs_rebuild = true;
                continue;
            }
            return 11;
        }
        ++frame_idx;
    }

    renderer.wait_idle();
    device.destroy_buffer(ib);
    device.destroy_buffer(vb);
    depth.destroy(device);

    // Clean up self-cooked tmp file so we don't pile them up.
    if (self_cooked)
    {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }

    std::printf("hello_cooked: clean exit.\n");
    return 0;
}
