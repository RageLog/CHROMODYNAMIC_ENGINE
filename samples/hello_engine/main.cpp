// =============================================================================
// CHROMODYNAMIC — samples/hello_engine
//
// Phase 138 / v0.99.64 — mega-showcase: a single ImGui-docked window
// running every major marathon subsystem live so a fresh observer can
// see "what does this engine do today?" in one place.
//
// Panels rendered (DockSpace, all visible at once):
//   * 3D Viewport — analytical-sky skybox + 5x5 PBR sphere sweep +
//     procedural primitive entities (cube / sphere / cone / cylinder /
//     torus from cd::asset::Primitives) + ECS-driven orbit camera.
//   * Scene tree — entity list, click to select.
//   * Inspector — DragFloat3 live-edit transform; EditHistory
//     captures drag-release as a single command.
//   * Audio meter — Mixer → Compressor → SimpleReverb → LowPass →
//     Limiter chain running continuously on a synthetic source;
//     panel shows comp gain reduction (dB), limiter activity, peak.
//   * Net sim ticker — SnapshotBuffer + DeltaWriter + LatencyStats +
//     Throttle simulating client/server traffic at 60 Hz, ASCII
//     table per second of stats.
//   * Random viz — PCG32 + Box-Muller histograms refreshed every
//     ~2 s.
//   * Counters — CounterTable snapshot (frames, draws, commands).
//   * History log — EditHistory + palette + sample-side events.
//   * Command Palette popup (Ctrl+Shift+P) — 15+ registered
//     commands including all four "Select primitive" entries,
//     transform resets, history clear, audio mute, net sim toggle,
//     random reseed, save/load.
//
// Stays at marathon discipline (sample pattern, single main.cpp,
// no engine apps-layer mimicry; that lands at v1.0+ time).
// =============================================================================
#include <cd/asset/Primitives.hpp>
#include <cd/asset_json/Json.hpp>
#include <cd/audio/Compressor.hpp>
#include <cd/audio/IAudioBackend.hpp>
#include <cd/audio/Limiter.hpp>
#include <cd/audio/LowPass.hpp>
#include <cd/audio/Mixer.hpp>
#include <cd/audio/SimpleReverb.hpp>
#include <cd/audio/WasapiBackend.hpp>
#include <cd/camera/Camera.hpp>
#include <cd/camera/Frustum.hpp>
#include <cd/core/CounterTable.hpp>
#include <cd/ecs/Entity.hpp>
#include <cd/ecs/World.hpp>
#include <cd/editor/CommandPalette.hpp>
#include <cd/editor/EditHistory.hpp>
#include <cd/editor/TransformCommands.hpp>
#include <cd/imgui/Context.hpp>
#include <cd/material/AnalyticalSkyMaterial.hpp>
#include <cd/material/Material.hpp>
#include <cd/material/StandardPbrMaterial.hpp>
#include <cd/math/Matrix.hpp>
#include <cd/math/Quaternion.hpp>
#include <cd/math/Random.hpp>
#include <cd/math/Transform.hpp>
#include <cd/math/Vector.hpp>
#include <cd/net/DeltaWriter.hpp>
#include <cd/net/LatencyStats.hpp>
#include <cd/net/SnapshotBuffer.hpp>
#include <cd/net/Throttle.hpp>
#include <cd/platform/Window.hpp>
#include <cd/render/Renderer.hpp>
#include <cd/rhi/Barriers.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi_vulkan/VulkanDevice.hpp>
#include <cd/scene/Scene.hpp>
#include <cd/scene/SceneCameraController.hpp>
#include <cd/scene/Serializer.hpp>
#include <cd/shader/Compiler.hpp>

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fstream>
#include <ios>
#include <span>
#include <string>
#include <vector>

namespace
{

// ============================================================================
// Synthetic audio source (drives the DSP chain continuously).
// ============================================================================

constexpr std::uint32_t kAudioSampleRate = 48000;
constexpr std::size_t   kAudioBufferLen  = 512;   // samples per tick

[[nodiscard]] float square_wave(std::uint64_t i, float hz) noexcept
{
    const float phase = static_cast<float>(i) * hz / static_cast<float>(kAudioSampleRate);
    const float frac  = phase - std::floor(phase);
    return (frac < 0.5F) ? 0.55F : -0.55F;
}

[[nodiscard]] float burst_noise(std::uint64_t i) noexcept
{
    const auto cycle     = static_cast<std::uint64_t>(kAudioSampleRate / 4);   // 250 ms
    const auto burst_len = static_cast<std::uint64_t>(kAudioSampleRate / 30);  // 33 ms
    if ((i % cycle) >= burst_len) return 0.0F;
    std::uint64_t x = i * 2654435761ULL + 0xC0FFEEULL;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    const float n = (static_cast<float>(x & 0xFFFFFFFFu) /
                     static_cast<float>(0xFFFFFFFFu)) * 2.0F - 1.0F;
    return n * 0.7F;
}

// ============================================================================
// Per-entity 3D draw data.
// ============================================================================

enum class PrimitiveKind : std::uint8_t
{
    kCube,
    kSphere,
    kCone,
    kCylinder,
    kTorus,
};

struct SceneEntity
{
    cd::ecs::Entity   handle {};
    std::string       name;
    cd::math::Vec3f   tint   { 1.0F, 1.0F, 1.0F };
    float             metallic  { 0.0F };
    float             roughness { 0.5F };
    PrimitiveKind     kind { PrimitiveKind::kCube };
};

// Reserved for save/load round-trip — currently unused but documents
// the convention.
[[maybe_unused]] [[nodiscard]] PrimitiveKind kind_from_name(std::string_view n) noexcept
{
    if (n == "Sphere")   return PrimitiveKind::kSphere;
    if (n == "Cone")     return PrimitiveKind::kCone;
    if (n == "Cylinder") return PrimitiveKind::kCylinder;
    if (n == "Torus")    return PrimitiveKind::kTorus;
    return PrimitiveKind::kCube;
}

// ============================================================================
// GPU mesh holder.
// ============================================================================

struct GpuMesh
{
    cd::rhi::BufferHandle vb;
    cd::rhi::BufferHandle ib;
    std::uint32_t         index_count { 0 };
};

[[nodiscard]] GpuMesh upload_mesh(cd::rhi::IDevice& dev, const cd::asset::PrimitiveMesh& m)
{
    GpuMesh out {};
    cd::rhi::BufferDesc vbd {};
    vbd.size = m.vertices.size() * sizeof(cd::asset::PrimitiveVertex);
    vbd.usage = cd::rhi::BufferUsage::kVertex;
    vbd.memory = cd::rhi::MemoryUsage::kCpuToGpu;
    auto vb_r = dev.create_buffer(vbd);
    if (!vb_r.has_value()) return out;
    (void)dev.upload_buffer(*vb_r, 0,
        std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(m.vertices.data()), vbd.size));

    cd::rhi::BufferDesc ibd {};
    ibd.size = m.indices.size() * sizeof(std::uint16_t);
    ibd.usage = cd::rhi::BufferUsage::kIndex;
    ibd.memory = cd::rhi::MemoryUsage::kCpuToGpu;
    auto ib_r = dev.create_buffer(ibd);
    if (!ib_r.has_value())
    {
        dev.destroy_buffer(*vb_r);
        return out;
    }
    (void)dev.upload_buffer(*ib_r, 0,
        std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(m.indices.data()), ibd.size));

    out.vb = *vb_r;
    out.ib = *ib_r;
    out.index_count = static_cast<std::uint32_t>(m.indices.size());
    return out;
}

void destroy_mesh(cd::rhi::IDevice& dev, GpuMesh& m)
{
    if (m.vb.is_valid()) dev.destroy_buffer(m.vb);
    if (m.ib.is_valid()) dev.destroy_buffer(m.ib);
    m = {};
}

// ============================================================================
// Convert PrimitiveVertex (44 B pos+normal+uv+color) to a 2-attribute
// pos+normal layout that the StandardPbrMaterial vertex shader expects.
// We do the copy CPU-side and upload as a separate stream because the
// PBR shader signature is fixed (pos@loc0, normal@loc1 — no color/uv).
// ============================================================================
struct PbrVertex { float pos[3]; float normal[3]; };

[[nodiscard]] std::vector<PbrVertex>
to_pbr_vertices(const cd::asset::PrimitiveMesh& m)
{
    std::vector<PbrVertex> out;
    out.reserve(m.vertices.size());
    for (const auto& v : m.vertices)
    {
        PbrVertex pv;
        pv.pos[0]    = v.pos[0];    pv.pos[1]    = v.pos[1];    pv.pos[2]    = v.pos[2];
        pv.normal[0] = v.normal[0]; pv.normal[1] = v.normal[1]; pv.normal[2] = v.normal[2];
        out.push_back(pv);
    }
    return out;
}

[[nodiscard]] GpuMesh upload_pbr_mesh(cd::rhi::IDevice& dev, const cd::asset::PrimitiveMesh& m)
{
    GpuMesh out {};
    const auto verts = to_pbr_vertices(m);
    cd::rhi::BufferDesc vbd {};
    vbd.size = verts.size() * sizeof(PbrVertex);
    vbd.usage = cd::rhi::BufferUsage::kVertex;
    vbd.memory = cd::rhi::MemoryUsage::kCpuToGpu;
    auto vb_r = dev.create_buffer(vbd);
    if (!vb_r.has_value()) return out;
    (void)dev.upload_buffer(*vb_r, 0,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(verts.data()), vbd.size));

    cd::rhi::BufferDesc ibd {};
    ibd.size = m.indices.size() * sizeof(std::uint16_t);
    ibd.usage = cd::rhi::BufferUsage::kIndex;
    ibd.memory = cd::rhi::MemoryUsage::kCpuToGpu;
    auto ib_r = dev.create_buffer(ibd);
    if (!ib_r.has_value())
    {
        dev.destroy_buffer(*vb_r);
        return out;
    }
    (void)dev.upload_buffer(*ib_r, 0,
        std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(m.indices.data()), ibd.size));

    out.vb = *vb_r;
    out.ib = *ib_r;
    out.index_count = static_cast<std::uint32_t>(m.indices.size());
    return out;
}

// ============================================================================
// Wireframe-like simple shader that draws PrimitiveVertex meshes (color
// from vertex.color). Used by the ECS entity panel — gives each
// primitive a recognisable shape via per-vertex normal-pastel colour.
// ============================================================================
constexpr const char* kPrimVS = R"glsl(
#version 450
layout(push_constant) uniform PC {
  mat4 mvp;
  vec4 tint;
} pc;
layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;
layout(location = 3) in vec3 in_color;
layout(location = 0) out vec3 v_color;
void main() {
  vec3 mixed = in_color * pc.tint.rgb;
  float ndl = max(dot(normalize(in_normal), normalize(vec3(0.4, 0.7, 0.5))), 0.15);
  v_color = mixed * (0.35 + 0.65 * ndl);
  vec4 clip = pc.mvp * vec4(in_pos, 1.0);
  clip.y = -clip.y;
  gl_Position = clip;
}
)glsl";

constexpr const char* kPrimFS = R"glsl(
#version 450
layout(location = 0) in  vec3 v_color;
layout(location = 0) out vec4 out_color;
void main() { out_color = vec4(v_color, 1.0); }
)glsl";

struct PrimPush
{
    cd::math::Mat4f mvp;
    float           tint[4];
};

static_assert(sizeof(PrimPush) == 80, "PrimPush layout drift");

// ============================================================================
// Depth target helper.
// ============================================================================
struct DepthTarget
{
    cd::rhi::TextureHandle     image {};
    cd::rhi::TextureViewHandle view  {};
    cd::rhi::Extent2D          extent {};
    void destroy(cd::rhi::IDevice& dev)
    {
        if (view.is_valid())  dev.destroy_texture_view(view);
        if (image.is_valid()) dev.destroy_texture(image);
        *this = {};
    }
};

[[nodiscard]] bool create_depth_target(cd::rhi::IDevice& dev,
                                       cd::rhi::Extent2D size,
                                       cd::rhi::Format   format,
                                       DepthTarget&      out)
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
    if (!img.has_value()) return false;
    cd::rhi::TextureViewDesc vd {};
    vd.texture = *img;
    vd.type = cd::rhi::TextureType::k2D;
    vd.format = format;
    vd.base_mip = 0; vd.mip_count = 1;
    vd.base_layer = 0; vd.layer_count = 1;
    auto v = dev.create_texture_view(vd);
    if (!v.has_value())
    {
        dev.destroy_texture(*img);
        return false;
    }
    out.image = *img;
    out.view = *v;
    out.extent = size;
    return true;
}

// ============================================================================
// Mini histogram helper for the random viz panel.
// ============================================================================
struct Histogram
{
    std::vector<std::size_t> bins;
    float lo { 0 };
    float hi { 1 };
    void rebuild(std::span<const float> samples, float lo_, float hi_, int n_bins)
    {
        lo = lo_; hi = hi_;
        bins.assign(static_cast<std::size_t>(n_bins), 0);
        const float inv = static_cast<float>(n_bins) / (hi - lo);
        for (float s : samples)
        {
            if (s < lo || s >= hi) continue;
            int idx = static_cast<int>((s - lo) * inv);
            if (idx >= 0 && idx < n_bins) ++bins[static_cast<std::size_t>(idx)];
        }
    }
};

}  // namespace

// ============================================================================
// Main.
// ============================================================================
int main()
{
    // ---- Window + Vulkan device + Renderer + ImGui ----
    cd::platform::WindowDesc wd {};
    wd.title = "CHROMODYNAMIC — hello_engine (mega-showcase)";
    wd.width = 1600;
    wd.height = 900;
    auto window_r = cd::platform::create_window(wd);
    if (!window_r.has_value()) return 1;
    auto& window = **window_r;

    cd::rhi_vulkan::VulkanCreateInfo vci {};
    auto dev_r = cd::rhi_vulkan::create_vulkan_device(vci);
    if (!dev_r.has_value()) return 2;
    auto& device = **dev_r;

    cd::render::RendererDesc rd {};
    rd.device = &device;
    rd.swapchain.window_handle = window.native_window_handle();
    rd.swapchain.display_handle = window.native_display_handle();
    rd.swapchain.extent = { window.width(), window.height() };
    rd.swapchain.format = cd::rhi::Format::kBGRA8Unorm;
    rd.frames_in_flight = 2;
    auto renderer_r = cd::render::Renderer::create(rd);
    if (!renderer_r.has_value()) return 3;
    auto& renderer = *renderer_r;

    cd::imgui::InitDesc id {};
    id.window = &window;
    id.device = &device;
    id.color_format = cd::rhi::Format::kBGRA8Unorm;
    id.frames_in_flight = 2;
    auto ctx_r = cd::imgui::Context::create(id);
    if (!ctx_r.has_value()) return 4;
    auto& ctx = **ctx_r;

    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    bool dock_initialised = false;

    // ---- Shader compiler + materials ----
    auto compiler = cd::shader::make_glslang_compiler();
    if (compiler == nullptr) return 5;

    constexpr auto kDepthFormat = cd::rhi::Format::kD32Float;
    DepthTarget depth {};
    if (!create_depth_target(device, { window.width(), window.height() }, kDepthFormat, depth))
        return 6;
    bool depth_initialised_on_gpu = false;

    // Sky material — no vertex buffer, depth off.
    cd::material::MaterialDesc sky_md {};
    sky_md.vertex_glsl   = cd::material::kAnalyticalSkyVS;
    sky_md.fragment_glsl = cd::material::kAnalyticalSkyFS;
    constexpr std::array<cd::rhi::Format, 1> kColorFmts {
        cd::rhi::Format::kBGRA8Unorm };
    sky_md.color_attachment_formats = kColorFmts;
    constexpr std::array<cd::rhi::PushConstantRange, 1> kSkyPush {
        cd::rhi::PushConstantRange { .stages = cd::rhi::ShaderStage::kVertex |
                                               cd::rhi::ShaderStage::kFragment,
                                     .offset = 0,
                                     .size = static_cast<std::uint32_t>(
                                         sizeof(cd::material::AnalyticalSkyPush)) } };
    sky_md.push_constants = kSkyPush;
    sky_md.raster.cull = cd::rhi::CullMode::kNone;
    sky_md.depth_stencil.depth_test = false;
    sky_md.depth_stencil.depth_write = false;
    sky_md.name = "hello_engine/sky";
    auto sky_r = cd::material::Material::create(device, compiler.get(), sky_md);
    if (!sky_r.has_value()) return 7;
    auto& sky_material = *sky_r;

    // Standard PBR material for the 5x5 sphere sweep.
    constexpr std::array<cd::rhi::VertexBinding, 1> kPbrBindings {
        cd::rhi::VertexBinding { 0, sizeof(PbrVertex), false } };
    constexpr std::array<cd::rhi::VertexAttribute, 2> kPbrAttrs {
        cd::rhi::VertexAttribute { 0, 0, cd::rhi::Format::kRGB32Float, offsetof(PbrVertex, pos) },
        cd::rhi::VertexAttribute { 1, 0, cd::rhi::Format::kRGB32Float, offsetof(PbrVertex, normal) } };
    constexpr std::array<cd::rhi::PushConstantRange, 1> kPbrPush {
        cd::rhi::PushConstantRange { .stages = cd::rhi::ShaderStage::kVertex |
                                               cd::rhi::ShaderStage::kFragment,
                                     .offset = 0,
                                     .size = static_cast<std::uint32_t>(
                                         sizeof(cd::material::StandardPbrPush)) } };
    cd::material::MaterialDesc pbr_md {};
    pbr_md.vertex_glsl   = cd::material::kStandardPbrVS;
    pbr_md.fragment_glsl = cd::material::kStandardPbrFS;
    pbr_md.color_attachment_formats = kColorFmts;
    pbr_md.depth_attachment_format = kDepthFormat;
    pbr_md.vertex_bindings = kPbrBindings;
    pbr_md.vertex_attributes = kPbrAttrs;
    pbr_md.push_constants = kPbrPush;
    pbr_md.raster.cull = cd::rhi::CullMode::kNone;
    pbr_md.depth_stencil.depth_test = true;
    pbr_md.depth_stencil.depth_write = true;
    pbr_md.depth_stencil.depth_compare = cd::rhi::CompareOp::kLess;
    pbr_md.name = "hello_engine/pbr";
    auto pbr_r = cd::material::Material::create(device, compiler.get(), pbr_md);
    if (!pbr_r.has_value()) return 8;
    auto& pbr_material = *pbr_r;

    // Primitive shader (PrimitiveVertex layout, simple Lambert + tint).
    constexpr std::array<cd::rhi::VertexBinding, 1> kPrimBindings {
        cd::rhi::VertexBinding { 0, sizeof(cd::asset::PrimitiveVertex), false } };
    constexpr std::array<cd::rhi::VertexAttribute, 4> kPrimAttrs {
        cd::rhi::VertexAttribute { 0, 0, cd::rhi::Format::kRGB32Float,
                                   offsetof(cd::asset::PrimitiveVertex, pos) },
        cd::rhi::VertexAttribute { 1, 0, cd::rhi::Format::kRGB32Float,
                                   offsetof(cd::asset::PrimitiveVertex, normal) },
        cd::rhi::VertexAttribute { 2, 0, cd::rhi::Format::kRG32Float,
                                   offsetof(cd::asset::PrimitiveVertex, uv) },
        cd::rhi::VertexAttribute { 3, 0, cd::rhi::Format::kRGB32Float,
                                   offsetof(cd::asset::PrimitiveVertex, color) } };
    constexpr std::array<cd::rhi::PushConstantRange, 1> kPrimPushRange {
        cd::rhi::PushConstantRange { .stages = cd::rhi::ShaderStage::kVertex |
                                               cd::rhi::ShaderStage::kFragment,
                                     .offset = 0,
                                     .size = static_cast<std::uint32_t>(sizeof(PrimPush)) } };
    cd::material::MaterialDesc prim_md {};
    prim_md.vertex_glsl   = kPrimVS;
    prim_md.fragment_glsl = kPrimFS;
    prim_md.color_attachment_formats = kColorFmts;
    prim_md.depth_attachment_format = kDepthFormat;
    prim_md.vertex_bindings = kPrimBindings;
    prim_md.vertex_attributes = kPrimAttrs;
    prim_md.push_constants = kPrimPushRange;
    prim_md.raster.cull = cd::rhi::CullMode::kNone;
    prim_md.depth_stencil.depth_test = true;
    prim_md.depth_stencil.depth_write = true;
    prim_md.depth_stencil.depth_compare = cd::rhi::CompareOp::kLess;
    prim_md.name = "hello_engine/prim";
    auto prim_r = cd::material::Material::create(device, compiler.get(), prim_md);
    if (!prim_r.has_value()) return 9;
    auto& prim_material = *prim_r;

    // ---- Meshes (one PBR sphere, five primitive entities) ----
    const auto cube_cpu     = cd::asset::make_cube();
    const auto sphere_cpu   = cd::asset::make_sphere(18, 28);
    const auto cone_cpu     = cd::asset::make_cone(32);
    const auto cyl_cpu      = cd::asset::make_cylinder(32);
    const auto torus_cpu    = cd::asset::make_torus(0.45F, 0.18F, 16, 24);

    GpuMesh cube_mesh   = upload_mesh(device, cube_cpu);
    GpuMesh sphere_mesh = upload_mesh(device, sphere_cpu);
    GpuMesh cone_mesh   = upload_mesh(device, cone_cpu);
    GpuMesh cyl_mesh    = upload_mesh(device, cyl_cpu);
    GpuMesh torus_mesh  = upload_mesh(device, torus_cpu);
    GpuMesh pbr_sphere  = upload_pbr_mesh(device, sphere_cpu);

    auto mesh_for = [&](PrimitiveKind k) -> const GpuMesh& {
        switch (k)
        {
            case PrimitiveKind::kSphere:   return sphere_mesh;
            case PrimitiveKind::kCone:     return cone_mesh;
            case PrimitiveKind::kCylinder: return cyl_mesh;
            case PrimitiveKind::kTorus:    return torus_mesh;
            default:                       return cube_mesh;
        }
    };

    // ---- World / Scene / EditHistory ----
    cd::ecs::World      world;
    cd::scene::Scene    scene { world };
    cd::editor::EditHistory history;
    std::deque<std::string> log;
    auto log_push = [&](std::string s) {
        log.emplace_back(std::move(s));
        while (log.size() > 64) log.pop_front();
    };

    std::vector<SceneEntity> entities;
    {
        struct Seed { const char* name; cd::math::Vec3f pos; cd::math::Vec3f tint; PrimitiveKind k; };
        const std::array<Seed, 5> seeds {{
            { "Cube",     { -2.4F, 0.0F,  0.0F }, { 1.00F, 0.55F, 0.45F }, PrimitiveKind::kCube },
            { "Sphere",   { -1.2F, 0.0F,  0.0F }, { 0.45F, 1.00F, 0.55F }, PrimitiveKind::kSphere },
            { "Cone",     {  0.0F, 0.0F,  0.0F }, { 0.50F, 0.55F, 1.00F }, PrimitiveKind::kCone },
            { "Cylinder", {  1.2F, 0.0F,  0.0F }, { 0.95F, 0.80F, 0.45F }, PrimitiveKind::kCylinder },
            { "Torus",    {  2.4F, 0.0F,  0.0F }, { 0.85F, 0.40F, 0.95F }, PrimitiveKind::kTorus },
        }};
        for (const auto& s : seeds)
        {
            SceneEntity e;
            e.handle = scene.create_node();
            e.name   = s.name;
            e.tint   = s.tint;
            e.kind   = s.k;
            scene.local(e.handle)->value.position = s.pos;
            entities.push_back(std::move(e));
        }
    }
    log_push("[boot] 5 ECS entities spawned via cd::asset::Primitives.");
    int selected = 0;

    // ---- Camera + SceneCameraController (orbit) ----
    cd::camera::Camera cam {};
    cam.eye = { 0.0F, 2.5F, 8.0F };
    cam.target = { 0.0F, 0.5F, 0.0F };
    cam.fov_y = 0.9F;
    cam.near_z = 0.05F;
    cam.far_z = 200.0F;
    cd::scene::SceneCameraController scene_cam;
    scene_cam.attach(cam, scene, /*follow=*/{});
    scene_cam.set_auto_spin(true);
    scene_cam.orbit().auto_spin_rate = 0.25F;

    // ---- Audio chain (continuous tick) ----
    cd::audio::Mixer<2> audio_bus;
    audio_bus.set_gain(0, 0.6F);
    audio_bus.set_gain(1, 0.7F);
    cd::audio::Compressor comp;
    comp.prepare(static_cast<float>(kAudioSampleRate),
                 /*threshold=*/0.40F, /*ratio=*/6.0F,
                 /*attack=*/0.004F, /*release=*/0.080F);
    cd::audio::SimpleReverb reverb;
    reverb.prepare(kAudioSampleRate / 8);
    reverb.set_feedback(0.35F);
    cd::audio::LowPass lowpass;
    lowpass.prepare(static_cast<float>(kAudioSampleRate), /*cutoff=*/6500.0F);
    cd::audio::Limiter limiter;
    limiter.prepare(static_cast<float>(kAudioSampleRate),
                    /*thresh=*/0.92F, /*attack=*/0.0002F, /*release=*/0.040F);
    std::uint64_t audio_t = 0;
    bool          audio_muted = false;
    float         audio_peak_window      = 0.0F;
    float         audio_comp_db_window   = 0.0F;
    float         audio_limiter_gain_min = 1.0F;
    std::deque<float> audio_meter_history;  // last ~120 ticks of peak

    // Phase 139 — last-5-seconds ring buffer of DSP chain output (s16
    // PCM). User clicks "Save WAV" in the Audio panel and the buffer
    // gets dumped to disk; play with any system audio player.
    constexpr std::size_t kAudioRingFrames = kAudioSampleRate * 5u;  // 5 s mono
    std::vector<std::int16_t> audio_ring(kAudioRingFrames, 0);
    std::size_t   audio_ring_write = 0;
    std::uint64_t audio_total_written = 0;

    // Phase 139 v2 — WASAPI live playback. Pre-render 2 seconds of the
    // DSP chain at startup, create a looping clip, play. The visual
    // panel keeps ticking against the same DSP for an in-sync meter,
    // but the audible output is the pre-rendered loop (WASAPI clip
    // semantics don't expose continuous-stream push from sample code).
    // Without this the user heard nothing because the engine's audio
    // backend was never instantiated by hello_engine.
    auto audio_backend = cd::audio::make_wasapi_audio_backend();
    cd::audio::ClipHandle  live_clip {};
    cd::audio::VoiceHandle live_voice {};
    bool audio_live_ok = (audio_backend != nullptr);
    if (audio_live_ok)
    {
        // Render 2 seconds of audio through the same DSP chain that
        // the on-screen meter walks every frame, then feed it to
        // WASAPI as a looping clip.
        constexpr std::size_t kPreRenderFrames = kAudioSampleRate * 2u;
        std::vector<float> live_buf(kPreRenderFrames, 0.0F);
        // Use a separate set of DSP nodes so the "live ticker" the
        // UI walks isn't pre-cooked by this render pass.
        cd::audio::Mixer<2>      m2;        m2.set_gain(0, 0.6F); m2.set_gain(1, 0.7F);
        cd::audio::Compressor    c2;        c2.prepare(static_cast<float>(kAudioSampleRate), 0.40F, 6.0F, 0.004F, 0.080F);
        cd::audio::SimpleReverb  r2;        r2.prepare(kAudioSampleRate / 8); r2.set_feedback(0.35F);
        cd::audio::LowPass       l2;        l2.prepare(static_cast<float>(kAudioSampleRate), 6500.0F);
        cd::audio::Limiter       L2;        L2.prepare(static_cast<float>(kAudioSampleRate), 0.92F, 0.0002F, 0.040F);
        for (std::size_t i = 0; i < kPreRenderFrames; ++i)
        {
            m2.mix(0, square_wave(i, 440.0F));
            m2.mix(1, burst_noise(i));
            float x = m2.pull();
            x = c2.process(x);
            const float wet = r2.process(x);
            x = 0.75F * x + 0.20F * wet;
            x = l2.process(x);
            x = L2.process(x);
            if (x >  1.0F) x =  1.0F;
            if (x < -1.0F) x = -1.0F;
            live_buf[i] = x * 0.7F;  // -3 dB headroom on output
        }
        cd::audio::ClipDesc cd_desc {};
        cd_desc.samples     = std::span<const float>(live_buf);
        cd_desc.channels    = 1;
        cd_desc.sample_rate = kAudioSampleRate;
        auto clip_r = audio_backend->create_clip(cd_desc);
        if (clip_r.has_value())
        {
            live_clip = *clip_r;
            auto voice_r = audio_backend->play(live_clip, /*vol=*/0.65F, /*loop=*/true);
            if (voice_r.has_value()) live_voice = *voice_r;
        }
    }

    // ---- Net sim (continuous tick) ----
    cd::net::Throttle             net_throttle { /*cap=*/4.0F, /*rate=*/30.0F };
    cd::net::SnapshotBuffer<float> net_snapbuf;  // tiny scalar state for the demo
    cd::net::LatencyStats         net_rtt;
    bool                          net_enabled = true;
    std::uint32_t                 net_sent = 0;
    std::uint32_t                 net_recv = 0;
    std::uint32_t                 net_drop = 0;
    std::uint64_t                 net_raw_bytes  = 0;
    std::uint64_t                 net_wire_bytes = 0;
    cd::math::Random              net_rng { 0xC0FFEE42u };
    float                         net_baseline = 0.0F;
    double                        net_t = 0.0;
    double                        next_net_tick = 0.0;

    // ---- Random viz ----
    cd::math::Random rand_rng { 0xA1B2C3D4u };
    Histogram hist_uniform;
    Histogram hist_normal;
    auto rebuild_random_viz = [&]() {
        std::vector<float> u;
        u.reserve(8192);
        for (int i = 0; i < 8192; ++i) u.push_back(rand_rng.next_float());
        hist_uniform.rebuild(u, 0.0F, 1.0F, 24);
        std::vector<float> n;
        n.reserve(8192);
        bool have_cached = false;
        float cached = 0.0F;
        for (int i = 0; i < 8192; ++i)
        {
            if (have_cached) { have_cached = false; n.push_back(cached); continue; }
            float u1 = rand_rng.next_float();
            if (u1 < 1e-7F) u1 = 1e-7F;
            const float u2 = rand_rng.next_float();
            const float r  = std::sqrt(-2.0F * std::log(u1));
            const float t  = 6.28318530717958F * u2;
            cached = r * std::sin(t);
            have_cached = true;
            n.push_back(r * std::cos(t));
        }
        hist_normal.rebuild(n, -3.0F, 3.0F, 24);
    };
    rebuild_random_viz();
    std::uint64_t next_random_refresh = 0;

    // ---- Counter table (engine self-stats) ----
    cd::core::CounterTable counters;

    // ---- Command palette ----
    cd::editor::CommandPalette palette;
    bool        palette_visible = false;
    std::string palette_query;
    palette.register_command(1, "Edit: Undo",
        [&]{ if (history.undo()) log_push("[palette] Undo"); });
    palette.register_command(2, "Edit: Redo",
        [&]{ if (history.redo()) log_push("[palette] Redo"); });
    palette.register_command(3, "Edit: Clear History",
        [&]{ history.clear(); log_push("[palette] History cleared"); });
    palette.register_command(10, "Select: Cube",
        [&]{ for (std::size_t i=0;i<entities.size();++i) if (entities[i].name=="Cube") { selected=int(i); log_push("[palette] Select Cube"); break; } });
    palette.register_command(11, "Select: Sphere",
        [&]{ for (std::size_t i=0;i<entities.size();++i) if (entities[i].name=="Sphere") { selected=int(i); log_push("[palette] Select Sphere"); break; } });
    palette.register_command(12, "Select: Cone",
        [&]{ for (std::size_t i=0;i<entities.size();++i) if (entities[i].name=="Cone") { selected=int(i); log_push("[palette] Select Cone"); break; } });
    palette.register_command(13, "Select: Cylinder",
        [&]{ for (std::size_t i=0;i<entities.size();++i) if (entities[i].name=="Cylinder") { selected=int(i); log_push("[palette] Select Cylinder"); break; } });
    palette.register_command(14, "Select: Torus",
        [&]{ for (std::size_t i=0;i<entities.size();++i) if (entities[i].name=="Torus") { selected=int(i); log_push("[palette] Select Torus"); break; } });
    palette.register_command(20, "Transform: Reset Selected",
        [&]{
            if (selected>=0 && selected<int(entities.size()))
            {
                auto& ent = entities[size_t(selected)];
                if (auto* lt = scene.local(ent.handle); lt)
                {
                    lt->value.position = {};
                    lt->value.scale = { 1.0F, 1.0F, 1.0F };
                    lt->value.rotation = { 0.0F, 0.0F, 0.0F, 1.0F };
                    log_push("[palette] Reset selected transform");
                }
            }
        });
    palette.register_command(30, "Camera: Toggle Auto-Spin",
        [&]{ scene_cam.set_auto_spin(!scene_cam.auto_spin());
             log_push(std::string("[palette] Auto-spin: ") +
                      (scene_cam.auto_spin() ? "ON" : "OFF")); });
    palette.register_command(31, "Camera: Follow Selected",
        [&]{
            if (selected>=0 && selected<int(entities.size()))
            {
                scene_cam.attach(cam, scene, entities[size_t(selected)].handle);
                log_push("[palette] Camera following: " + entities[size_t(selected)].name);
            }
        });
    palette.register_command(40, "Audio: Toggle Mute",
        [&]{
            audio_muted = !audio_muted;
            // Phase 139 v3 — drive WASAPI voice volume so mute is audible.
            if (audio_live_ok && audio_backend && live_voice.is_valid())
                audio_backend->set_volume(live_voice, audio_muted ? 0.0F : 0.65F);
            log_push(std::string("[palette] Audio: ") + (audio_muted?"MUTED":"LIVE"));
        });
    palette.register_command(50, "Net: Toggle Sim",
        [&]{ net_enabled = !net_enabled;
             log_push(std::string("[palette] Net sim: ") + (net_enabled?"RUNNING":"PAUSED")); });
    palette.register_command(60, "Random: Reseed + Refresh",
        [&]{ rand_rng = cd::math::Random { static_cast<std::uint64_t>(std::rand()) };
             rebuild_random_viz(); log_push("[palette] Random reseeded"); });
    palette.register_command(70, "Help: Print Shortcuts",
        [&]{ log_push("Ctrl+Shift+P: command palette");
             log_push("Esc: close palette / quit");
             log_push("Right-mouse drag in viewport: orbit");
             log_push("Mouse wheel: zoom"); });

    // ---- Frame loop ----
    using clock = std::chrono::steady_clock;
    auto last_tick = clock::now();
    std::uint32_t frame_idx = 0;
    bool needs_rebuild = false;
    std::vector<cd::platform::OSEvent> events;
    events.reserve(64);

    // Phase 139 v2 — platform-level modifier tracking. cd::imgui_backend
    // doesn't forward Ctrl/Shift state into ImGui's IO reliably, so we
    // track from the same OSEvent KeyDown/KeyUp pairs that drive the
    // rest of the sample.
    bool mod_ctrl  = false;
    bool mod_shift = false;

    while (true)
    {
        events.clear();
        if (!window.pump_events(events)) break;
        for (const auto& e : events)
        {
            ctx.handle_event(e);
            if (e.kind == cd::platform::OSEventKind::kKeyDown &&
                e.key == cd::platform::KeyCode::kEscape)
            {
                if (palette_visible) { palette_visible = false; palette_query.clear(); }
                else                  window.request_close();
            }
            else if (e.kind == cd::platform::OSEventKind::kResize)
            {
                needs_rebuild = true;
            }
            // F1 alternatif (focus-bağımsız, zero-modifier).
            else if (e.kind == cd::platform::OSEventKind::kKeyDown &&
                     e.key == cd::platform::KeyCode::kF1)
            {
                palette_visible = !palette_visible;
                if (palette_visible) palette_query.clear();
            }
            // Phase 139 v2 — platform modifier tracking + Ctrl+Shift+P.
            else if (e.kind == cd::platform::OSEventKind::kKeyDown)
            {
                if (e.key == cd::platform::KeyCode::kLCtrl  ||
                    e.key == cd::platform::KeyCode::kRCtrl)  mod_ctrl  = true;
                if (e.key == cd::platform::KeyCode::kLShift ||
                    e.key == cd::platform::KeyCode::kRShift) mod_shift = true;
                if (e.key == cd::platform::KeyCode::kP && mod_ctrl && mod_shift)
                {
                    palette_visible = !palette_visible;
                    if (palette_visible) palette_query.clear();
                }
            }
            else if (e.kind == cd::platform::OSEventKind::kKeyUp)
            {
                if (e.key == cd::platform::KeyCode::kLCtrl  ||
                    e.key == cd::platform::KeyCode::kRCtrl)  mod_ctrl  = false;
                if (e.key == cd::platform::KeyCode::kLShift ||
                    e.key == cd::platform::KeyCode::kRShift) mod_shift = false;
            }
        }
        if (needs_rebuild)
        {
            if (window.width() == 0 || window.height() == 0) continue;
            if (!renderer.recreate_swapchain({ window.width(), window.height() }).has_value())
                continue;
            if (!create_depth_target(device, { window.width(), window.height() }, kDepthFormat, depth))
                continue;
            depth_initialised_on_gpu = false;
            needs_rebuild = false;
        }

        // ---- dt ----
        const auto now = clock::now();
        const float dt = std::chrono::duration<float>(now - last_tick).count();
        last_tick = now;

        // ---- Tick audio chain (always-on synthesis) ----
        if (!audio_muted)
        {
            float peak = 0.0F;
            float comp_db_min = 0.0F;
            float lim_gain_min = 1.0F;
            for (std::size_t s = 0; s < kAudioBufferLen; ++s, ++audio_t)
            {
                audio_bus.mix(0, square_wave(audio_t, 440.0F));
                audio_bus.mix(1, burst_noise(audio_t));
                float x = audio_bus.pull();
                x = comp.process(x);
                if (comp.gain_db() < comp_db_min) comp_db_min = comp.gain_db();
                const float wet = reverb.process(x);
                x = 0.75F * x + 0.20F * wet;
                x = lowpass.process(x);
                x = limiter.process(x);
                if (limiter.current_gain() < lim_gain_min) lim_gain_min = limiter.current_gain();
                if (std::fabs(x) > peak) peak = std::fabs(x);
                // Phase 139 — capture to 5 s ring buffer.
                if (x >  1.0F) x =  1.0F;
                if (x < -1.0F) x = -1.0F;
                audio_ring[audio_ring_write] =
                    static_cast<std::int16_t>(x * 32760.0F);
                ++audio_ring_write;
                if (audio_ring_write >= kAudioRingFrames) audio_ring_write = 0;
                ++audio_total_written;
            }
            audio_peak_window      = peak;
            audio_comp_db_window   = comp_db_min;
            audio_limiter_gain_min = lim_gain_min;
            audio_meter_history.push_back(peak);
            while (audio_meter_history.size() > 120) audio_meter_history.pop_front();
            counters.increment("audio_ticks");
        }

        // ---- Tick net sim ----
        if (net_enabled)
        {
            net_t += static_cast<double>(dt);
            net_throttle.update(dt);
            while (net_t >= next_net_tick)
            {
                next_net_tick += 1.0 / 60.0;  // 60 Hz server tick
                if (net_throttle.try_consume(1.0F))
                {
                    const float v = std::sin(static_cast<float>(net_t) * 1.2F);
                    // delta vs last baseline
                    const std::byte cur_bytes[4] = {
                        std::byte((std::uint32_t(v * 1e6F) >>  0) & 0xFFu),
                        std::byte((std::uint32_t(v * 1e6F) >>  8) & 0xFFu),
                        std::byte((std::uint32_t(v * 1e6F) >> 16) & 0xFFu),
                        std::byte((std::uint32_t(v * 1e6F) >> 24) & 0xFFu),
                    };
                    const std::byte base_bytes[4] = {
                        std::byte((std::uint32_t(net_baseline * 1e6F) >>  0) & 0xFFu),
                        std::byte((std::uint32_t(net_baseline * 1e6F) >>  8) & 0xFFu),
                        std::byte((std::uint32_t(net_baseline * 1e6F) >> 16) & 0xFFu),
                        std::byte((std::uint32_t(net_baseline * 1e6F) >> 24) & 0xFFu),
                    };
                    const auto delta = cd::net::write_delta(
                        std::span<const std::byte>(base_bytes),
                        std::span<const std::byte>(cur_bytes));
                    net_raw_bytes  += 4;
                    net_wire_bytes += delta.size();
                    net_baseline = v;
                    ++net_sent;
                    if (net_rng.next_float() < 0.10F) { ++net_drop; }
                    else
                    {
                        const double lat = 0.03 + 0.06 * static_cast<double>(net_rng.next_float());
                        net_rtt.record(static_cast<std::uint32_t>(lat * 2.0 * 1e6));
                        net_snapbuf.push(net_t + lat, v);
                        ++net_recv;
                    }
                }
            }
            (void)net_snapbuf.sample(net_t - 0.10);  // client-side interp
            net_snapbuf.drop_older_than(net_t - 0.5);
            counters.increment("net_ticks");
        }

        // ---- Random viz periodic refresh ----
        if (frame_idx >= next_random_refresh)
        {
            rebuild_random_viz();
            next_random_refresh = frame_idx + 120;  // ~2 s @ 60 fps
        }

        // ---- Update scene camera ----
        scene_cam.update(dt);

        // ---- Begin GPU frame ----
        auto frame_r = renderer.begin_frame();
        if (!frame_r.has_value())
        {
            if (frame_r.error().code == static_cast<std::uint32_t>(
                    cd::render::render_errors::Code::kSwapchainOutOfDate))
            { needs_rebuild = true; continue; }
            return 10;
        }
        auto& frame = *frame_r;
        auto& cmd = *frame.command_buffer;

        if (!depth_initialised_on_gpu)
        {
            std::array<cd::rhi::TextureBarrier, 1> db {
                cd::rhi::TextureBarrier {
                    .texture = depth.image,
                    .from = cd::rhi::ResourceState::kUndefined,
                    .to = cd::rhi::ResourceState::kDepthWrite,
                    .range = { .base_mip = 0, .mip_count = 1, .base_layer = 0, .layer_count = 1 } } };
            cmd.barrier({}, db);
            depth_initialised_on_gpu = true;
        }

        std::array<cd::rhi::ColorAttachmentInfo, 1> color_attach {
            cd::rhi::ColorAttachmentInfo { .view = frame.swapchain_image_view,
                                          .load_op = cd::rhi::LoadOp::kClear,
                                          .store_op = cd::rhi::StoreOp::kStore,
                                          .clear_color = { .f32 = { 1.0F, 0.0F, 1.0F, 1.0F } } } };
        cd::rhi::DepthStencilAttachmentInfo depth_attach {};
        depth_attach.view = depth.view;
        depth_attach.depth_load = cd::rhi::LoadOp::kClear;
        depth_attach.depth_store = cd::rhi::StoreOp::kStore;
        depth_attach.clear.depth = 1.0F;

        cd::rhi::RenderPassBeginInfo rp {};
        rp.render_area = cd::rhi::Rect2D { {0,0}, frame.extent };
        rp.color_attachments = color_attach;
        rp.depth_stencil = &depth_attach;
        cmd.begin_render_pass(rp);
        cmd.set_viewport(cd::rhi::Viewport {
            0.0F, 0.0F,
            static_cast<float>(frame.extent.width),
            static_cast<float>(frame.extent.height),
            0.0F, 1.0F });
        cmd.set_scissor(cd::rhi::Rect2D { {0,0}, frame.extent });

        const float aspect = static_cast<float>(frame.extent.width) /
                             static_cast<float>(frame.extent.height);
        const cd::math::Mat4f vp = cd::camera::view_projection(cam, aspect);

        // ---- Sky pass ----
        cd::math::Vec3f forward {
            cam.target.x - cam.eye.x, cam.target.y - cam.eye.y, cam.target.z - cam.eye.z };
        const float fl = std::sqrt(forward.x*forward.x + forward.y*forward.y + forward.z*forward.z);
        forward.x /= fl; forward.y /= fl; forward.z /= fl;
        constexpr cd::math::Vec3f world_up { 0.0F, 1.0F, 0.0F };
        cd::math::Vec3f sky_right {
            forward.y*world_up.z - forward.z*world_up.y,
            forward.z*world_up.x - forward.x*world_up.z,
            forward.x*world_up.y - forward.y*world_up.x };
        const float rl = std::sqrt(sky_right.x*sky_right.x + sky_right.y*sky_right.y + sky_right.z*sky_right.z);
        sky_right.x /= rl; sky_right.y /= rl; sky_right.z /= rl;
        const cd::math::Vec3f sky_up {
            sky_right.y*forward.z - sky_right.z*forward.y,
            sky_right.z*forward.x - sky_right.x*forward.z,
            sky_right.x*forward.y - sky_right.y*forward.x };
        const float half_h = std::tan(cam.fov_y * 0.5F);
        const float half_w = half_h * aspect;

        cd::material::AnalyticalSkyPush spush {};
        spush.cam_right[0] = sky_right.x; spush.cam_right[1] = sky_right.y; spush.cam_right[2] = sky_right.z; spush.cam_right[3] = half_w;
        spush.cam_up[0]    = sky_up.x;    spush.cam_up[1]    = sky_up.y;    spush.cam_up[2]    = sky_up.z;    spush.cam_up[3]    = half_h;
        spush.cam_fwd[0]   = forward.x;   spush.cam_fwd[1]   = forward.y;   spush.cam_fwd[2]   = forward.z;   spush.cam_fwd[3]   = 0.0F;
        spush.sun_dir[0]   = -0.4F;       spush.sun_dir[1]   = -0.6F;       spush.sun_dir[2]   = -0.7F;       spush.sun_dir[3]   = 0.9F;
        sky_material.apply(cmd);
        cmd.push_constants(sky_material.pipeline_layout(),
                           cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
                           0, sizeof(spush), &spush);
        cmd.draw(3, 1, 0, 0);

        // ---- 5x5 PBR sphere sweep (back row of the viewport) ----
        pbr_material.apply(cmd);
        cmd.bind_vertex_buffer(0, pbr_sphere.vb, 0);
        cmd.bind_index_buffer(pbr_sphere.ib, 0, cd::rhi::IndexType::kUInt16);
        constexpr int kGrid = 5;
        constexpr float kSpacing = 1.2F;
        std::uint32_t culled = 0;
        std::uint32_t intersecting = 0;
        std::uint32_t fully_inside = 0;
        // Phase 153: extract frustum from the current VP each frame and
        // use sphere-vs-frustum to drive cull stats. Bounding-sphere
        // radius is the diagonal of the unit-sphere mesh AABB scaled by
        // its world position; the mesh in pbr_sphere has unit radius so
        // we use 0.5F as the cull radius (visual radius is slightly
        // smaller than the bounding sphere).
        const auto frustum = cd::camera::extract_frustum(vp);
        constexpr float kSphereRadius = 0.5F;
        for (int row = 0; row < kGrid; ++row)
        {
            for (int col = 0; col < kGrid; ++col)
            {
                const float metallic = static_cast<float>(col) / static_cast<float>(kGrid - 1);
                const float roughness = 0.05F + (1.0F - 0.05F) *
                    (static_cast<float>(row) / static_cast<float>(kGrid - 1));
                const float x = (static_cast<float>(col) - 2.0F) * kSpacing;
                const float y = 2.2F + (static_cast<float>(row) - 2.0F) * 0.9F;
                const float z = -4.5F;
                const cd::math::Vec3f center { x, y, z };
                const auto cull = cd::camera::test_sphere(frustum, center, kSphereRadius);
                if (cull == cd::camera::CullResult::kOutside) { ++culled; continue; }
                if (cull == cd::camera::CullResult::kIntersecting) ++intersecting;
                else ++fully_inside;
                cd::math::Mat4f model = cd::math::Mat4f::identity();
                model[3][0] = x; model[3][1] = y; model[3][2] = z;
                const auto mvp = vp * model;
                cd::material::StandardPbrPush pb {};
                std::memcpy(pb.mvp, &mvp, sizeof(pb.mvp));
                pb.albedo[0] = 0.95F; pb.albedo[1] = 0.64F; pb.albedo[2] = 0.32F; pb.albedo[3] = 1.0F;
                pb.mr_amb[0] = metallic; pb.mr_amb[1] = roughness; pb.mr_amb[2] = 0.0F; pb.mr_amb[3] = 0.0F;
                pb.camera_pos[0] = cam.eye.x; pb.camera_pos[1] = cam.eye.y; pb.camera_pos[2] = cam.eye.z; pb.camera_pos[3] = 0.0F;
                pb.light_dir[0]  = -0.4F; pb.light_dir[1]  = -0.6F; pb.light_dir[2]  = -0.7F; pb.light_dir[3]  = 0.9F;
                cmd.push_constants(pbr_material.pipeline_layout(),
                                   cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
                                   0, sizeof(pb), &pb);
                cmd.draw_indexed(pbr_sphere.index_count, 1, 0, 0, 0);
                counters.increment("draws_pbr");
            }
        }
        counters.set("culled_pbr", culled);
        counters.set("intersecting_pbr", intersecting);
        counters.set("inside_pbr", fully_inside);

        // ---- ECS entity primitives row (front of the viewport) ----
        prim_material.apply(cmd);
        for (const auto& ent : entities)
        {
            const auto& mesh = mesh_for(ent.kind);
            if (!mesh.vb.is_valid()) continue;
            cmd.bind_vertex_buffer(0, mesh.vb, 0);
            cmd.bind_index_buffer(mesh.ib, 0, cd::rhi::IndexType::kUInt16);
            auto* lt = scene.local(ent.handle);
            if (lt == nullptr) continue;
            const auto model = cd::math::to_mat4(lt->value);
            const auto mvp = vp * model;
            PrimPush pp {};
            pp.mvp = mvp;
            pp.tint[0] = ent.tint.x; pp.tint[1] = ent.tint.y; pp.tint[2] = ent.tint.z; pp.tint[3] = 1.0F;
            cmd.push_constants(prim_material.pipeline_layout(),
                               cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
                               0, sizeof(pp), &pp);
            cmd.draw_indexed(mesh.index_count, 1, 0, 0, 0);
            counters.increment("draws_prim");
        }

        // ---- ImGui frame ----
        ctx.new_frame();

        // Phase 139 — palette hotkey through ImGui (after new_frame so
        // IO modifier state is current). This is the path that works
        // regardless of focus / text-input absorption.
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_P))
        {
            palette_visible = !palette_visible;
            if (palette_visible) palette_query.clear();
        }

        // DockSpace host.
        {
            const ImGuiViewport* main_vp = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(main_vp->WorkPos);
            ImGui::SetNextWindowSize(main_vp->WorkSize);
            ImGui::SetNextWindowViewport(main_vp->ID);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0F);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0,0});
            ImGui::Begin("##cd_dockhost", nullptr,
                ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
                ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize  |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground);
            ImGui::PopStyleVar(3);
            const ImGuiID dock_id = ImGui::GetID("cd_engine_dock");
            if (!dock_initialised && ImGui::DockBuilderGetNode(dock_id) == nullptr)
            {
                ImGui::DockBuilderRemoveNode(dock_id);
                const int flags =
                    static_cast<int>(ImGuiDockNodeFlags_DockSpace) |
                    static_cast<int>(ImGuiDockNodeFlags_PassthruCentralNode);
                ImGui::DockBuilderAddNode(dock_id, static_cast<ImGuiDockNodeFlags>(flags));
                ImGui::DockBuilderSetNodeSize(dock_id, main_vp->WorkSize);
                ImGuiID m = dock_id;
                ImGuiID dock_left   = ImGui::DockBuilderSplitNode(m, ImGuiDir_Left,  0.16F, nullptr, &m);
                ImGuiID dock_right  = ImGui::DockBuilderSplitNode(m, ImGuiDir_Right, 0.25F, nullptr, &m);
                ImGuiID dock_bot    = ImGui::DockBuilderSplitNode(m, ImGuiDir_Down,  0.30F, nullptr, &m);
                ImGuiID dock_botR   = ImGui::DockBuilderSplitNode(dock_bot, ImGuiDir_Right, 0.50F, nullptr, &dock_bot);
                ImGui::DockBuilderDockWindow("Scene",     dock_left);
                ImGui::DockBuilderDockWindow("Inspector", dock_right);
                ImGui::DockBuilderDockWindow("Counters",  dock_right);
                ImGui::DockBuilderDockWindow("Random",    dock_right);
                ImGui::DockBuilderDockWindow("Audio",     dock_bot);
                ImGui::DockBuilderDockWindow("Net Sim",   dock_bot);
                ImGui::DockBuilderDockWindow("History",   dock_botR);
                ImGui::DockBuilderFinish(dock_id);
                dock_initialised = true;
            }
            ImGui::DockSpace(dock_id, ImVec2{0,0}, ImGuiDockNodeFlags_PassthruCentralNode);
            ImGui::End();
        }

        // ---- Scene tree ----
        ImGui::Begin("Scene");
        ImGui::Text("Entities (%zu)", entities.size());
        ImGui::Separator();
        for (std::size_t i = 0; i < entities.size(); ++i)
        {
            const bool sel = (selected == static_cast<int>(i));
            char row[128] {};
            std::snprintf(row, sizeof(row), "%s##e%zu", entities[i].name.c_str(), i);
            if (ImGui::Selectable(row, sel)) selected = static_cast<int>(i);
        }
        ImGui::Separator();
        ImGui::TextDisabled("Ctrl+Shift+P = command palette");
        ImGui::TextDisabled("Esc closes palette / quits");
        ImGui::End();

        // ---- Inspector ----
        ImGui::Begin("Inspector");
        if (selected >= 0 && selected < static_cast<int>(entities.size()))
        {
            auto& ent = entities[static_cast<std::size_t>(selected)];
            auto* lt = scene.local(ent.handle);
            if (lt != nullptr)
            {
                ImGui::Text("Entity: %s", ent.name.c_str());
                ImGui::TextColored(ImVec4(ent.tint.x, ent.tint.y, ent.tint.z, 1.0F),
                                   "tint preview");
                ImGui::Separator();

                ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.62F);

                // Position
                ImGui::SeparatorText("Position");
                {
                    static cd::math::Vec3f pre {};
                    float xyz[3] { lt->value.position.x, lt->value.position.y, lt->value.position.z };
                    bool changed = ImGui::DragFloat3("##pos", xyz, 0.05F, -10.0F, 10.0F, "%.3f");
                    if (ImGui::IsItemActivated()) pre = lt->value.position;
                    if (changed) lt->value.position = { xyz[0], xyz[1], xyz[2] };
                    if (ImGui::IsItemDeactivatedAfterEdit())
                    {
                        cd::math::Vec3f delta {
                            lt->value.position.x - pre.x,
                            lt->value.position.y - pre.y,
                            lt->value.position.z - pre.z };
                        if (delta.x != 0 || delta.y != 0 || delta.z != 0)
                        {
                            lt->value.position = pre;
                            history.push(std::make_unique<cd::editor::TranslateCommand>(
                                scene, ent.handle, delta));
                            log_push("drag: Translate " + ent.name);
                        }
                    }
                }
                // Scale
                ImGui::SeparatorText("Scale");
                {
                    static cd::math::Vec3f pre { 1,1,1 };
                    float xyz[3] { lt->value.scale.x, lt->value.scale.y, lt->value.scale.z };
                    bool changed = ImGui::DragFloat3("##sca", xyz, 0.02F, 0.05F, 5.0F, "%.3f");
                    if (ImGui::IsItemActivated()) pre = lt->value.scale;
                    if (changed) lt->value.scale = { xyz[0], xyz[1], xyz[2] };
                    if (ImGui::IsItemDeactivatedAfterEdit())
                    {
                        cd::math::Vec3f factor {
                            pre.x != 0 ? lt->value.scale.x / pre.x : 1,
                            pre.y != 0 ? lt->value.scale.y / pre.y : 1,
                            pre.z != 0 ? lt->value.scale.z / pre.z : 1 };
                        if (factor.x != 1 || factor.y != 1 || factor.z != 1)
                        {
                            lt->value.scale = pre;
                            history.push(std::make_unique<cd::editor::ScaleCommand>(
                                scene, ent.handle, factor));
                            log_push("drag: Scale " + ent.name);
                        }
                    }
                }
                ImGui::PopItemWidth();
            }
        }
        else
        {
            ImGui::TextDisabled("no selection");
        }
        ImGui::End();

        // ---- Counters ----
        ImGui::Begin("Counters");
        const auto snap = counters.snapshot();
        // Phase 139 — FPS / dt readout up top.
        const double fps = (dt > 0.0F) ? (1.0 / static_cast<double>(dt)) : 0.0;
        ImGui::Text("FPS: %5.1f   dt: %.2f ms   frame: %u",
                    fps, static_cast<double>(dt) * 1000.0, frame_idx);
        ImGui::Separator();
        for (const auto& [name, value] : snap)
        {
            ImGui::Text("%-20s %lld", name.c_str(), static_cast<long long>(value));
        }
        ImGui::End();

        // ---- Random viz ----
        ImGui::Begin("Random");
        ImGui::TextDisabled("PCG32 + Box-Muller (auto-refresh ~2s)");
        ImGui::SeparatorText("Uniform [0,1)");
        if (!hist_uniform.bins.empty())
        {
            std::vector<float> bars(hist_uniform.bins.size());
            std::size_t peak = 1;
            for (auto b : hist_uniform.bins) if (b > peak) peak = b;
            for (std::size_t i = 0; i < hist_uniform.bins.size(); ++i)
                bars[i] = static_cast<float>(hist_uniform.bins[i]) / static_cast<float>(peak);
            ImGui::PlotHistogram("##uniform", bars.data(),
                                 static_cast<int>(bars.size()), 0, nullptr,
                                 0.0F, 1.0F, ImVec2(0, 60));
        }
        ImGui::SeparatorText("N(0,1) Box-Muller");
        if (!hist_normal.bins.empty())
        {
            std::vector<float> bars(hist_normal.bins.size());
            std::size_t peak = 1;
            for (auto b : hist_normal.bins) if (b > peak) peak = b;
            for (std::size_t i = 0; i < hist_normal.bins.size(); ++i)
                bars[i] = static_cast<float>(hist_normal.bins[i]) / static_cast<float>(peak);
            ImGui::PlotHistogram("##normal", bars.data(),
                                 static_cast<int>(bars.size()), 0, nullptr,
                                 0.0F, 1.0F, ImVec2(0, 60));
        }
        ImGui::End();

        // ---- Audio ----
        ImGui::Begin("Audio");
        if (audio_muted)
            ImGui::TextColored(ImVec4(1, 0.5F, 0.3F, 1), "MUTED");
        else
            ImGui::TextColored(ImVec4(0.4F, 1, 0.4F, 1), "LIVE");
        ImGui::Text("Mixer -> Comp -> Reverb -> LowPass -> Limiter");
        ImGui::Separator();
        ImGui::Text("peak (last buf)      %.3f", static_cast<double>(audio_peak_window));
        ImGui::Text("comp gain reduction  %.2f dB", static_cast<double>(audio_comp_db_window));
        ImGui::Text("limiter min gain     %.4f", static_cast<double>(audio_limiter_gain_min));
        if (!audio_meter_history.empty())
        {
            std::vector<float> vv(audio_meter_history.begin(), audio_meter_history.end());
            ImGui::PlotLines("##peak_hist", vv.data(),
                             static_cast<int>(vv.size()), 0, "peak history",
                             0.0F, 1.0F, ImVec2(0, 60));
        }
        // Phase 139 — last 5 s of DSP output dump.
        ImGui::Separator();
        ImGui::TextDisabled("No live audio backend wired in this sample —");
        ImGui::TextDisabled("DSP chain ticks in memory. Save WAV to hear it.");
        if (ImGui::Button("Save Last 5 s as hello_engine_out.wav"))
        {
            // Compose contiguous buffer from ring (oldest → newest).
            std::vector<std::int16_t> samples;
            samples.reserve(kAudioRingFrames);
            std::size_t start = audio_ring_write;
            std::size_t n = (audio_total_written < kAudioRingFrames)
                ? static_cast<std::size_t>(audio_total_written)
                : kAudioRingFrames;
            if (audio_total_written < kAudioRingFrames) start = 0;
            for (std::size_t i = 0; i < n; ++i)
            {
                samples.push_back(audio_ring[(start + i) % kAudioRingFrames]);
            }
            // Minimal WAV header (mono s16) — same encoder shape as
            // hello_audio_chain / hello_audio_synth.
            const std::uint32_t data_bytes =
                static_cast<std::uint32_t>(samples.size() * sizeof(std::int16_t));
            const std::uint32_t fmt_size = 16;
            const std::uint32_t riff_size = 4u + 8u + fmt_size + 8u + data_bytes;
            std::vector<std::byte> bytes;
            bytes.reserve(8u + riff_size);
            auto push_tag = [&](const char (&t)[5]) {
                for (int i = 0; i < 4; ++i) bytes.push_back(static_cast<std::byte>(t[i]));
            };
            auto push_le = [&](std::uint64_t v, int n_bytes) {
                for (int i = 0; i < n_bytes; ++i)
                {
                    const auto shift = static_cast<unsigned>(i) * 8u;
                    bytes.push_back(std::byte{static_cast<unsigned char>((v >> shift) & 0xFFu)});
                }
            };
            push_tag("RIFF"); push_le(riff_size, 4); push_tag("WAVE");
            push_tag("fmt "); push_le(fmt_size, 4);
            push_le(1u, 2);                            // PCM
            push_le(1u, 2);                            // mono
            push_le(kAudioSampleRate, 4);
            push_le(kAudioSampleRate * 1u * 2u, 4);    // byte rate
            push_le(2u, 2);                            // block align
            push_le(16u, 2);                           // bits per sample
            push_tag("data"); push_le(data_bytes, 4);
            bytes.insert(bytes.end(),
                         reinterpret_cast<const std::byte*>(samples.data()),
                         reinterpret_cast<const std::byte*>(samples.data() + samples.size()));
            std::ofstream f { "hello_engine_out.wav", std::ios::binary | std::ios::trunc };
            if (f)
            {
                f.write(reinterpret_cast<const char*>(bytes.data()),
                        static_cast<std::streamsize>(bytes.size()));
                log_push("[audio] wrote hello_engine_out.wav (" +
                         std::to_string(bytes.size()) + " B)");
            }
            else
            {
                log_push("[audio] WAV write failed (ofstream)");
            }
        }
        ImGui::End();

        // ---- Net Sim ----
        ImGui::Begin("Net Sim");
        ImGui::TextColored(net_enabled ? ImVec4(0.4F,1,0.4F,1) : ImVec4(1,0.5F,0.3F,1),
                           "%s", net_enabled ? "RUNNING" : "PAUSED");
        ImGui::Text("server tick 60 Hz | throttle 30 pkt/s | 10%% loss | 30-90 ms latency");
        ImGui::Separator();
        ImGui::Text("sent    %u", net_sent);
        ImGui::Text("recv    %u   (delivery %.1f%%)", net_recv,
                    net_sent==0?0.0:100.0*static_cast<double>(net_recv)/static_cast<double>(net_sent));
        ImGui::Text("drop    %u", net_drop);
        ImGui::Text("raw     %llu B  /  wire %llu B   (%.1f%% wire/raw)",
                    static_cast<unsigned long long>(net_raw_bytes),
                    static_cast<unsigned long long>(net_wire_bytes),
                    net_raw_bytes==0?0.0:100.0*static_cast<double>(net_wire_bytes)/static_cast<double>(net_raw_bytes));
        ImGui::Text("RTT     %.2f ms   jitter %.2f ms",
                    static_cast<double>(net_rtt.current_rtt_us()) / 1000.0,
                    static_cast<double>(net_rtt.jitter_us()) / 1000.0);
        ImGui::Text("snapshots buffered: %zu", net_snapbuf.size());
        ImGui::End();

        // ---- History ----
        ImGui::Begin("History");
        ImGui::Text("undo depth %zu  redo depth %zu  (bytes %zu)",
                    history.undo_depth(), history.redo_depth(), history.bytes_in_use());
        ImGui::Separator();
        for (auto it = log.rbegin(); it != log.rend(); ++it)
            ImGui::TextUnformatted(it->c_str());
        ImGui::End();

        // ---- Palette popup ----
        if (palette_visible)
        {
            const float vw_p = static_cast<float>(frame.extent.width);
            const float pw = 520.0F, ph = 360.0F;
            ImGui::SetNextWindowPos(ImVec2((vw_p - pw) * 0.5F, 80.0F), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(pw, ph), ImGuiCond_Always);
            if (ImGui::Begin("Command Palette", &palette_visible,
                ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking))
            {
                if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
                char buf[128] {};
                std::snprintf(buf, sizeof(buf), "%s", palette_query.c_str());
                if (ImGui::InputText("##q", buf, sizeof(buf))) palette_query = buf;
                ImGui::Separator();
                const auto hits = palette.filter(palette_query);
                if (hits.empty())
                {
                    ImGui::TextDisabled("no match (%zu commands)", palette.size());
                }
                else
                {
                    for (std::size_t i = 0; i < hits.size() && i < 24; ++i)
                    {
                        const auto& e = palette.at(hits[i]);
                        char row[160] {};
                        std::snprintf(row, sizeof(row), "  %s", e.label.c_str());
                        if (ImGui::Selectable(row))
                        {
                            (void)palette.invoke(hits[i]);
                            palette_visible = false;
                            palette_query.clear();
                            break;
                        }
                    }
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) && !hits.empty())
                {
                    (void)palette.invoke(hits.front());
                    palette_visible = false;
                    palette_query.clear();
                }
            }
            ImGui::End();
        }

        // ---- ImGui pass ----
        ctx.render(cmd);
        cmd.end_render_pass();

        auto end_r = renderer.end_frame();
        if (!end_r.has_value())
        {
            if (end_r.error().code == static_cast<std::uint32_t>(
                    cd::render::render_errors::Code::kSwapchainOutOfDate))
            { needs_rebuild = true; continue; }
            return 11;
        }
        ++frame_idx;
        counters.set("frame", frame_idx);
    }

    renderer.wait_idle();

    // ---- Cleanup ----
    destroy_mesh(device, cube_mesh);
    destroy_mesh(device, sphere_mesh);
    destroy_mesh(device, cone_mesh);
    destroy_mesh(device, cyl_mesh);
    destroy_mesh(device, torus_mesh);
    destroy_mesh(device, pbr_sphere);
    depth.destroy(device);
    std::printf("hello_engine: clean exit (%u frames).\n", frame_idx);
    return 0;
}
