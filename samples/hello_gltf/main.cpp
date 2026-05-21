// =============================================================================
// CHROMODYNAMIC — samples/hello_gltf/main.cpp
//
// Loads a glTF / glb file via cd::asset_gltf and renders the first mesh with
// per-vertex shading (NdotL against a fixed light + base-color factor from
// the first material). Exercises:
//   * cd::asset_gltf::load_gltf end-to-end (POSITION + NORMAL + indices)
//   * Auto-framing camera derived from the scene bbox
//   * Material with depth + push constants (reuses hello_cube wiring)
//   * Geometry uploaded once per scene primitive
//
// Usage:
//   hello_gltf <path/to/file.gltf|.glb>
// If no path is given the sample falls back to a hard-coded triangle so the
// pipeline still demonstrates "asset → GPU" end-to-end.
// =============================================================================
#include <cd/asset_gltf/GltfLoader.hpp>
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

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{

/// Vertex layout the GLSL pipeline expects. Matches the layout used by
/// cd::asset_gltf::GltfVertex byte-for-byte (position, normal, uv) so we can
/// memcpy a primitive's vertex span straight into a GPU buffer.
struct Vertex
{
    float pos[3];
    float normal[3];
    float uv[2];
};
static_assert(sizeof(Vertex) == sizeof(cd::asset_gltf::GltfVertex), "vertex layout mismatch with GltfVertex");
static_assert(offsetof(Vertex, pos) == offsetof(cd::asset_gltf::GltfVertex, position), "pos offset");
static_assert(offsetof(Vertex, normal) == offsetof(cd::asset_gltf::GltfVertex, normal), "normal offset");
static_assert(offsetof(Vertex, uv) == offsetof(cd::asset_gltf::GltfVertex, texcoord0), "uv offset");

/// MVP + per-material base color, packed into one push-constant block.
/// std140 alignment: mat4 (16 floats) followed by vec4 (4 floats) = 80 bytes.
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
  clip.y = -clip.y;  // Vulkan NDC Y is down; our math is Y-up.
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
  // Cheap Lambertian with a fixed light direction so geometry reads as 3D.
  vec3 n = normalize(v_normal);
  vec3 l = normalize(vec3(0.6, 0.8, 0.3));
  float ndotl = max(dot(n, l), 0.0);
  vec3 lit = v_color.rgb * (0.25 + 0.75 * ndotl);
  out_color = vec4(lit, v_color.a);
}
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

/// Per-frame depth resource, same shape as hello_cube — owned by the sample,
/// not the renderer.
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

/// One GPU-resident primitive ready to draw — owned by `Drawable` so the
/// destructor can release the underlying buffers without manual bookkeeping.
struct Drawable
{
    cd::rhi::BufferHandle vb {};
    cd::rhi::BufferHandle ib {};
    std::uint32_t index_count { 0 };
    std::array<float, 4> base_color { 1.0F, 1.0F, 1.0F, 1.0F };

    void destroy(cd::rhi::IDevice& dev)
    {
        if (ib.is_valid())
            dev.destroy_buffer(ib);
        if (vb.is_valid())
            dev.destroy_buffer(vb);
        ib = {};
        vb = {};
    }
};

/// Hard-coded triangle as a fallback when no glTF path is supplied. Lets the
/// demo prove the pipeline end-to-end without shipping a binary asset.
[[nodiscard]] cd::asset_gltf::GltfScene make_fallback_triangle()
{
    cd::asset_gltf::GltfScene scene;
    cd::asset_gltf::GltfMesh mesh;
    mesh.name = "fallback_triangle";
    cd::asset_gltf::GltfPrimitive prim;
    prim.vertices = {
        cd::asset_gltf::GltfVertex {
                                    .position = { 0.0F, 0.5F, 0.0F },
                                    .normal = { 0.0F, 0.0F, 1.0F },
                                    .texcoord0 = { 0.5F, 0.0F } },
        cd::asset_gltf::GltfVertex { .position = { -0.5F, -0.5F, 0.0F },
                                    .normal = { 0.0F, 0.0F, 1.0F },
                                    .texcoord0 = { 0.0F, 1.0F } },
        cd::asset_gltf::GltfVertex { .position = { 0.5F, -0.5F, 0.0F },
                                    .normal = { 0.0F, 0.0F, 1.0F },
                                    .texcoord0 = { 1.0F, 1.0F } },
    };
    prim.indices = { 0, 1, 2 };
    prim.material_index = -1;
    mesh.primitives.push_back(std::move(prim));
    scene.meshes.push_back(std::move(mesh));
    scene.bbox_min = { -0.5F, -0.5F, 0.0F };
    scene.bbox_max = { 0.5F, 0.5F, 0.0F };
    return scene;
}

}  // namespace

int main(int argc, char** argv)
{
    // ---- Load scene (file or fallback) ------------------------------------
    cd::asset_gltf::GltfScene scene;
    if (argc >= 2)
    {
        auto loaded = cd::asset_gltf::load_gltf(argv[1]);
        if (!loaded.has_value())
        {
            std::fprintf(
                stderr,
                "gltf: %.*s\n",
                static_cast<int>(loaded.error().message.size()),
                loaded.error().message.data()
            );
            return 1;
        }
        scene = std::move(*loaded);
        std::printf(
            "hello_gltf: loaded %s — %zu mesh(es), %zu material(s)\n",
            argv[1],
            scene.meshes.size(),
            scene.materials.size()
        );
    }
    else
    {
        scene = make_fallback_triangle();
        std::printf("hello_gltf: no path argument — using built-in fallback triangle.\n");
        std::printf("            usage: hello_gltf <path/to/file.gltf|.glb>\n");
    }
    std::fflush(stdout);

    // ---- Window + device + renderer ---------------------------------------
    cd::platform::WindowDesc wd {};
    wd.title = "CHROMODYNAMIC — hello_gltf";
    wd.width = 1280;
    wd.height = 720;
    auto window_r = cd::platform::create_window(wd);
    if (!window_r.has_value())
    {
        std::fprintf(
            stderr,
            "window: %.*s\n",
            static_cast<int>(window_r.error().message.size()),
            window_r.error().message.data()
        );
        return 2;
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
        return 3;
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
        return 4;
    }
    auto& renderer = *renderer_r;

    // ---- Depth target -----------------------------------------------------
    constexpr auto kDepthFormat = cd::rhi::Format::kD32Float;
    DepthTarget depth {};
    if (!create_depth_target(device, { window.width(), window.height() }, kDepthFormat, depth))
    {
        std::fprintf(stderr, "depth create failed\n");
        return 5;
    }
    bool depth_initialized_on_gpu = false;

    // ---- Upload every primitive ------------------------------------------
    std::vector<Drawable> drawables;
    drawables.reserve(16);
    for (const auto& mesh : scene.meshes)
    {
        for (const auto& prim : mesh.primitives)
        {
            if (prim.vertices.empty() || prim.indices.empty())
                continue;
            Drawable d {};
            const std::span<const std::byte> vb_bytes { reinterpret_cast<const std::byte*>(prim.vertices.data()),
                                                        prim.vertices.size() * sizeof(cd::asset_gltf::GltfVertex) };
            const std::span<const std::byte> ib_bytes { reinterpret_cast<const std::byte*>(prim.indices.data()),
                                                        prim.indices.size() * sizeof(std::uint32_t) };
            d.vb = make_upload_buffer(device, vb_bytes, cd::rhi::BufferUsage::kVertex);
            d.ib = make_upload_buffer(device, ib_bytes, cd::rhi::BufferUsage::kIndex);
            d.index_count = static_cast<std::uint32_t>(prim.indices.size());
            if (prim.material_index >= 0 &&
                static_cast<std::size_t>(prim.material_index) < scene.materials.size())
            {
                d.base_color = scene.materials[static_cast<std::size_t>(prim.material_index)].base_color_factor;
            }
            if (!d.vb.is_valid() || !d.ib.is_valid())
            {
                std::fprintf(stderr, "buffer upload failed for primitive\n");
                d.destroy(device);
                continue;
            }
            drawables.push_back(d);
        }
    }
    if (drawables.empty())
    {
        std::fprintf(stderr, "no drawable primitives — bailing out\n");
        return 6;
    }
    std::printf("hello_gltf: uploaded %zu primitive(s).\n", drawables.size());
    std::fflush(stdout);

    // ---- Material ---------------------------------------------------------
    auto compiler = cd::shader::make_glslang_compiler();
    if (compiler == nullptr)
    {
        std::fprintf(stderr, "no glslang\n");
        return 7;
    }

    constexpr std::array<cd::rhi::VertexBinding, 1> kBindings {
        cd::rhi::VertexBinding { 0, sizeof(Vertex), false }
    };
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
    md.raster.cull = cd::rhi::CullMode::kNone;  // glTF winding varies — disable culling.
    md.depth_stencil.depth_test = true;
    md.depth_stencil.depth_write = true;
    md.depth_stencil.depth_compare = cd::rhi::CompareOp::kLess;
    md.name = "gltf";
    auto material_r = cd::material::Material::create(device, compiler.get(), md);
    if (!material_r.has_value())
    {
        std::fprintf(
            stderr,
            "material: %.*s\n",
            static_cast<int>(material_r.error().message.size()),
            material_r.error().message.data()
        );
        return 8;
    }
    auto& material = *material_r;

    // ---- Camera framing from scene bbox -----------------------------------
    const cd::math::Vec3f center {
        (scene.bbox_min[0] + scene.bbox_max[0]) * 0.5F,
        (scene.bbox_min[1] + scene.bbox_max[1]) * 0.5F,
        (scene.bbox_min[2] + scene.bbox_max[2]) * 0.5F,
    };
    const cd::math::Vec3f extent {
        scene.bbox_max[0] - scene.bbox_min[0],
        scene.bbox_max[1] - scene.bbox_min[1],
        scene.bbox_max[2] - scene.bbox_min[2],
    };
    const float radius = 0.5F * std::sqrt(extent[0] * extent[0] + extent[1] * extent[1] + extent[2] * extent[2]);
    const float camera_distance = std::max(radius * 2.5F, 1.5F);
    const float near_z = std::max(camera_distance * 0.02F, 0.05F);
    const float far_z = std::max(camera_distance * 10.0F, 50.0F);

    std::printf("hello_gltf: ready. ESC or close to exit.\n");
    std::fflush(stdout);

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

    const auto t_start = std::chrono::steady_clock::now();
    while (true)
    {
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

        // Orbit camera around the centroid.
        const float elapsed = std::chrono::duration<float>(std::chrono::steady_clock::now() - t_start).count();
        const float angle = elapsed * 0.6F;
        const cd::math::Vec3f eye {
            center[0] + std::cos(angle) * camera_distance,
            center[1] + camera_distance * 0.45F,
            center[2] + std::sin(angle) * camera_distance,
        };
        const cd::math::Mat4f view = cd::math::look_at(eye, center, cd::math::Vec3f { 0.0F, 1.0F, 0.0F });
        const float aspect = static_cast<float>(frame.extent.width) / static_cast<float>(frame.extent.height);
        const cd::math::Mat4f proj =
            cd::math::perspective(/*fov_y_rad=*/1.0F, aspect, near_z, far_z);
        const cd::math::Mat4f view_proj = proj * view;

        material.apply(cmd);
        for (const auto& d : drawables)
        {
            PushBlock pb {};
            pb.mvp = view_proj;  // model is identity for now
            pb.base_color = d.base_color;
            cmd.push_constants(
                material.pipeline_layout(),
                cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
                /*offset=*/0,
                static_cast<std::uint32_t>(sizeof(pb)),
                &pb
            );
            cmd.bind_vertex_buffer(0, d.vb, 0);
            cmd.bind_index_buffer(d.ib, 0, cd::rhi::IndexType::kUInt32);
            cmd.draw_indexed(d.index_count, /*instance_count=*/1, 0, 0, 0);
        }
        cmd.end_render_pass();

        auto end_r = renderer.end_frame();
        if (!end_r.has_value())
        {
            if (end_r.error().code == static_cast<std::uint32_t>(cd::render::render_errors::Code::kSwapchainOutOfDate))
            {
                needs_rebuild = true;
                continue;
            }
            std::fprintf(
                stderr,
                "end_frame: %.*s\n",
                static_cast<int>(end_r.error().message.size()),
                end_r.error().message.data()
            );
            return 10;
        }
    }

    renderer.wait_idle();
    for (auto& d : drawables)
        d.destroy(device);
    depth.destroy(device);
    std::printf("hello_gltf: clean exit.\n");
    return 0;
}
