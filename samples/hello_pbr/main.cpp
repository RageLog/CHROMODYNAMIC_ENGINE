// =============================================================================
// CHROMODYNAMIC — samples/hello_pbr
//
// Classic PBR showcase: a 5×5 sphere grid where the X axis sweeps metallic
// from 0 (dielectric) to 1 (metal) and the Y axis sweeps roughness from
// 0.05 (mirror) to 1 (matte). Single hard-coded directional light + a
// faint ambient. Albedo is the same warm copper-ish tone everywhere so
// the material parameter response is the only visual variable.
//
// Demonstrates that cd::material + cd::shader::ICompiler + the GLSL
// Cook-Torrance pipeline first introduced in hello_gltf works standalone
// (no asset import needed) and that the renderer can dispatch many
// draws with per-instance push constants.
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
#include <cd/render/Renderer.hpp>
#include <cd/rhi/Barriers.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi_vulkan/VulkanDevice.hpp>
#include <cd/shader/Compiler.hpp>

#include <array>
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
    float normal[3];
};

// Generate a UV-sphere of unit radius. `stacks` = horizontal rings,
// `slices` = vertical wedges. Returns vertex+index buffers ready for
// upload. For a 5×5 grid 32×16 is plenty (~512 vertices, ~960 tris).
struct SphereMesh
{
    std::vector<Vertex> vertices;
    std::vector<std::uint16_t> indices;
};

SphereMesh make_uv_sphere(int stacks, int slices)
{
    SphereMesh out;
    out.vertices.reserve(static_cast<std::size_t>((stacks + 1) * (slices + 1)));

    constexpr float kPi = 3.14159265358979F;
    for (int i = 0; i <= stacks; ++i)
    {
        const float v = static_cast<float>(i) / static_cast<float>(stacks);
        const float phi = v * kPi;  // 0..PI
        const float sin_phi = std::sin(phi);
        const float cos_phi = std::cos(phi);
        for (int j = 0; j <= slices; ++j)
        {
            const float u = static_cast<float>(j) / static_cast<float>(slices);
            const float theta = u * 2.0F * kPi;  // 0..2PI
            const float sin_t = std::sin(theta);
            const float cos_t = std::cos(theta);
            Vertex vtx;
            vtx.pos[0] = sin_phi * cos_t;
            vtx.pos[1] = cos_phi;
            vtx.pos[2] = sin_phi * sin_t;
            // Normal of a unit sphere equals position.
            vtx.normal[0] = vtx.pos[0];
            vtx.normal[1] = vtx.pos[1];
            vtx.normal[2] = vtx.pos[2];
            out.vertices.push_back(vtx);
        }
    }

    out.indices.reserve(static_cast<std::size_t>(stacks * slices * 6));
    for (int i = 0; i < stacks; ++i)
    {
        for (int j = 0; j < slices; ++j)
        {
            const std::uint16_t a = static_cast<std::uint16_t>(i * (slices + 1) + j);
            const std::uint16_t b = static_cast<std::uint16_t>(a + slices + 1);
            out.indices.push_back(a);
            out.indices.push_back(b);
            out.indices.push_back(static_cast<std::uint16_t>(a + 1));
            out.indices.push_back(b);
            out.indices.push_back(static_cast<std::uint16_t>(b + 1));
            out.indices.push_back(static_cast<std::uint16_t>(a + 1));
        }
    }
    return out;
}

// Push constant block — 144 bytes. Within Vulkan's 128-byte minimum
// guarantee? NO — but every desktop GPU supports at least 256 bytes,
// and we explicitly require Vulkan 1.3 (desktop-class). The asset_gltf
// sample uses 112; we add an extra vec4 for albedo to keep that source
// uncluttered.
//   0   mat4 mvp                  (64 B)
//   64  vec4 albedo               (16 B)
//   80  vec4 mr_amb (metallic, roughness, ambient, _)
//   96  vec4 camera_pos
//   112 vec4 light_dir            (xyz=light direction, w=intensity)
struct PushBlock
{
    cd::math::Mat4f mvp;
    std::array<float, 4> albedo;
    std::array<float, 4> mr_amb;
    std::array<float, 4> camera_pos;
    std::array<float, 4> light_dir;
};

static_assert(sizeof(PushBlock) == 128, "PushBlock size must equal 128 (Vulkan minimum push constant range)");

constexpr const char* kVS = R"glsl(
#version 450
layout(push_constant) uniform PC {
  mat4 mvp;
  vec4 albedo;
  vec4 mr_amb;
  vec4 camera_pos;
  vec4 light_dir;
} pc;
layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec3 in_normal;
layout(location = 0) out vec3 v_world_pos;
layout(location = 1) out vec3 v_normal;
void main() {
  // model = identity (sphere positions are already in world via mvp).
  v_world_pos = in_pos;
  v_normal = in_normal;
  vec4 clip = pc.mvp * vec4(in_pos, 1.0);
  clip.y = -clip.y;  // Vulkan NDC Y is down.
  gl_Position = clip;
}
)glsl";

constexpr const char* kFS = R"glsl(
#version 450
layout(push_constant) uniform PC {
  mat4 mvp;
  vec4 albedo;
  vec4 mr_amb;
  vec4 camera_pos;
  vec4 light_dir;
} pc;
layout(location = 0) in  vec3 v_world_pos;
layout(location = 1) in  vec3 v_normal;
layout(location = 0) out vec4 out_color;

const float PI = 3.14159265358979;

float D_GGX(float NoH, float a) {
  float a2 = a * a;
  float d  = (NoH * NoH) * (a2 - 1.0) + 1.0;
  return a2 / (PI * d * d + 1e-7);
}

float G_SchlickGGX(float NoV, float k) {
  return NoV / (NoV * (1.0 - k) + k + 1e-7);
}

float G_Smith(float NoV, float NoL, float roughness) {
  float r = roughness + 1.0;
  float k = (r * r) / 8.0;
  return G_SchlickGGX(NoV, k) * G_SchlickGGX(NoL, k);
}

vec3 F_Schlick(float HoV, vec3 F0) {
  return F0 + (vec3(1.0) - F0) * pow(clamp(1.0 - HoV, 0.0, 1.0), 5.0);
}

void main() {
  vec3 albedo = pc.albedo.rgb;
  float metallic = clamp(pc.mr_amb.x, 0.0, 1.0);
  float roughness = clamp(pc.mr_amb.y, 0.04, 1.0);
  float ambient   = pc.mr_amb.z;

  vec3 N = normalize(v_normal);
  vec3 V = normalize(pc.camera_pos.xyz - v_world_pos);
  vec3 L = normalize(-pc.light_dir.xyz);
  vec3 H = normalize(L + V);

  float NoL = max(dot(N, L), 0.0);
  float NoV = max(dot(N, V), 0.0);
  float NoH = max(dot(N, H), 0.0);
  float HoV = max(dot(H, V), 0.0);

  vec3 F0 = mix(vec3(0.04), albedo, metallic);
  float D = D_GGX(NoH, roughness * roughness);
  float G = G_Smith(NoV, NoL, roughness);
  vec3  F = F_Schlick(HoV, F0);

  vec3 specular = (D * G) * F / (4.0 * NoV * NoL + 1e-7);
  vec3 kS = F;
  vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);
  vec3 diffuse = kD * albedo / PI;
  vec3 lit = (diffuse + specular) * NoL * pc.light_dir.w;
  vec3 amb = ambient * albedo;
  vec3 color = amb + lit;

  // Reinhard tone-map + gamma.
  color = color / (color + vec3(1.0));
  color = pow(color, vec3(1.0 / 2.2));
  out_color = vec4(color, 1.0);
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

    cd::platform::WindowDesc wd {};
    wd.title = "CHROMODYNAMIC — hello_pbr (5x5 metallic/roughness sweep)";
    wd.width = 1280;
    wd.height = 720;
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

    // ---- Geometry: a single sphere shared across all 25 instances --------
    auto sphere = make_uv_sphere(32, 16);
    const std::span<const std::byte> vb_bytes { reinterpret_cast<const std::byte*>(sphere.vertices.data()),
                                                sphere.vertices.size() * sizeof(Vertex) };
    const std::span<const std::byte> ib_bytes { reinterpret_cast<const std::byte*>(sphere.indices.data()),
                                                sphere.indices.size() * sizeof(std::uint16_t) };
    auto vb = make_upload_buffer(device, vb_bytes, cd::rhi::BufferUsage::kVertex);
    auto ib = make_upload_buffer(device, ib_bytes, cd::rhi::BufferUsage::kIndex);
    if (!vb.is_valid() || !ib.is_valid())
        return 5;

    // ---- Material ---------------------------------------------------------
    auto compiler = cd::shader::make_glslang_compiler();
    if (compiler == nullptr)
        return 6;

    constexpr std::array<cd::rhi::VertexBinding, 1> kBindings {
        cd::rhi::VertexBinding { 0, sizeof(Vertex), false }
    };
    constexpr std::array<cd::rhi::VertexAttribute, 2> kAttrs {
        cd::rhi::VertexAttribute { 0, 0, cd::rhi::Format::kRGB32Float, offsetof(Vertex, pos)    },
        cd::rhi::VertexAttribute { 1, 0, cd::rhi::Format::kRGB32Float, offsetof(Vertex, normal) }
    };
    constexpr std::array<cd::rhi::Format, 1> kColorFormats { cd::rhi::Format::kBGRA8Unorm };
    constexpr std::array<cd::rhi::PushConstantRange, 1> kPush {
        cd::rhi::PushConstantRange { .stages = cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
                                    .offset = 0,
                                    .size = static_cast<std::uint32_t>(sizeof(PushBlock)) }
    };

    cd::material::MaterialDesc md {};
    md.vertex_glsl = kVS;
    md.fragment_glsl = kFS;
    md.color_attachment_formats = kColorFormats;
    md.depth_attachment_format = kDepthFormat;
    md.vertex_bindings = kBindings;
    md.vertex_attributes = kAttrs;
    md.push_constants = kPush;
    // Same Vulkan NDC Y-flip + default-cull trap as hello_anim (BUG #5).
    // `clip.y = -clip.y` in the vertex shader inverts winding after the
    // perspective divide; default cull=kBack + front_face=CCW then drops
    // the camera-facing triangles. Without this line the fragment shader
    // would only see the back hemisphere of each sphere, with its vertex
    // normals pointing away from the viewer — N·L and N·V both negative,
    // diffuse and Cook-Torrance specular both clamp to zero, and the
    // 5×5 metallic/roughness sweep looks like flat plastic. That's the
    // suspected root cause of BUG #2 from the v1.0 smoke.
    md.raster.cull = cd::rhi::CullMode::kNone;
    md.depth_stencil.depth_test = true;
    md.depth_stencil.depth_write = true;
    md.depth_stencil.depth_compare = cd::rhi::CompareOp::kLess;
    md.name = "hello_pbr/sphere";
    auto mat_r = cd::material::Material::create(device, compiler.get(), md);
    if (!mat_r.has_value())
        return 7;
    auto& material = *mat_r;

    // ---- Camera (cd::camera) ---------------------------------------------
    constexpr int kGrid = 5;
    constexpr float kSpacing = 2.4F;  // sphere ø=2 + breathing room.
    float orbit_angle = 0.0F;         // animated when !no_spin
    constexpr float kOrbitDistance = 14.0F;

    std::printf("hello_pbr: ready. 25 spheres (metallic 0..1 × roughness 0.05..1). ESC to exit.\n");
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
            if (!create_depth_target(device, { window.width(), window.height() }, kDepthFormat, depth))
                continue;
            depth_initialized_on_gpu = false;
            needs_rebuild = false;
        }

        if (!runtime.no_spin)
            orbit_angle += 0.005F;

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

        // First-time depth attachment transition UNDEFINED → DEPTH_WRITE.
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
                                          .clear_color = { .f32 = { 0.05F, 0.05F, 0.07F, 1.0F } } }
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
        material.apply(cmd);
        cmd.bind_vertex_buffer(0, vb, 0);
        cmd.bind_index_buffer(ib, 0, cd::rhi::IndexType::kUInt16);

        cd::camera::Camera cam {};
        cam.eye = { kOrbitDistance * std::sin(orbit_angle), 1.5F, kOrbitDistance * std::cos(orbit_angle) };
        cam.target = { 0.0F, 0.0F, 0.0F };
        cam.fov_y = 0.85F;
        cam.near_z = 0.1F;
        cam.far_z = 100.0F;
        const float aspect = static_cast<float>(frame.extent.width) / static_cast<float>(frame.extent.height);
        const cd::math::Mat4f vp = cd::camera::view_projection(cam, aspect);
        const std::array<float, 3> camera_pos { cam.eye.x, cam.eye.y, cam.eye.z };

        for (int row = 0; row < kGrid; ++row)
        {
            for (int col = 0; col < kGrid; ++col)
            {
                const float metallic = static_cast<float>(col) / static_cast<float>(kGrid - 1);
                const float roughness =
                    0.05F + (1.0F - 0.05F) * (static_cast<float>(row) / static_cast<float>(kGrid - 1));
                const float x = (static_cast<float>(col) - static_cast<float>(kGrid - 1) * 0.5F) * kSpacing;
                const float y = (static_cast<float>(row) - static_cast<float>(kGrid - 1) * 0.5F) * kSpacing;

                cd::math::Mat4f model = cd::math::Mat4f::identity();
                model[3][0] = x;
                model[3][1] = y;
                model[3][2] = 0.0F;
                const auto mvp = vp * model;

                PushBlock pb {};
                pb.mvp = mvp;
                pb.albedo = { 0.95F, 0.55F, 0.25F, 1.0F };
                pb.mr_amb = { metallic, roughness, 0.03F, 0.0F };
                pb.camera_pos = { camera_pos[0], camera_pos[1], camera_pos[2], 0.0F };
                pb.light_dir = { -0.4F, -0.6F, -0.7F, 4.0F };

                cmd.push_constants(
                    material.pipeline_layout(),
                    cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
                    /*offset=*/0,
                    static_cast<std::uint32_t>(sizeof(pb)),
                    &pb
                );
                cmd.draw_indexed(static_cast<std::uint32_t>(sphere.indices.size()), 1, 0, 0, 0);
            }
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
            return 9;
        }
        ++frame_idx;
    }

    renderer.wait_idle();
    depth.destroy(device);
    device.destroy_buffer(vb);
    device.destroy_buffer(ib);
    std::printf("hello_pbr: clean exit (%u frames).\n", frame_idx);
    return 0;
}
