// =============================================================================
// CHROMODYNAMIC — samples/hello_pbr
//
// Classic PBR showcase: a 5×5 sphere grid where the X axis sweeps metallic
// from 0 (dielectric) to 1 (metal) and the Y axis sweeps roughness from
// 0.05 (mirror) to 1 (matte). Three-light rig (warm key + cool fill +
// warm back-rim) plus analytical-sky split-sum IBL ambient so metallic
// and dielectric materials read distinctly without a precomputed cubemap.
//
// Material model:
//   * Cook-Torrance specular = D_GGX * G_Smith * F_Schlick / (4 NoV NoL)
//   * Roughness-aware Fresnel for the IBL ambient (Lazarov/Karis)
//   * IBL split-sum stand-in: env sampled along reflection R, blurred
//     toward N by roughness. Real prefiltered cubemap + BRDF LUT comes
//     with the Phase 17 IBL bake pipeline.
//
// Demonstrates that cd::material + cd::shader::ICompiler + the GLSL
// Cook-Torrance pipeline first introduced in hello_gltf works standalone
// (no asset import needed) and that the renderer can dispatch many
// draws with per-instance push constants.
// =============================================================================
#include "GoldenCapture.hpp"
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

// 64-byte push block for the skybox pass — camera basis vectors so the
// fragment shader reconstructs world rays without a matrix inverse.
struct SkyPush
{
    std::array<float, 4> cam_right;  // xyz=right basis, w=half_w = tan(fov/2)*aspect
    std::array<float, 4> cam_up;     // xyz=up    basis, w=half_h = tan(fov/2)
    std::array<float, 4> cam_fwd;    // xyz=forward (toward target), w=_
    std::array<float, 4> sun_dir;    // xyz=normalized direction, w=intensity
};

static_assert(sizeof(SkyPush) == 64, "SkyPush must equal 64 B");

constexpr const char* kSkyVS = R"glsl(
#version 450
// Fullscreen triangle without a vertex buffer — vertex index 0,1,2 maps
// to (-1,-1), (3,-1), (-1,3) which after clipping covers the full NDC.
layout(location = 0) out vec2 v_ndc;
void main() {
  vec2 p = vec2((gl_VertexIndex == 1) ? 3.0 : -1.0,
                (gl_VertexIndex == 2) ? 3.0 : -1.0);
  v_ndc = p;
  gl_Position = vec4(p, 1.0, 1.0);  // z=1 — pin to back of depth range.
}
)glsl";

// Sky FS reuses the exact 3-band palette as the PBR fragment's
// `sample_env` so the spheres' IBL reflections agree with the
// background they sit on. Plus a sun disk so the warm key light has
// a visible source.
constexpr const char* kSkyFS = R"glsl(
#version 450
layout(push_constant) uniform SkyPC {
  vec4 cam_right;
  vec4 cam_up;
  vec4 cam_fwd;
  vec4 sun_dir;
} pc;
layout(location = 0) in  vec2 v_ndc;
layout(location = 0) out vec4 out_color;

vec3 ray_dir(vec2 ndc) {
  vec3 forward = pc.cam_fwd.xyz;
  vec3 right   = pc.cam_right.xyz;
  vec3 up      = pc.cam_up.xyz;
  return normalize(forward
                 + ndc.x * pc.cam_right.w * right
                 - ndc.y * pc.cam_up.w    * up);
}

vec3 sample_env(vec3 dir) {
  vec3 zenith  = vec3(0.18, 0.42, 0.85);
  vec3 horizon = vec3(0.78, 0.86, 0.96);
  vec3 ground  = vec3(0.10, 0.10, 0.14);
  float h = dir.y;
  if (h >= 0.0) return mix(horizon, zenith, pow(clamp(h, 0.0, 1.0), 0.6));
  return mix(horizon, ground, pow(clamp(-h, 0.0, 1.0), 0.5));
}

void main() {
  vec3 dir = ray_dir(v_ndc);
  vec3 sky = sample_env(dir);

  // Sun disk + glow aligned with PBR key light direction.
  vec3 L = normalize(-pc.sun_dir.xyz);
  float cos_a = clamp(dot(dir, L), 0.0, 1.0);
  float disk = smoothstep(0.9994, 0.9998, cos_a);
  float glow = pow(cos_a, 64.0);
  vec3 sun_color = vec3(1.0, 0.93, 0.82) * pc.sun_dir.w;
  vec3 result = sky + sun_color * (disk * 6.0 + glow * 0.5);

  // ACES tonemap (matches sphere shader) + gamma.
  const float a_ = 2.51;
  const float b_ = 0.03;
  const float c_ = 2.43;
  const float d_ = 0.59;
  const float e_ = 0.14;
  result = clamp((result * (a_ * result + b_)) /
                 (result * (c_ * result + d_) + e_),
                 vec3(0.0), vec3(1.0));
  result = pow(result, vec3(1.0 / 2.2));
  out_color = vec4(result, 1.0);
}
)glsl";

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

// Lazarov / Karis roughness-aware Fresnel for IBL ambient. Without this
// rough metals would over-reflect at grazing angles. The "(1-roughness)"
// term collapses the off-axis lobe as roughness rises so a rough copper
// ball looks matte-copper rather than chrome-copper.
vec3 F_Schlick_roughness(float cos_theta, vec3 F0, float roughness) {
  vec3 ceiling = max(vec3(1.0 - roughness), F0);
  return F0 + (ceiling - F0) * pow(clamp(1.0 - cos_theta, 0.0, 1.0), 5.0);
}

// Analytical sky — same 3-band palette as hello_skybox so the sphere
// grid mirrors the same atmosphere a scene-wide skybox would draw.
vec3 sample_env(vec3 dir) {
  vec3 zenith  = vec3(0.18, 0.42, 0.85);
  vec3 horizon = vec3(0.78, 0.86, 0.96);
  vec3 ground  = vec3(0.10, 0.10, 0.14);
  float h = dir.y;
  if (h >= 0.0) return mix(horizon, zenith, pow(clamp(h, 0.0, 1.0), 0.6));
  return mix(horizon, ground, pow(clamp(-h, 0.0, 1.0), 0.5));
}

// Cook-Torrance lobe for one analytical light direction.
vec3 direct_lobe(vec3 N, vec3 V, vec3 L,
                 vec3 albedo, float metallic, float roughness,
                 vec3 F0, vec3 light_color)
{
  vec3 H = normalize(L + V);
  float NoL = max(dot(N, L), 0.0);
  float NoV = max(dot(N, V), 0.0);
  float NoH = max(dot(N, H), 0.0);
  float HoV = max(dot(H, V), 0.0);
  float D = D_GGX(NoH, roughness * roughness);
  float G = G_Smith(NoV, NoL, roughness);
  vec3  F = F_Schlick(HoV, F0);
  vec3 specular = (D * G) * F / (4.0 * NoV * NoL + 1e-7);
  vec3 kS = F;
  vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);
  vec3 diffuse = kD * albedo / PI;
  return (diffuse + specular) * NoL * light_color;
}

void main() {
  vec3 albedo = pc.albedo.rgb;
  float metallic = clamp(pc.mr_amb.x, 0.0, 1.0);
  float roughness = clamp(pc.mr_amb.y, 0.04, 1.0);

  vec3 N = normalize(v_normal);
  vec3 V = normalize(pc.camera_pos.xyz - v_world_pos);
  float NoV = max(dot(N, V), 0.0);
  vec3 F0 = mix(vec3(0.04), albedo, metallic);

  // ---- Direct lighting --------------------------------------------------
  // Three lights: warm key (from PushBlock) + cool fill + back-rim. The
  // fill softens the shadow side; the rim outlines metallic spheres so
  // the camera-side highlight is unmistakable.
  vec3 L_key  = normalize(-pc.light_dir.xyz);
  vec3 L_fill = normalize(vec3( 0.6, 0.3,  0.7));
  vec3 L_rim  = normalize(vec3(-0.1, 0.2, -1.0));
  vec3 C_key  = vec3(1.00, 0.93, 0.82) * pc.light_dir.w;     // warm
  vec3 C_fill = vec3(0.55, 0.70, 0.95) * pc.light_dir.w * 0.30;  // cool
  vec3 C_rim  = vec3(1.00, 0.88, 0.70) * pc.light_dir.w * 0.55;  // back warm

  vec3 direct  = direct_lobe(N, V, L_key,  albedo, metallic, roughness, F0, C_key);
       direct += direct_lobe(N, V, L_fill, albedo, metallic, roughness, F0, C_fill);
       direct += direct_lobe(N, V, L_rim,  albedo, metallic, roughness, F0, C_rim);

  // ---- IBL ambient (split-sum without BRDF LUT) -------------------------
  // Diffuse irradiance ≈ env sampled along N. Specular reflection ≈ env
  // along R, blurred toward N as roughness rises. Phase 17 will land a
  // proper prefiltered cubemap + BRDF LUT; this is the analytical
  // stand-in good enough to make metallic vs. dielectric obvious.
  vec3 R = reflect(-V, N);
  vec3 env_diffuse  = sample_env(N);
  vec3 env_specular = mix(sample_env(R), env_diffuse, roughness);

  vec3 ibl_F  = F_Schlick_roughness(NoV, F0, roughness);
  vec3 ibl_kD = (vec3(1.0) - ibl_F) * (1.0 - metallic);
  vec3 ibl    = ibl_kD * env_diffuse * albedo + env_specular * ibl_F;

  vec3 color = direct + ibl;

  // Narkowicz fitted ACES tone-map + gamma. ACES preserves highlight
  // tint (Fresnel colour on metals) far better than Reinhard, which
  // crushes the warm highlight + cool reflection blend into white and
  // erases the metallic-vs-dielectric distinction.
  // Source: Krzysztof Narkowicz, "ACES Filmic Tone Mapping Curve",
  // 2015. Five-coefficient rational approximation of the ACES RRT+ODT.
  const float a_ = 2.51;
  const float b_ = 0.03;
  const float c_ = 2.43;
  const float d_ = 0.59;
  const float e_ = 0.14;
  color = clamp((color * (a_ * color + b_)) /
                (color * (c_ * color + d_) + e_),
                vec3(0.0), vec3(1.0));
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

    // ---- Skybox material (no vertex buffer, no depth) --------------------
    constexpr std::array<cd::rhi::PushConstantRange, 1> kSkyPush {
        cd::rhi::PushConstantRange { .stages = cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
                                    .offset = 0,
                                    .size = static_cast<std::uint32_t>(sizeof(SkyPush)) }
    };
    cd::material::MaterialDesc smd {};
    smd.vertex_glsl = kSkyVS;
    smd.fragment_glsl = kSkyFS;
    smd.color_attachment_formats = kColorFormats;
    // The sky pass clears the swapchain and writes color, but does NOT
    // touch the depth buffer (depth_write=false, no depth_attachment_format).
    // Sphere pass that follows will then write depth normally.
    smd.push_constants = kSkyPush;
    smd.raster.cull = cd::rhi::CullMode::kNone;
    smd.depth_stencil.depth_test = false;
    smd.depth_stencil.depth_write = false;
    smd.name = "hello_pbr/sky";
    auto sky_r = cd::material::Material::create(device, compiler.get(), smd);
    if (!sky_r.has_value())
        return 7;
    auto& sky_material = *sky_r;

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

        // Clear color is irrelevant — sky pass overwrites every pixel
        // before any sphere draws. Keep magenta so a regression that
        // disables the sky pass is immediately visible.
        std::array<cd::rhi::ColorAttachmentInfo, 1> color_attach {
            cd::rhi::ColorAttachmentInfo { .view = frame.swapchain_image_view,
                                          .load_op = cd::rhi::LoadOp::kClear,
                                          .store_op = cd::rhi::StoreOp::kStore,
                                          .clear_color = { .f32 = { 1.0F, 0.0F, 1.0F, 1.0F } } }
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
        cam.eye = { kOrbitDistance * std::sin(orbit_angle), 1.5F, kOrbitDistance * std::cos(orbit_angle) };
        cam.target = { 0.0F, 0.0F, 0.0F };
        cam.fov_y = 0.85F;
        cam.near_z = 0.1F;
        cam.far_z = 100.0F;
        const float aspect = static_cast<float>(frame.extent.width) / static_cast<float>(frame.extent.height);
        const cd::math::Mat4f vp = cd::camera::view_projection(cam, aspect);
        const std::array<float, 3> camera_pos { cam.eye.x, cam.eye.y, cam.eye.z };

        // ---- Sky pass (first, no depth) -----------------------------------
        // Compute camera basis the same way hello_skybox does, push to the
        // sky material's fragment shader, draw a fullscreen triangle.
        cd::math::Vec3f forward {
            cam.target.x - cam.eye.x, cam.target.y - cam.eye.y, cam.target.z - cam.eye.z,
        };
        const float fl = std::sqrt(forward.x * forward.x + forward.y * forward.y + forward.z * forward.z);
        forward.x /= fl; forward.y /= fl; forward.z /= fl;
        constexpr cd::math::Vec3f world_up { 0.0F, 1.0F, 0.0F };
        cd::math::Vec3f sky_right { forward.y * world_up.z - forward.z * world_up.y,
                                    forward.z * world_up.x - forward.x * world_up.z,
                                    forward.x * world_up.y - forward.y * world_up.x };
        const float rl = std::sqrt(sky_right.x * sky_right.x + sky_right.y * sky_right.y + sky_right.z * sky_right.z);
        sky_right.x /= rl; sky_right.y /= rl; sky_right.z /= rl;
        const cd::math::Vec3f sky_up {
            sky_right.y * forward.z - sky_right.z * forward.y,
            sky_right.z * forward.x - sky_right.x * forward.z,
            sky_right.x * forward.y - sky_right.y * forward.x };
        const float half_h = std::tan(cam.fov_y * 0.5F);
        const float half_w = half_h * aspect;

        SkyPush spush {};
        spush.cam_right = { sky_right.x, sky_right.y, sky_right.z, half_w };
        spush.cam_up    = { sky_up.x,    sky_up.y,    sky_up.z,    half_h };
        spush.cam_fwd   = { forward.x,   forward.y,   forward.z,   0.0F   };
        // Match the sphere shader's L_key (negated because the shader
        // also negates internally: L_key = normalize(-pc.light_dir.xyz)).
        spush.sun_dir   = { -0.4F, -0.6F, -0.7F, 0.9F };

        sky_material.apply(cmd);
        cmd.push_constants(
            sky_material.pipeline_layout(),
            cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
            /*offset=*/0,
            static_cast<std::uint32_t>(sizeof(spush)),
            &spush
        );
        cmd.draw(3, 1, 0, 0);

        // ---- Sphere pass --------------------------------------------------
        material.apply(cmd);
        cmd.bind_vertex_buffer(0, vb, 0);
        cmd.bind_index_buffer(ib, 0, cd::rhi::IndexType::kUInt16);

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
                // Warm copper-ish albedo (0.95, 0.64, 0.32). With ACES
                // tonemap retaining tint and a cool sky reflecting from
                // the upper hemisphere, the X axis reads cleanly as:
                //   metallic=0 → tinted dielectric (F0=0.04 grey-white
                //                highlight on copper diffuse — "shiny
                //                copper-painted plastic")
                //   metallic=1 → tinted metal (F0=albedo, no diffuse,
                //                spec lobe is copper-coloured, reflection
                //                of cool sky tints the upper hemisphere
                //                cyan-on-copper). The Fresnel grazing
                //                rim is unmistakably copper on the right
                //                column and unmistakably white on the
                //                left column.
                pb.albedo = { 0.95F, 0.64F, 0.32F, 1.0F };
                // mr_amb.z (legacy "ambient") is now ignored — IBL term
                // in the shader replaces the flat ambient. Kept the slot
                // for binary-compat with any cached PushBlock layout.
                pb.mr_amb = { metallic, roughness, 0.0F, 0.0F };
                pb.camera_pos = { camera_pos[0], camera_pos[1], camera_pos[2], 0.0F };
                // Key-light intensity 0.9 — with ACES tonemap and the
                // analytical-sky IBL contributing ~0.4 ambient, 0.9 lands
                // peak luminance in ACES's linear region so the Fresnel
                // tint on the metallic side stays visible instead of
                // saturating to white.
                pb.light_dir = { -0.4F, -0.6F, -0.7F, 0.9F };

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
    std::printf("hello_pbr: clean exit (%u frames).\n", frame_idx);
    return golden_rc;
}
