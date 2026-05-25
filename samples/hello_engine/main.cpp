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
#include <cd/asset/AssetId.hpp>
#include <cd/asset/AsyncStreamer.hpp>
#include <cd/asset/Primitives.hpp>
#include <cd/asset/StreamRequest.hpp>
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
#include <cd/editor/AxisGizmo.hpp>
#include <cd/editor/CommandPalette.hpp>
#include <cd/editor/SelectionOutline.hpp>
#include <cd/editor/EditHistory.hpp>
#include <cd/light/Attenuation.hpp>
#include <cd/light/ClusterGrid.hpp>
#include <cd/light/ColorTemperature.hpp>
#include <cd/light/Light.hpp>
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
#include <atomic>
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
#include <thread>
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
    std::uint32_t         vertex_count { 0 };  // Faz 1.7: BLAS reads positions
    std::uint32_t         index_count  { 0 };
};

[[nodiscard]] GpuMesh upload_mesh(cd::rhi::IDevice& dev, const cd::asset::PrimitiveMesh& m)
{
    GpuMesh out {};
    cd::rhi::BufferDesc vbd {};
    vbd.size = m.vertices.size() * sizeof(cd::asset::PrimitiveVertex);
    // Faz 1.7: kStorage + kTransferDst added so the BLAS builder can
    // read positions out of this buffer. Same usage list hello_path_
    // trace uses for its scene VB. kVertex is still here for the
    // raster path.
    vbd.usage = cd::rhi::BufferUsage::kVertex
              | cd::rhi::BufferUsage::kStorage
              | cd::rhi::BufferUsage::kTransferDst;
    vbd.memory = cd::rhi::MemoryUsage::kCpuToGpu;
    auto vb_r = dev.create_buffer(vbd);
    if (!vb_r.has_value()) return out;
    (void)dev.upload_buffer(*vb_r, 0,
        std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(m.vertices.data()), vbd.size));

    cd::rhi::BufferDesc ibd {};
    ibd.size = m.indices.size() * sizeof(std::uint16_t);
    ibd.usage = cd::rhi::BufferUsage::kIndex
              | cd::rhi::BufferUsage::kStorage
              | cd::rhi::BufferUsage::kTransferDst;
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
    out.vertex_count = static_cast<std::uint32_t>(m.vertices.size());
    out.index_count  = static_cast<std::uint32_t>(m.indices.size());
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
    out.vertex_count = static_cast<std::uint32_t>(verts.size());
    out.index_count  = static_cast<std::uint32_t>(m.indices.size());
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
  mat4 model;            // world-space transform of this entity
  vec4 tint;
  vec4 sun_dir;          // xyz=directional dir, w=intensity
  vec4 sun_color;        // xyz=color, w=ambient
  vec4 point_pos_range;  // xyz=world position, w=range (0 = no point light)
  vec4 point_color;      // xyz=color, w=intensity (lumens/4π)
  vec4 spot_dir_cos;     // xyz=spot forward, w=cos(outer); w<=0 = point
} pc;
// Faz 1.6 CSM — light-space view-projection for shadow sampling.
// Set 0 / binding 0 is a per-frame UBO updated each draw cycle by
// the host with the current sun's ortho VP. Binding 1 (sampler) is
// declared in the fragment shader.
layout(set = 0, binding = 0) uniform Shadow {
  mat4 light_vp;
} cd_shadow;
layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;
layout(location = 3) in vec3 in_color;
layout(location = 0) out vec3 v_world_pos;
layout(location = 1) out vec3 v_world_normal;
layout(location = 2) out vec3 v_albedo;
layout(location = 3) out vec4 v_shadow_pos;
void main() {
  v_albedo = in_color * pc.tint.rgb;
  vec4 wp = pc.model * vec4(in_pos, 1.0);
  v_world_pos = wp.xyz;
  // Inverse-transpose-of-model would be more correct for non-uniform
  // scale; for the sample we use model directly (scales are uniform).
  v_world_normal = normalize((pc.model * vec4(in_normal, 0.0)).xyz);
  // Shadow-space position. light_vp is set up so x,y ∈ [-1,1] and
  // z ∈ [0,1] for fragments inside the shadow ortho frustum. Vulkan
  // sample-side flips y to match texture v-down, done in the FS.
  v_shadow_pos = cd_shadow.light_vp * vec4(v_world_pos, 1.0);
  vec4 clip = pc.mvp * vec4(in_pos, 1.0);
  clip.y = -clip.y;
  gl_Position = clip;
}
)glsl";

constexpr const char* kPrimFS = R"glsl(
#version 460
// Faz 1.7 — inline RT shadows via ray queries inside the raster FS.
// VK_KHR_ray_query is required at the device level; the material
// creation gates on device.features().ray_query so this extension
// guard never fires on unsupported hardware. GLSL 460 is required
// because ray-query intrinsics were introduced for that profile.
#extension GL_EXT_ray_query : require
layout(push_constant) uniform PC {
  mat4 mvp;
  mat4 model;
  vec4 tint;
  vec4 sun_dir;
  vec4 sun_color;
  vec4 point_pos_range;
  vec4 point_color;
  vec4 spot_dir_cos;  // Faz 1.8: xyz=spot forward, w=cos(outer) (<=0 = point)
} pc;
// Faz 1.6 CSM descriptors — match the VS layout.
layout(set = 0, binding = 0) uniform Shadow {
  mat4 light_vp;
} cd_shadow;
layout(set = 0, binding = 1) uniform sampler2D cd_shadow_map;
// Faz 1.7 TLAS — rebuilt every frame on the host with the current
// scene transforms. Used to shadow-test punctual / spot / area
// lights that CSM can't cover (CSM is single-directional only).
layout(set = 0, binding = 2) uniform accelerationStructureEXT cd_tlas;
layout(location = 0) in vec3 v_world_pos;
layout(location = 1) in vec3 v_world_normal;
layout(location = 2) in vec3 v_albedo;
layout(location = 3) in vec4 v_shadow_pos;
layout(location = 0) out vec4 out_color;

// Frostbite windowed inverse-square attenuation.
float distance_atten(float d, float range) {
  if (range <= 0.0) return 0.0;
  float ratio = d / range;
  float w = clamp(1.0 - ratio*ratio*ratio*ratio, 0.0, 1.0);
  return (w * w) / (d * d + 0.01);
}

// Faz 1.7 inline RT shadow visibility test. Shoots a ray from the
// surface point toward `dir` for at most `tmax` metres. Returns 1.0
// when nothing blocks (lit) and 0.0 on any committed intersection
// (shadowed). The kTerminateOnFirstHit ray flag lets us early-out as
// soon as the first opaque triangle is hit — no need to find the
// closest one. Ray origin is biased by +1mm along the surface
// normal to dodge self-intersection acne.
float ray_visibility(vec3 origin, vec3 N, vec3 dir, float tmax) {
  rayQueryEXT rq;
  rayQueryInitializeEXT(
      rq, cd_tlas,
      gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT,
      0xFFu,
      origin + N * 0.001,
      0.0, dir, tmax);
  while (rayQueryProceedEXT(rq)) { /* opaque-only walk */ }
  return (rayQueryGetIntersectionTypeEXT(rq, true) ==
          gl_RayQueryCommittedIntersectionNoneEXT) ? 1.0 : 0.0;
}

// 3×3 PCF shadow sampling. Returns 1.0 = fully lit, 0.0 = fully
// occluded. Vulkan clip space x,y ∈ [-1,1], depth ∈ [0,1]; texture
// uv has y down (matches Vulkan clip y after perspective divide).
float sample_shadow(vec4 sp, vec3 N, vec3 L) {
  // Perspective divide — ortho gives w=1 but keep for generality.
  vec3 p = sp.xyz / sp.w;
  // Outside the shadow ortho frustum → assume lit (sky / far away).
  if (p.x < -1.0 || p.x > 1.0 || p.y < -1.0 || p.y > 1.0 ||
      p.z < 0.0 || p.z > 1.0) return 1.0;
  // Vulkan: NDC y down → texture v down, same orientation, no flip.
  vec2 uv = p.xy * 0.5 + 0.5;
  // Slope-scaled depth bias — fights shadow acne on grazing-angle
  // fragments. Coefficient picked empirically.
  float bias = max(0.0025 * (1.0 - max(dot(N, L), 0.0)), 0.0005);
  float ref  = p.z - bias;
  vec2 ts = 1.0 / vec2(textureSize(cd_shadow_map, 0));
  float s = 0.0;
  for (int dy = -1; dy <= 1; ++dy)
    for (int dx = -1; dx <= 1; ++dx) {
      float d = texture(cd_shadow_map, uv + vec2(float(dx), float(dy)) * ts).r;
      s += (d < ref) ? 0.0 : 1.0;
    }
  return s / 9.0;
}

void main() {
  // tint.w sentinel: < 0.5 = "shadow-projection draw" — bypass lighting
  // entirely and output a flat dark silhouette. Used by the planar-
  // shadow pass that re-draws each caster, projected onto the floor
  // plane along the sun direction. Alpha-blend would soften the result
  // but isn't wired in MaterialDesc yet, so we ship hard shadows.
  if (pc.tint.w < 0.5) {
    out_color = vec4(pc.tint.rgb, 1.0);
    return;
  }

  // tint.w > 1.5 = "floor draw" — overlay an analytic grid in the
  // fragment shader instead of via ImGui's foreground draw list. This
  // keeps grid lines properly z-occluded by other geometry (the user-
  // flagged "grid objects arasindan gozukmemeli" issue) for free.
  // Otherwise identical to a normal lit shading path.
  vec3 albedo = v_albedo;
  if (pc.tint.w > 1.5) {
    // Minor cells every 1 m, major every 5 m. fwidth gives a
    // distance-aware line width so lines stay constant-thickness as
    // the camera moves, instead of aliasing into glitter.
    vec2 p   = v_world_pos.xz;
    vec2 dp  = fwidth(p);
    vec2 mod1 = abs(fract(p) - 0.5);
    vec2 mod5 = abs(fract(p * 0.2) - 0.5);
    float lminor = min(mod1.x, mod1.y);
    float lmajor = min(mod5.x, mod5.y);
    float dminor = max(dp.x, dp.y) * 0.7;
    float dmajor = max(dp.x, dp.y) * 0.7 * 0.2;
    float a_minor = 1.0 - smoothstep(0.5 - dminor * 1.5, 0.5 - dminor * 0.5, lminor + 0.5 - dminor);
    float a_major = 1.0 - smoothstep(0.5 - dmajor * 2.0, 0.5 - dmajor * 0.5, lmajor + 0.5 - dmajor);
    // Wrap minor/major into a single intensity. Minor lines are dim
    // greyish; major every-5m and axis lines (x=0 / z=0) are brighter.
    float on_axis_x = step(abs(p.x), max(dp.x, 0.005));
    float on_axis_z = step(abs(p.y), max(dp.y, 0.005));
    vec3 minor_col = vec3(0.50, 0.52, 0.58);
    vec3 major_col = vec3(0.75, 0.78, 0.85);
    vec3 ax_x_col  = vec3(0.95, 0.30, 0.25);   // red = X axis (in world.x)
    vec3 ax_z_col  = vec3(0.25, 0.45, 0.95);   // blue = Z axis (in world.z)
    vec3 line_col  = minor_col;
    float line_a   = a_minor * 0.35;
    line_col = mix(line_col, major_col, smoothstep(0.0, 0.8, a_major));
    line_a   = max(line_a, a_major * 0.6);
    line_col = mix(line_col, ax_x_col, on_axis_z * 0.85);  // axis X runs along z=0
    line_col = mix(line_col, ax_z_col, on_axis_x * 0.85);  // axis Z runs along x=0
    line_a   = max(line_a, max(on_axis_x, on_axis_z));
    albedo = mix(albedo, line_col, clamp(line_a, 0.0, 1.0));
  }

  vec3 N = normalize(v_world_normal);
  vec3 lit = vec3(0.0);

  // Directional sun + CSM shadow attenuation.
  vec3 Ld = normalize(-pc.sun_dir.xyz);
  float ndl_sun = max(dot(N, Ld), 0.0);
  float shade = sample_shadow(v_shadow_pos, N, Ld);
  lit += albedo * pc.sun_color.rgb * (pc.sun_dir.w * ndl_sun * shade);

  // Point / spot light — per-fragment direction + attenuation,
  // gated by an inline RT shadow ray (Faz 1.7). When spot_dir_cos.w
  // > 0 the cone falloff multiplies the contribution; otherwise
  // (point light) the cone factor is 1.0. Skip the ray test when
  // the surface is back-facing the light — saves the rayQuery cost
  // on fragments that would be zeroed by NdotL anyway.
  if (pc.point_pos_range.w > 0.0) {
    vec3 to_p = pc.point_pos_range.xyz - v_world_pos;
    float d   = length(to_p);
    if (d > 1e-4) {
      vec3 Lp = to_p / d;
      float ndl_p = max(dot(N, Lp), 0.0);
      float atten = distance_atten(d, pc.point_pos_range.w);
      // Spot cone falloff (Frostbite-style smooth step). spot_dir.w
      // holds cos(outer); cos(inner) is auto-derived as outer+0.05
      // so the penumbra is a few degrees wide without extra push
      // bytes. spot_dir.w <= 0 disables the cone (acts as point).
      // Lp points from fragment TO light, so the angle between the
      // beam axis and the light-to-frag direction is dot(-Lp, axis).
      float cone = 1.0;
      if (pc.spot_dir_cos.w > 0.0) {
        vec3  spot_axis = normalize(pc.spot_dir_cos.xyz);
        float cos_b     = dot(-Lp, spot_axis);
        float cos_out   = pc.spot_dir_cos.w;
        float cos_in    = clamp(cos_out + 0.05, cos_out, 0.9999);
        cone            = smoothstep(cos_out, cos_in, cos_b);
      }
      float vis = (ndl_p > 0.0 && cone > 0.0)
                ? ray_visibility(v_world_pos, N, Lp, d - 0.01)
                : 0.0;
      lit += albedo * pc.point_color.rgb *
             (pc.point_color.w * ndl_p * atten * vis * cone);
    }
  }

  // Hemisphere ambient (sky-up / ground-down). Top-facing fragments
  // pick up cool sky bounce, bottom-facing pick up warm ground bounce.
  // Cheap stand-in for indirect light until the IBL UBO+descriptor
  // path lands (Faz E). Reference: Lagarde & de Rousiers 2014 §3.
  float up_t   = N.y * 0.5 + 0.5;
  vec3  sky_c  = vec3(0.55, 0.65, 0.85);
  vec3  gnd_c  = vec3(0.18, 0.16, 0.14);
  vec3  hemi   = mix(gnd_c, sky_c, up_t) * pc.sun_color.w;
  vec3  ambient = albedo * hemi;
  vec3  c       = lit + ambient;

  // AGX tonemap (Sobotka 2022) — saturation-preserving on coloured
  // highlights vs. ACES Narkowicz which over-desaturates. Closes the
  // user-flagged "renklerde bir gariplik" anomaly.
  // Source matches cd::post_tonemap::kAgxGlsl line-for-line.
  const float kMinEv = -12.47393, kMaxEv = 4.026069;
  vec3 agx_log = clamp((log2(max(c, vec3(1e-10))) - vec3(kMinEv)) /
                       (kMaxEv - kMinEv), vec3(0.0), vec3(1.0));
  vec3 x2  = agx_log * agx_log;
  vec3 x4  = x2 * x2;
  c = clamp( 15.5  * x4 * x2
           - 40.14 * x4 * agx_log
           + 31.96 * x4
           -  6.868 * x2 * agx_log
           +  0.4298 * x2
           +  0.1191 * agx_log
           -  0.00232, vec3(0.0), vec3(1.0));
  c = pow(c, vec3(1.0/2.2));
  out_color = vec4(c, 1.0);
}
)glsl";

// ============================================================================
// Shadow-map pipeline (Faz 1.6 CSM). Depth-only render pass: a single
// 2K shadow map rendered from the sun's POV with an orthographic
// projection sized to cover the entire scene. Vertex shader is
// trivial — multiply by light_mvp. Fragment shader is empty (depth
// is the only output we need; Vulkan still requires a stage but the
// validator accepts a no-op FS).
// ============================================================================
constexpr const char* kShadowVS = R"glsl(
#version 450
layout(push_constant) uniform PC { mat4 light_mvp; } pc;
layout(location = 0) in vec3 in_pos;
void main() { gl_Position = pc.light_mvp * vec4(in_pos, 1.0); }
)glsl";

constexpr const char* kShadowFS = R"glsl(
#version 450
void main() {}
)glsl";

struct PrimPush
{
    cd::math::Mat4f mvp;
    cd::math::Mat4f model;
    float           tint[4];
    float           sun_dir[4];
    float           sun_color[4];
    float           point_pos_range[4];
    float           point_color[4];
    // Spot-cone — xyz = forward direction (normalised), w = cos(outer
    // half-angle). w <= 0.0 means "this is a point light, no cone";
    // the FS uses w as the sentinel to skip the cone falloff math.
    // Inner half-angle is auto-derived as cos(outer) + 0.05 so the
    // penumbra is a few degrees wide without extra push bytes.
    float           spot_dir_cos[4];
};

static_assert(sizeof(PrimPush) == 224, "PrimPush layout drift");

// ----------------------------------------------------------------------------
// Planar-shadow projection matrix.
//
// Builds the affine transform that flattens any point onto the plane
// y = plane_y along the (directional-light) ray direction `sun_dir`.
// Treats `sun_dir` as the direction the light travels in (so for a sun
// pointing down-forward, sun_dir.y < 0).
//
// Derivation: for caster point P, projected point P' lies on the line
//   P' = P + t * sun_dir,    requiring P'.y = plane_y
//   ⇒ t = (plane_y - P.y) / sun_dir.y
//   ⇒ P'.x = P.x + t * sun_dir.x
//   ⇒ P'.z = P.z + t * sun_dir.z
//
// In column-major Mat4f (cd::math convention, see ADR-017 P4):
//   S[0] = ( 1,            0,       0,            0 )
//   S[1] = ( -Lx/Ly,       0,      -Lz/Ly,        0 )
//   S[2] = ( 0,            0,       1,            0 )
//   S[3] = ( plane_y*Lx/Ly, plane_y, plane_y*Lz/Ly, 1 )
//
// `lift` is added to plane_y in the matrix only so projected verts sit
// just above the floor and dodge depth-fight with it.
// ----------------------------------------------------------------------------
[[nodiscard]] inline cd::math::Mat4f
make_planar_shadow_matrix(const cd::math::Vec3f& sun_dir,
                          float                  plane_y,
                          float                  lift) noexcept
{
    // Clamp |Ly| away from 0 so a sun coming in horizontally doesn't
    // produce an infinite-length shadow (numerically: divide-by-zero).
    constexpr float kMinAbs = 0.10F;
    float Ly = sun_dir.y;
    if (std::fabs(Ly) < kMinAbs) Ly = (Ly < 0.0F) ? -kMinAbs : kMinAbs;
    const float k = 1.0F / Ly;
    const float ax = sun_dir.x * k;
    const float az = sun_dir.z * k;
    const float py = plane_y + lift;
    cd::math::Mat4f m = cd::math::Mat4f::identity();
    // Column 0: x-axis untouched.
    m[0][0] = 1.0F; m[0][1] = 0.0F; m[0][2] = 0.0F; m[0][3] = 0.0F;
    // Column 1: y-input bleeds into x and z, y-output zeroed (plane).
    m[1][0] = -ax;  m[1][1] = 0.0F; m[1][2] = -az;  m[1][3] = 0.0F;
    // Column 2: z-axis untouched.
    m[2][0] = 0.0F; m[2][1] = 0.0F; m[2][2] = 1.0F; m[2][3] = 0.0F;
    // Column 3: translation pins y to plane_y+lift and adds the
    // origin-shift contribution from the (h - 0) projection offset.
    m[3][0] = py * ax;
    m[3][1] = py;
    m[3][2] = py * az;
    m[3][3] = 1.0F;
    return m;
}

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

[[nodiscard]] bool create_depth_target(cd::rhi::IDevice&    dev,
                                       cd::rhi::Extent2D    size,
                                       cd::rhi::Format      format,
                                       DepthTarget&         out,
                                       cd::rhi::TextureUsage extra_usage =
                                           cd::rhi::TextureUsage::kNone)
{
    out.destroy(dev);
    cd::rhi::TextureDesc td {};
    td.type = cd::rhi::TextureType::k2D;
    td.format = format;
    td.extent = { size.width, size.height, 1 };
    td.mip_levels = 1;
    td.array_layers = 1;
    td.usage  = cd::rhi::TextureUsage::kDepthStencilAttachment | extra_usage;
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
    // Faz 1.6 CSM + Faz 1.7 inline RT — three descriptor bindings on
    // the prim pipeline:
    //   0: UBO  with the sun's light_vp matrix (vertex + fragment).
    //   1: sampler2D over the shadow depth map (fragment only).
    //   2: scene TLAS (acceleration structure) for ray queries
    //      against the punctual / spot / area lights' shadow tests.
    // Faz 1.7 requires ray_query device support — gated below before
    // we attempt prim_material creation. Without it the shader's
    // `#extension GL_EXT_ray_query : require` would fail to compile.
    if (!device.features().ray_query)
    {
        std::fprintf(stderr,
            "hello_engine: device lacks VK_KHR_ray_query; "
            "Faz 1.7 inline RT shadows require it. "
            "Re-run on RT-capable hardware or git-checkout f04b588 "
            "(pre-1.7 CSM-only ship).\n");
        return 9;
    }
    constexpr std::array<cd::rhi::DescriptorSetLayoutBinding, 3> kPrimDescBindings {
        cd::rhi::DescriptorSetLayoutBinding { .binding = 0,
                                              .type    = cd::rhi::DescriptorType::kUniformBuffer,
                                              .count   = 1,
                                              .stages  = cd::rhi::ShaderStage::kVertex |
                                                         cd::rhi::ShaderStage::kFragment },
        cd::rhi::DescriptorSetLayoutBinding { .binding = 1,
                                              .type    = cd::rhi::DescriptorType::kCombinedImageSampler,
                                              .count   = 1,
                                              .stages  = cd::rhi::ShaderStage::kFragment },
        cd::rhi::DescriptorSetLayoutBinding { .binding = 2,
                                              .type    = cd::rhi::DescriptorType::kAccelerationStructure,
                                              .count   = 1,
                                              .stages  = cd::rhi::ShaderStage::kFragment } };
    cd::material::MaterialDesc prim_md {};
    prim_md.vertex_glsl   = kPrimVS;
    prim_md.fragment_glsl = kPrimFS;
    prim_md.color_attachment_formats = kColorFmts;
    prim_md.depth_attachment_format = kDepthFormat;
    prim_md.vertex_bindings = kPrimBindings;
    prim_md.vertex_attributes = kPrimAttrs;
    prim_md.push_constants = kPrimPushRange;
    prim_md.descriptor_bindings = kPrimDescBindings;
    prim_md.raster.cull = cd::rhi::CullMode::kNone;
    prim_md.depth_stencil.depth_test = true;
    prim_md.depth_stencil.depth_write = true;
    prim_md.depth_stencil.depth_compare = cd::rhi::CompareOp::kLess;
    prim_md.name = "hello_engine/prim";
    auto prim_r = cd::material::Material::create(device, compiler.get(), prim_md);
    if (!prim_r.has_value())
    {
        std::fprintf(stderr, "hello_engine: prim_material create failed: %.*s\n",
            static_cast<int>(prim_r.error().message.size()),
            prim_r.error().message.data());
        return 9;
    }
    auto& prim_material = *prim_r;

    // Shadow material (Faz 1.6 CSM) — depth-only pipeline (no color
    // attachment) with a trivial mat4 push constant. Used in the
    // shadow pass to rasterize every caster from the sun's POV.
    constexpr std::array<cd::rhi::PushConstantRange, 1> kShadowPushRange {
        cd::rhi::PushConstantRange { .stages = cd::rhi::ShaderStage::kVertex,
                                     .offset = 0,
                                     .size = static_cast<std::uint32_t>(
                                         sizeof(cd::math::Mat4f)) } };
    cd::material::MaterialDesc shadow_md {};
    shadow_md.vertex_glsl   = kShadowVS;
    shadow_md.fragment_glsl = kShadowFS;
    shadow_md.color_attachment_formats = {};  // depth-only
    shadow_md.depth_attachment_format  = kDepthFormat;
    shadow_md.vertex_bindings   = kPrimBindings;
    shadow_md.vertex_attributes = kPrimAttrs;
    shadow_md.push_constants    = kShadowPushRange;
    // Back-face culling for casters reduces shadow acne on the back
    // side of each mesh by ~50%. depth_bias_enable + slope pushes
    // shadow depth slightly away from the caster surface (Persson's
    // shadow-acne mitigation pattern).
    shadow_md.raster.cull = cd::rhi::CullMode::kBack;
    shadow_md.raster.depth_bias_enable   = true;
    shadow_md.raster.depth_bias_constant = 1.25F;
    shadow_md.raster.depth_bias_slope    = 1.75F;
    shadow_md.depth_stencil.depth_test    = true;
    shadow_md.depth_stencil.depth_write   = true;
    shadow_md.depth_stencil.depth_compare = cd::rhi::CompareOp::kLess;
    shadow_md.name = "hello_engine/shadow";
    auto shadow_r = cd::material::Material::create(device, compiler.get(), shadow_md);
    if (!shadow_r.has_value()) return 10;
    auto& shadow_material = *shadow_r;

    // ---- Shadow-map resources (Faz 1.6 CSM) ----
    // 2K depth texture + sampler + UBO holding light_vp. The
    // MaterialInstance below points the prim pipeline at all three.
    constexpr cd::rhi::Extent2D kShadowMapSize { 2048, 2048 };
    DepthTarget shadow_target {};
    if (!create_depth_target(device, kShadowMapSize, kDepthFormat,
                             shadow_target,
                             cd::rhi::TextureUsage::kSampled))
        return 11;
    bool shadow_initialised_on_gpu = false;

    cd::rhi::SamplerDesc shadow_sd {};
    shadow_sd.mag_filter   = cd::rhi::SamplerFilter::kLinear;
    shadow_sd.min_filter   = cd::rhi::SamplerFilter::kLinear;
    shadow_sd.mipmap_mode  = cd::rhi::SamplerMipmapMode::kNearest;
    shadow_sd.address_u    = cd::rhi::SamplerAddressMode::kClampToBorder;
    shadow_sd.address_v    = cd::rhi::SamplerAddressMode::kClampToBorder;
    shadow_sd.address_w    = cd::rhi::SamplerAddressMode::kClampToBorder;
    shadow_sd.border_color = cd::rhi::BorderColor::kFloatOpaqueWhite;  // 1.0 depth = no shadow
    shadow_sd.max_lod      = 1.0F;
    auto shadow_samp_r = device.create_sampler(shadow_sd);
    if (!shadow_samp_r.has_value()) return 12;
    const auto shadow_sampler = *shadow_samp_r;

    cd::rhi::BufferDesc shadow_ubo_desc {};
    shadow_ubo_desc.size   = sizeof(cd::math::Mat4f);  // 64 bytes
    shadow_ubo_desc.usage  = cd::rhi::BufferUsage::kUniform;
    shadow_ubo_desc.memory = cd::rhi::MemoryUsage::kCpuToGpu;
    auto shadow_ubo_r = device.create_buffer(shadow_ubo_desc);
    if (!shadow_ubo_r.has_value()) return 13;
    const auto shadow_ubo = *shadow_ubo_r;

    auto prim_inst_r = cd::material::MaterialInstance::create(device, prim_material);
    if (!prim_inst_r.has_value()) return 14;
    auto& prim_inst = *prim_inst_r;
    {
        std::array<cd::rhi::DescriptorWrite, 2> writes {
            cd::rhi::DescriptorWrite { .binding = 0,
                                       .array_element = 0,
                                       .type = cd::rhi::DescriptorType::kUniformBuffer,
                                       .buffer = shadow_ubo,
                                       .buffer_offset = 0,
                                       .buffer_range = sizeof(cd::math::Mat4f) },
            cd::rhi::DescriptorWrite { .binding = 1,
                                       .array_element = 0,
                                       .type = cd::rhi::DescriptorType::kCombinedImageSampler,
                                       .view = shadow_target.view,
                                       .sampler = shadow_sampler } };
        if (auto wr = prim_inst.update(writes); !wr.has_value()) return 15;
    }

    // ---- Meshes (one PBR sphere, five primitive entities) ----
    const auto cube_cpu     = cd::asset::make_cube();
    const auto sphere_cpu   = cd::asset::make_sphere(18, 28);
    const auto cone_cpu     = cd::asset::make_cone(32);
    const auto cyl_cpu      = cd::asset::make_cylinder(32);
    const auto torus_cpu    = cd::asset::make_torus(0.45F, 0.18F, 16, 24);
    // Floor quad — 80×80 m centred at origin, normal +Y. Faz 1.5: real
    // geometry on which the planar shadow pass can project caster
    // silhouettes (no actual floor previously, only an ImGui grid).
    const auto floor_cpu    = cd::asset::make_plane(80.0F);

    GpuMesh cube_mesh   = upload_mesh(device, cube_cpu);
    GpuMesh sphere_mesh = upload_mesh(device, sphere_cpu);
    GpuMesh cone_mesh   = upload_mesh(device, cone_cpu);
    GpuMesh cyl_mesh    = upload_mesh(device, cyl_cpu);
    GpuMesh torus_mesh  = upload_mesh(device, torus_cpu);
    GpuMesh floor_mesh  = upload_mesh(device, floor_cpu);
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

    // ---- Faz 1.7 — per-mesh-kind BLAS ----
    // One BLAS per shape (cube / sphere / cone / cylinder / torus +
    // floor quad). Geometry is static, so we build these once at
    // boot and keep them for the lifetime of the program.
    auto build_blas = [&](const GpuMesh& m,
                          std::string_view name) -> cd::rhi::AccelStructureHandle {
        cd::rhi::AccelTriangleGeometry tri {};
        tri.vertex_buffer = m.vb;
        tri.vertex_offset = 0;
        tri.vertex_count  = m.vertex_count;
        tri.vertex_stride = sizeof(cd::asset::PrimitiveVertex);
        tri.index_buffer  = m.ib;
        tri.index_offset  = 0;
        tri.index_count   = m.index_count;
        tri.index_type    = cd::rhi::IndexType::kUInt16;
        std::array<cd::rhi::AccelTriangleGeometry, 1> tris { tri };
        cd::rhi::AccelStructureDesc bd {};
        bd.kind        = cd::rhi::AccelStructureKind::kBottomLevel;
        bd.triangles   = std::span<const cd::rhi::AccelTriangleGeometry>(tris);
        bd.debug_name  = name;
        auto r = device.create_acceleration_structure(bd);
        return r.has_value() ? *r : cd::rhi::AccelStructureHandle {};
    };
    cd::rhi::AccelStructureHandle blas_cube   = build_blas(cube_mesh,   "blas_cube");
    cd::rhi::AccelStructureHandle blas_sphere = build_blas(sphere_mesh, "blas_sphere");
    cd::rhi::AccelStructureHandle blas_cone   = build_blas(cone_mesh,   "blas_cone");
    cd::rhi::AccelStructureHandle blas_cyl    = build_blas(cyl_mesh,    "blas_cyl");
    cd::rhi::AccelStructureHandle blas_torus  = build_blas(torus_mesh,  "blas_torus");
    cd::rhi::AccelStructureHandle blas_floor  = build_blas(floor_mesh,  "blas_floor");
    auto blas_for_kind = [&](PrimitiveKind k) -> cd::rhi::AccelStructureHandle {
        switch (k)
        {
            case PrimitiveKind::kSphere:   return blas_sphere;
            case PrimitiveKind::kCone:     return blas_cone;
            case PrimitiveKind::kCylinder: return blas_cyl;
            case PrimitiveKind::kTorus:    return blas_torus;
            default:                       return blas_cube;
        }
    };
    // Build all BLAS on a one-shot cmd buffer. The renderer's
    // per-frame cmd buffers don't exist until begin_frame, so we
    // borrow a transient one for this boot operation.
    {
        auto bcmd_ptr = device.create_command_buffer();
        if (bcmd_ptr == nullptr) return 16;
        auto& bcmd = *bcmd_ptr;
        bcmd.begin();
        for (auto h : { blas_cube, blas_sphere, blas_cone, blas_cyl,
                        blas_torus, blas_floor })
            if (h.is_valid()) bcmd.build_acceleration_structure(h);
        bcmd.end();
        cd::rhi::SubmitDesc bsd {};
        std::array<cd::rhi::ICommandBuffer*, 1> bcbs { &bcmd };
        bsd.command_buffers = bcbs;
        (void)device.submit(bsd);
        device.wait_idle();
    }

    // Per-frame TLAS scratch. `current_tlas` is what the descriptor
    // points at this frame; `tlas_destroy_queue` holds handles whose
    // destroy must wait until the renderer has cycled past the
    // submission that referenced them (frames_in_flight=2 → wait 3
    // frames as a defensive margin).
    cd::rhi::AccelStructureHandle current_tlas {};
    struct DeferredTlas { cd::rhi::AccelStructureHandle h; std::uint32_t destroy_at_frame; };
    std::deque<DeferredTlas> tlas_destroy_queue;

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
    // Selection kind — entities and lights are both pickable.
    enum class SelKind : std::uint8_t { kEntity = 0, kLight = 1 };
    SelKind selected_kind = SelKind::kEntity;

    // Phase 151 — selection-outline state. Style defaults to
    // kWireframe (the cheapest of the three documented techniques
    // and the one we draw as an ImGui foreground overlay below).
    cd::editor::SelectionOutline outline;
    outline.style = cd::editor::OutlineStyle::kWireframe;

    // Phase 152 — axis-translation gizmo state + UI bookkeeping.
    cd::editor::AxisGizmo gizmo;
    bool gizmo_visible = true;       // toggle via palette
    enum class GizmoMode : std::uint8_t { kTranslate = 0, kRotate = 1, kScale = 2 };
    GizmoMode gizmo_mode = GizmoMode::kTranslate;
    ImVec2 gizmo_drag_anchor { 0,0 }; // screen-pixel mouse at begin_drag
    cd::math::Vec3f gizmo_drag_world_start {};  // target position at begin_drag
    cd::math::Vec3f gizmo_drag_scale_start { 1.0F, 1.0F, 1.0F };  // scale at begin_drag
    cd::math::Quatf gizmo_drag_rot_start {};                       // rotation at begin_drag
    // Cross-frame: was the mouse on an axis arrow LAST frame? Used so
    // the pick path (which runs earlier in the frame than the gizmo
    // overlay) can suppress entity-pick when the user is starting a
    // gizmo drag. One-frame lag is invisible at 60+ FPS.
    bool gizmo_was_hovered = false;

    // ---- Camera + SceneCameraController (orbit) ----
    cd::camera::Camera cam {};
    cam.eye = { 0.0F, 2.5F, 8.0F };
    cam.target = { 0.0F, 0.5F, 0.0F };
    cam.fov_y = 0.9F;
    cam.near_z = 0.05F;
    cam.far_z = 200.0F;
    cd::scene::SceneCameraController scene_cam;
    scene_cam.attach(cam, scene, /*follow=*/{});
    scene_cam.set_auto_spin(false);    // user-controlled by default; toggle from palette
    scene_cam.orbit().auto_spin_rate = 0.25F;

    // ---- Free-look camera state (WASD + right-mouse look + wheel zoom) ----
    // When the user holds the right mouse button, we disable auto-spin and
    // switch to FPS-style yaw/pitch from mouse delta + WASD translation.
    bool   cam_right_drag    = false;
    float  cam_yaw           = 0.0F;    // around +Y
    float  cam_pitch         = -0.15F;  // looking slightly down
    float  cam_dist          = 8.0F;    // distance from target (used as zoom)
    float  last_mouse_x      = 0.0F;
    float  last_mouse_y      = 0.0F;
    bool   has_last_mouse    = false;
    bool   key_w = false, key_a = false, key_s = false, key_d = false;
    bool   key_q = false, key_e = false;  // up/down
    constexpr float kCamMoveSpeed = 6.0F;     // m/s
    constexpr float kCamLookSpeed = 0.005F;   // rad/pixel

    // ---- Pick state (3D click-to-select) ----
    // Left click in the viewport casts a ray from the mouse pixel into
    // world space and tests against every entity's sphere bound.
    bool pending_pick = false;
    float pick_x = 0.0F, pick_y = 0.0F;

    // ---- Manual camera mode ----
    // Once the user touches WASD or right-mouse drag, the camera goes
    // into "manual mode" and scene_cam stops updating cam.eye/target —
    // otherwise the orbit camera snaps the eye back to its own pose on
    // every frame. Manual mode persists until palette "Camera: Toggle
    // Auto-Spin" is hit (which re-engages scene_cam orbit).
    bool cam_manual_mode = false;

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
    bool          audio_muted = true;  // start muted; palette "Audio: Toggle Mute" opens it
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
            // Start silent so the user doesn't get a sudden tone. The
            // "Audio: Toggle Mute" palette command unmutes to 0.65F.
            auto voice_r = audio_backend->play(live_clip, /*vol=*/0.0F, /*loop=*/true);
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

    // ---- cd::light demo (Phase 171/172) ----
    // 4 lights representing the four common light types. Each has a
    // CCT slider that drives the color via Krystek's CCT→RGB; the
    // panel previews the resulting linear RGB.
    struct LightRow
    {
        std::string         name;
        cd::light::Light    light;
        bool                enabled { true };
        float               kelvin  { 6500.0F };  // mirrors light.color_kelvin
    };
    std::vector<LightRow> lights;
    lights.push_back({ "Sun (cool 6500K)",
        cd::light::directional({ -0.3F, -0.9F, -0.2F }, { 1, 1, 1 }, 100000.0F),
        true, 6500.0F });
    lights.push_back({ "Tungsten point (2700K)",
        cd::light::point({ 2.0F, 2.0F, -2.0F }, { 1, 1, 1 }, 1200.0F, 8.0F),
        true, 2700.0F });
    lights.push_back({ "Halogen spot (3200K)",
        cd::light::spot({ -2.0F, 3.0F, 1.0F }, { 0.4F, -1.0F, -0.2F },
                        { 1, 1, 1 }, 1500.0F, 10.0F, 0.4F, 0.7F),
        true, 3200.0F });
    lights.push_back({ "Cyan rect-area (8000K)",
        cd::light::rect_area({ 0.0F, 4.0F, 3.0F }, { 0, 0, -1 }, { 1, 0, 0 },
                             3.0F, 1.0F, { 0.6F, 0.85F, 1.0F }, 800.0F),
        true, 8000.0F });

    // Per-frame ClusterGrid for stats. View-space Z range here is just
    // for the panel's "lights per cluster" preview.
    cd::light::ClusterGrid cluster_grid;
    cd::light::ClusterGridDesc cluster_desc;
    cluster_desc.tiles_x = 8; cluster_desc.tiles_y = 4; cluster_desc.slices_z = 8;
    cluster_desc.near_z = 0.1F; cluster_desc.far_z = 100.0F;
    cluster_grid.configure(cluster_desc);

    // ---- AsyncStreamer demo (Phase 150) ----
    // Drives a background worker thread that processes simulated load
    // requests with a sleep so the streamer panel can show pending →
    // in-flight → complete transitions in real time.
    std::atomic<std::uint32_t> streamer_completed { 0 };
    std::atomic<std::uint32_t> streamer_failed    { 0 };
    cd::asset::AsyncStreamer streamer {
        [&streamer_completed, &streamer_failed](cd::asset::AssetId id) -> bool {
            const auto v = id.value();
            std::this_thread::sleep_for(std::chrono::milliseconds(120 + (v % 5) * 80));
            const bool ok = (v % 17 != 0);
            if (ok) streamer_completed.fetch_add(1, std::memory_order_relaxed);
            else    streamer_failed.fetch_add(1, std::memory_order_relaxed);
            return ok;
        }
    };
    streamer.start();
    std::vector<cd::asset::AssetId> streamer_tracked;
    std::uint64_t                   streamer_next_id = 1;
    auto streamer_enqueue = [&](std::int32_t priority) {
        cd::asset::AssetId id { streamer_next_id++ };
        cd::asset::StreamRequest req;
        req.id = id;
        req.priority = priority;
        streamer.enqueue(req);
        streamer_tracked.push_back(id);
        if (streamer_tracked.size() > 32)
            streamer_tracked.erase(streamer_tracked.begin(),
                                   streamer_tracked.begin() + 8);
    };

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
             // Re-engaging auto-spin also exits manual mode so the
             // orbit camera takes back control.
             if (scene_cam.auto_spin()) cam_manual_mode = false;
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
        [&]{ log_push("Ctrl+Shift+P / F1: command palette");
             log_push("Esc: close palette / quit");
             log_push("WASD: move camera target | Q/E: down/up");
             log_push("Right-mouse drag: FPS look | wheel: zoom");
             log_push("Left-click entity: select | empty space: unselect");
             log_push("F: focus camera on selected");
             log_push("Space: cycle gizmo mode (Translate/Rotate/Scale)"); });

    // Phase 154 — scene save/load round-trip. The serializer pulls
    // transforms out of cd::scene::Scene; per-entity metadata (name,
    // tint, primitive kind) rides the WriteExtras/ReadExtras callbacks
    // so the round-trip is lossless.
    constexpr const char* kSavePath = "hello_engine.cdscene.json";
    auto kind_name = [](PrimitiveKind k) -> const char* {
        switch (k)
        {
            case PrimitiveKind::kSphere:   return "Sphere";
            case PrimitiveKind::kCone:     return "Cone";
            case PrimitiveKind::kCylinder: return "Cylinder";
            case PrimitiveKind::kTorus:    return "Torus";
            case PrimitiveKind::kCube:     return "Cube";
        }
        return "Cube";
    };
    palette.register_command(80, "Scene: Save",
        [&]{
            auto find_entity = [&](cd::ecs::Entity e) -> const SceneEntity* {
                for (const auto& en : entities)
                    if (en.handle.id == e.id) return &en;
                return nullptr;
            };
            auto root = cd::scene::serialize_scene_with(scene,
                [&](cd::ecs::Entity e, cd::asset_json::Object& obj) {
                    const auto* en = find_entity(e);
                    if (en == nullptr) return;
                    obj["name"] = cd::asset_json::Value { en->name };
                    obj["kind"] = cd::asset_json::Value { std::string { kind_name(en->kind) } };
                    cd::asset_json::Array tint;
                    tint.push_back(cd::asset_json::Value { static_cast<double>(en->tint.x) });
                    tint.push_back(cd::asset_json::Value { static_cast<double>(en->tint.y) });
                    tint.push_back(cd::asset_json::Value { static_cast<double>(en->tint.z) });
                    obj["tint"] = cd::asset_json::Value { std::move(tint) };
                });
            const auto text = cd::asset_json::serialize(root, /*pretty=*/true);
            std::ofstream f { kSavePath, std::ios::binary | std::ios::trunc };
            if (f)
            {
                f.write(text.data(), static_cast<std::streamsize>(text.size()));
                log_push(std::string { "[scene] Saved " } + std::to_string(entities.size()) +
                         " entities to " + kSavePath);
            }
            else
            {
                log_push("[scene] Save failed (ofstream)");
            }
        });
    palette.register_command(95, "Gizmo: Toggle Visibility",
        [&]{ gizmo_visible = !gizmo_visible;
             log_push(std::string("[gizmo] visible=") + (gizmo_visible?"true":"false")); });
    palette.register_command(90, "Streamer: Enqueue 8 burst",
        [&]{ for (int i = 0; i < 8; ++i) streamer_enqueue(i * 10);
             log_push("[palette] Streamer +8 burst"); });
    palette.register_command(91, "Streamer: Enqueue 32 burst",
        [&]{ for (int i = 0; i < 32; ++i) streamer_enqueue(i % 4);
             log_push("[palette] Streamer +32 burst"); });
    palette.register_command(81, "Scene: Load (replace world)",
        [&]{
            auto r = cd::asset_json::load(kSavePath);
            if (!r.has_value())
            {
                log_push(std::string { "[scene] Load failed: " } + std::string { r.error().message });
                return;
            }
            // Build a fresh world+scene; old `world` / `scene` get
            // replaced via assignment (cd::scene::Scene holds a
            // reference so we have to rebuild entities vector too).
            // The simpler path: clear `entities`, deserialize into the
            // existing scene, and pull metadata back from the JSON.
            for (auto& en : entities)
            {
                if (en.handle.is_valid()) scene.destroy_node(en.handle);
            }
            entities.clear();
            std::vector<SceneEntity> loaded;
            auto rd = cd::scene::deserialize_scene_with(scene, *r,
                [&](cd::ecs::Entity e, const cd::asset_json::Object& obj) {
                    SceneEntity en;
                    en.handle = e;
                    en.kind = PrimitiveKind::kCube;
                    en.tint = { 1.0F, 1.0F, 1.0F };
                    if (auto it = obj.find("name"); it != obj.end() && it->second.is_string())
                        en.name = it->second.as_string();
                    if (auto it = obj.find("kind"); it != obj.end() && it->second.is_string())
                        en.kind = kind_from_name(it->second.as_string());
                    if (auto it = obj.find("tint"); it != obj.end() && it->second.is_array() &&
                        it->second.as_array().size() == 3)
                    {
                        const auto& a = it->second.as_array();
                        if (a[0].is_number() && a[1].is_number() && a[2].is_number())
                        {
                            en.tint = {
                                static_cast<float>(a[0].as_number()),
                                static_cast<float>(a[1].as_number()),
                                static_cast<float>(a[2].as_number()),
                            };
                        }
                    }
                    loaded.push_back(std::move(en));
                });
            if (!rd.has_value())
            {
                log_push(std::string { "[scene] Deserialize failed: " } +
                         std::string { rd.error().message });
                return;
            }
            entities = std::move(loaded);
            selected = entities.empty() ? -1 : 0;
            history.clear();
            log_push(std::string { "[scene] Loaded " } + std::to_string(entities.size()) +
                     " entities from " + kSavePath);
        });

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
                // ESC priority chain (lessons-learned §P3):
                //   1) active gizmo drag → cancel + revert
                //   2) palette visible    → close palette
                //   3) selection active   → clear selection
                //   4) otherwise          → no-op (NEVER quit)
                //
                // User feedback: ESC kept closing the window even with
                // the priority chain, because empty editor state fell
                // through to window.request_close(). Production editors
                // (Unity, Blender, UE) never quit on ESC — quit is a
                // menu / close-button action only. Match that.
                if (gizmo.is_dragging())
                {
                    (void)gizmo.end_drag();
                    log_push("[esc] gizmo drag cancelled");
                }
                else if (palette_visible)
                {
                    palette_visible = false;
                    palette_query.clear();
                    log_push("[esc] palette closed");
                }
                else if (selected >= 0)
                {
                    selected = -1;
                    log_push("[esc] selection cleared");
                }
                // else: do nothing — ESC must never close the window.
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

            // ---- WASD movement keys (continuous state) ----
            const bool key_dn = (e.kind == cd::platform::OSEventKind::kKeyDown);
            const bool key_up = (e.kind == cd::platform::OSEventKind::kKeyUp);
            if (key_dn || key_up)
            {
                const bool v = key_dn;
                if (e.key == cd::platform::KeyCode::kW) key_w = v;
                if (e.key == cd::platform::KeyCode::kA) key_a = v;
                if (e.key == cd::platform::KeyCode::kS) key_s = v;
                if (e.key == cd::platform::KeyCode::kD) key_d = v;
                if (e.key == cd::platform::KeyCode::kQ) key_q = v;
                if (e.key == cd::platform::KeyCode::kE) key_e = v;
                // Engage manual mode on any WASD/QE press so scene_cam
                // stops fighting the user.
                if (key_dn && (e.key == cd::platform::KeyCode::kW ||
                               e.key == cd::platform::KeyCode::kA ||
                               e.key == cd::platform::KeyCode::kS ||
                               e.key == cd::platform::KeyCode::kD ||
                               e.key == cd::platform::KeyCode::kQ ||
                               e.key == cd::platform::KeyCode::kE))
                {
                    cam_manual_mode = true;
                    scene_cam.set_auto_spin(false);
                }
            }
            // F = focus the camera on the currently selected entity (frame).
            if (key_dn && e.key == cd::platform::KeyCode::kF &&
                selected >= 0 && selected < static_cast<int>(entities.size()))
            {
                if (auto* lt = scene.local(entities[static_cast<std::size_t>(selected)].handle))
                {
                    cam.target.x = lt->value.position.x;
                    cam.target.y = lt->value.position.y;
                    cam.target.z = lt->value.position.z;
                    log_push("[cam] focus " + entities[static_cast<std::size_t>(selected)].name);
                }
            }
            // Space = cycle gizmo mode translate → rotate → scale → translate.
            if (key_dn && e.key == cd::platform::KeyCode::kSpace &&
                !ImGui::GetIO().WantCaptureKeyboard)
            {
                gizmo_mode = static_cast<GizmoMode>(
                    (static_cast<std::uint8_t>(gizmo_mode) + 1u) % 3u);
                const char* mode_str =
                    gizmo_mode == GizmoMode::kTranslate ? "TRANSLATE" :
                    gizmo_mode == GizmoMode::kRotate    ? "ROTATE"    :
                                                          "SCALE";
                log_push(std::string { "[gizmo] mode: " } + mode_str);
            }
            // Delete = remove currently selected entity OR light (user feedback:
            // "objeleri ve isiklari kafama gore silebilmeliyim"). Gated on
            // WantCaptureKeyboard so text-input fields in ImGui don't trigger.
            if (key_dn && e.key == cd::platform::KeyCode::kDelete &&
                !ImGui::GetIO().WantCaptureKeyboard && selected >= 0)
            {
                if (selected_kind == SelKind::kEntity &&
                    selected < static_cast<int>(entities.size()))
                {
                    const std::string name = entities[static_cast<std::size_t>(selected)].name;
                    scene.destroy_node(entities[static_cast<std::size_t>(selected)].handle);
                    entities.erase(entities.begin() + selected);
                    log_push(std::string { "[edit] entity deleted: " } + name);
                }
                else if (selected_kind == SelKind::kLight &&
                         selected < static_cast<int>(lights.size()))
                {
                    const std::string name = lights[static_cast<std::size_t>(selected)].name;
                    lights.erase(lights.begin() + selected);
                    log_push(std::string { "[edit] light deleted: " } + name);
                }
                selected = -1;
            }

            // ---- Right-mouse drag → FPS look; left-click → request pick ----
            if (e.kind == cd::platform::OSEventKind::kMouseButtonDown)
            {
                if (e.mouse_button == cd::platform::MouseButton::kRight)
                {
                    cam_right_drag   = true;
                    cam_manual_mode  = true;       // persist until user re-enables auto-spin
                    has_last_mouse   = false;
                    scene_cam.set_auto_spin(false);
                    // INIT yaw/pitch + dist from the current orbit camera so
                    // the right-drag mode doesn't snap to a default pose.
                    const float dxd = cam.target.x - cam.eye.x;
                    const float dyd = cam.target.y - cam.eye.y;
                    const float dzd = cam.target.z - cam.eye.z;
                    const float dist = std::sqrt(dxd*dxd + dyd*dyd + dzd*dzd);
                    if (dist > 1e-3F)
                    {
                        cam_dist  = dist;
                        cam_pitch = std::asin(dyd / dist);
                        cam_yaw   = std::atan2(dxd, -dzd);
                    }
                }
                else if (e.mouse_button == cd::platform::MouseButton::kLeft &&
                         !ImGui::GetIO().WantCaptureMouse)
                {
                    pending_pick = true;
                    pick_x = e.mouse_x;
                    pick_y = e.mouse_y;
                }
            }
            if (e.kind == cd::platform::OSEventKind::kMouseButtonUp &&
                e.mouse_button == cd::platform::MouseButton::kRight)
            {
                cam_right_drag = false;
            }
            if (e.kind == cd::platform::OSEventKind::kMouseMove)
            {
                if (cam_right_drag && has_last_mouse)
                {
                    const float dx = e.mouse_x - last_mouse_x;
                    const float dy = e.mouse_y - last_mouse_y;
                    cam_yaw   -= dx * kCamLookSpeed;
                    cam_pitch -= dy * kCamLookSpeed;
                    // Clamp pitch so we don't flip the camera over.
                    constexpr float kHalfPi = 1.5707963F;
                    if (cam_pitch >  kHalfPi - 0.05F) cam_pitch =  kHalfPi - 0.05F;
                    if (cam_pitch < -kHalfPi + 0.05F) cam_pitch = -kHalfPi + 0.05F;
                }
                last_mouse_x   = e.mouse_x;
                last_mouse_y   = e.mouse_y;
                has_last_mouse = true;
            }
            if (e.kind == cd::platform::OSEventKind::kMouseWheel &&
                !ImGui::GetIO().WantCaptureMouse)
            {
                cam_dist *= (e.wheel > 0.0F) ? 0.9F : 1.1F;
                if (cam_dist < 1.0F)   cam_dist = 1.0F;
                if (cam_dist > 100.0F) cam_dist = 100.0F;
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
        // If the user is right-dragging OR pressing any WASD key, take
        // direct control: the SceneCameraController's orbit is bypassed
        // and we drive cam.eye / cam.target from yaw/pitch/dist + WASD.
        const bool wasd_active = key_w || key_a || key_s || key_d || key_q || key_e;
        if (cam_right_drag || wasd_active)
        {
            // On WASD-first frame, sync yaw/pitch/dist from current cam so
            // the position doesn't snap.
            static bool wasd_was_active_prev = false;
            if (wasd_active && !wasd_was_active_prev && !cam_right_drag)
            {
                const float dxd = cam.target.x - cam.eye.x;
                const float dyd = cam.target.y - cam.eye.y;
                const float dzd = cam.target.z - cam.eye.z;
                const float dist = std::sqrt(dxd*dxd + dyd*dyd + dzd*dzd);
                if (dist > 1e-3F)
                {
                    cam_dist  = dist;
                    cam_pitch = std::asin(dyd / dist);
                    cam_yaw   = std::atan2(dxd, -dzd);
                }
            }
            wasd_was_active_prev = wasd_active;

            // Forward = view direction in world space.
            const float cp = std::cos(cam_pitch), sp = std::sin(cam_pitch);
            const float cy = std::cos(cam_yaw),   sy = std::sin(cam_yaw);
            cd::math::Vec3f forward { cp * sy, sp, -cp * cy };
            cd::math::Vec3f right   { cy,      0.0F, sy };

            // WASD moves the camera *target* (and eye follows by cam_dist).
            const float spd = kCamMoveSpeed * dt;
            if (key_w) { cam.target.x += forward.x * spd; cam.target.y += forward.y * spd; cam.target.z += forward.z * spd; }
            if (key_s) { cam.target.x -= forward.x * spd; cam.target.y -= forward.y * spd; cam.target.z -= forward.z * spd; }
            if (key_d) { cam.target.x += right.x   * spd; cam.target.z += right.z   * spd; }
            if (key_a) { cam.target.x -= right.x   * spd; cam.target.z -= right.z   * spd; }
            if (key_e) { cam.target.y += spd; }
            if (key_q) { cam.target.y -= spd; }

            // Eye = target - forward * cam_dist (so the target stays in view).
            cam.eye.x = cam.target.x - forward.x * cam_dist;
            cam.eye.y = cam.target.y - forward.y * cam_dist;
            cam.eye.z = cam.target.z - forward.z * cam_dist;
        }
        else if (!cam_manual_mode)
        {
            // Only auto-orbit if the user hasn't started manual control.
            // Once manual mode engages, the camera stays exactly where
            // the user left it on right-mouse release / WASD release.
            scene_cam.update(dt);
        }

        // ---- 3D click-to-pick ----
        // Unproject the click pixel to a world ray, then sphere-test
        // each entity. The gizmo overlay (rendered later in this
        // frame) may set `pending_pick=false` if the click landed on
        // an axis arrow — in that case it consumed the click and we
        // skip the pick. The frame here is one-late but for a UX
        // click the lag is invisible.
        if (pending_pick && gizmo_was_hovered)
        {
            // The user is clicking on a gizmo arrow (hover detected
            // last frame). Don't repick; let the gizmo claim the drag.
            pending_pick = false;
        }
        if (pending_pick)
        {
            pending_pick = false;
            const float vw = static_cast<float>(window.width());
            const float vh = static_cast<float>(window.height());
            if (vw > 0 && vh > 0)
            {
                const float aspect_pick = vw / vh;
                // Invert VP analytically would be ideal; we use unproject
                // via two ray endpoints (NDC near + far) → world.
                const float ndc_x = (2.0F * pick_x / vw) - 1.0F;
                const float ndc_y = 1.0F - (2.0F * pick_y / vh);
                // Build inverse VP by row-by-row 4x4 inversion. Use the
                // engine's existing utility if present; otherwise a small
                // local Gauss-Jordan would do. Quick path: use camera
                // basis directly.
                const float cp = std::cos(cam_pitch), sp = std::sin(cam_pitch);
                const float cy = std::cos(cam_yaw),   sy = std::sin(cam_yaw);
                cd::math::Vec3f fwd { cp * sy, sp, -cp * cy };
                cd::math::Vec3f rgt { cy,      0.0F, sy };
                cd::math::Vec3f up_v {
                    fwd.y*rgt.z - fwd.z*rgt.y,
                    fwd.z*rgt.x - fwd.x*rgt.z,
                    fwd.x*rgt.y - fwd.y*rgt.x };
                // Use the orbit camera's basis when we're NOT in WASD mode.
                if (!cam_right_drag && !wasd_active)
                {
                    fwd.x = cam.target.x - cam.eye.x;
                    fwd.y = cam.target.y - cam.eye.y;
                    fwd.z = cam.target.z - cam.eye.z;
                    const float fl = std::sqrt(fwd.x*fwd.x + fwd.y*fwd.y + fwd.z*fwd.z);
                    if (fl > 1e-5F) { fwd.x/=fl; fwd.y/=fl; fwd.z/=fl; }
                    cd::math::Vec3f world_up { 0,1,0 };
                    rgt.x = fwd.y*world_up.z - fwd.z*world_up.y;
                    rgt.y = fwd.z*world_up.x - fwd.x*world_up.z;
                    rgt.z = fwd.x*world_up.y - fwd.y*world_up.x;
                    const float rl = std::sqrt(rgt.x*rgt.x + rgt.y*rgt.y + rgt.z*rgt.z);
                    if (rl > 1e-5F) { rgt.x/=rl; rgt.y/=rl; rgt.z/=rl; }
                    up_v.x = rgt.y*fwd.z - rgt.z*fwd.y;
                    up_v.y = rgt.z*fwd.x - rgt.x*fwd.z;
                    up_v.z = rgt.x*fwd.y - rgt.y*fwd.x;
                }
                const float tan_half_fov = std::tan(cam.fov_y * 0.5F);
                const float scale_x = aspect_pick * tan_half_fov;
                const float scale_y = tan_half_fov;
                cd::math::Vec3f ray_dir {
                    fwd.x + rgt.x * ndc_x * scale_x + up_v.x * ndc_y * scale_y,
                    fwd.y + rgt.y * ndc_x * scale_x + up_v.y * ndc_y * scale_y,
                    fwd.z + rgt.z * ndc_x * scale_x + up_v.z * ndc_y * scale_y };
                const float rdl = std::sqrt(ray_dir.x*ray_dir.x + ray_dir.y*ray_dir.y + ray_dir.z*ray_dir.z);
                if (rdl > 1e-5F) { ray_dir.x/=rdl; ray_dir.y/=rdl; ray_dir.z/=rdl; }

                // Sphere-test every entity. Radius = 0.55 (unit primitive + slack).
                float best_t = 1e30F;
                int   best_i = -1;
                for (std::size_t i = 0; i < entities.size(); ++i)
                {
                    auto* lt = scene.local(entities[i].handle);
                    if (lt == nullptr) continue;
                    const cd::math::Vec3f c { lt->value.position.x,
                                              lt->value.position.y,
                                              lt->value.position.z };
                    const cd::math::Vec3f oc {
                        cam.eye.x - c.x, cam.eye.y - c.y, cam.eye.z - c.z };
                    const float b = oc.x*ray_dir.x + oc.y*ray_dir.y + oc.z*ray_dir.z;
                    const float cc = oc.x*oc.x + oc.y*oc.y + oc.z*oc.z - 0.55F*0.55F;
                    const float disc = b*b - cc;
                    if (disc < 0.0F) continue;
                    const float t = -b - std::sqrt(disc);
                    if (t > 0.0F && t < best_t) { best_t = t; best_i = static_cast<int>(i); }
                }
                // Also try light positions (point/spot only — directional
                // has no world position, area is bigger but we use its center).
                int   best_light = -1;
                float best_light_t = best_t;
                for (std::size_t i = 0; i < lights.size(); ++i)
                {
                    const auto& Lt = lights[i].light;
                    if (Lt.type == cd::light::LightType::kDirectional) continue;
                    const cd::math::Vec3f c { Lt.position.x, Lt.position.y, Lt.position.z };
                    const cd::math::Vec3f oc {
                        cam.eye.x - c.x, cam.eye.y - c.y, cam.eye.z - c.z };
                    constexpr float kLightPickR = 0.4F;
                    const float b = oc.x*ray_dir.x + oc.y*ray_dir.y + oc.z*ray_dir.z;
                    const float cc = oc.x*oc.x + oc.y*oc.y + oc.z*oc.z - kLightPickR*kLightPickR;
                    const float disc = b*b - cc;
                    if (disc < 0.0F) continue;
                    const float t = -b - std::sqrt(disc);
                    if (t > 0.0F && t < best_light_t)
                    { best_light_t = t; best_light = static_cast<int>(i); }
                }
                if (best_light >= 0)
                {
                    selected = best_light;
                    selected_kind = SelKind::kLight;
                    log_push("[pick] selected light " + lights[static_cast<std::size_t>(best_light)].name);
                }
                else if (best_i >= 0)
                {
                    selected = best_i;
                    selected_kind = SelKind::kEntity;
                    log_push("[pick] selected " + entities[static_cast<std::size_t>(best_i)].name);
                }
                else
                {
                    // Empty-space click → unselect.
                    if (selected >= 0)
                    {
                        log_push("[pick] cleared selection");
                        selected = -1;
                    }
                }
            }
        }

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

        // ---- Faz 1.7 — per-frame TLAS rebuild ----
        // 1) tick deferred destroy queue (TLAS handles older than 3
        //    frames are guaranteed past the in-flight window),
        // 2) collect instances (ECS entities + sphere grid + floor),
        // 3) create + build the TLAS on this frame's cmd buffer,
        // 4) defer destroy of the previous frame's TLAS,
        // 5) update the prim_inst descriptor binding 2 to the new TLAS.
        while (!tlas_destroy_queue.empty() &&
               tlas_destroy_queue.front().destroy_at_frame <= frame_idx)
        {
            device.destroy_acceleration_structure(tlas_destroy_queue.front().h);
            tlas_destroy_queue.pop_front();
        }
        {
            std::vector<cd::rhi::AccelInstance> instances;
            instances.reserve(entities.size() + 25 + 1);
            auto push_inst = [&](cd::rhi::AccelStructureHandle blas,
                                 const cd::math::Mat4f& m)
            {
                if (!blas.is_valid()) return;
                cd::rhi::AccelInstance inst {};
                // 3×4 row-major transform from column-major Mat4f.
                for (std::size_t r = 0; r < 3; ++r)
                {
                    inst.transform[r*4 + 0] = m[0][r];
                    inst.transform[r*4 + 1] = m[1][r];
                    inst.transform[r*4 + 2] = m[2][r];
                    inst.transform[r*4 + 3] = m[3][r];
                }
                inst.blas = blas;
                inst.mask = 0xFFu;
                instances.push_back(inst);
            };
            for (const auto& ent : entities)
            {
                auto* lt = scene.local(ent.handle);
                if (lt == nullptr) continue;
                push_inst(blas_for_kind(ent.kind), cd::math::to_mat4(lt->value));
            }
            constexpr int kRtGS = 5;
            constexpr float kRtSp = 1.2F;
            for (int row = 0; row < kRtGS; ++row)
                for (int col = 0; col < kRtGS; ++col)
                {
                    const float x = (static_cast<float>(col) - 2.0F) * kRtSp;
                    const float y = 2.2F + (static_cast<float>(row) - 2.0F) * 0.9F;
                    const float z = -4.5F;
                    cd::math::Mat4f m = cd::math::Mat4f::identity();
                    m[3][0] = x; m[3][1] = y; m[3][2] = z;
                    push_inst(blas_sphere, m);
                }
            // Floor: identity scale, y = kFloorY (matches the floor draw).
            {
                cd::math::Mat4f fm = cd::math::Mat4f::identity();
                fm[3][1] = -0.55F;
                push_inst(blas_floor, fm);
            }
            cd::rhi::AccelStructureDesc tld {};
            tld.kind       = cd::rhi::AccelStructureKind::kTopLevel;
            tld.instances  = std::span<const cd::rhi::AccelInstance>(instances);
            tld.debug_name = "tlas_frame";
            auto new_r = device.create_acceleration_structure(tld);
            if (new_r.has_value())
            {
                cmd.build_acceleration_structure(*new_r);
                if (current_tlas.is_valid())
                    tlas_destroy_queue.push_back({ current_tlas, frame_idx + 3 });
                current_tlas = *new_r;
                std::array<cd::rhi::DescriptorWrite, 1> tlas_writes {
                    cd::rhi::DescriptorWrite {
                        .binding = 2,
                        .array_element = 0,
                        .type  = cd::rhi::DescriptorType::kAccelerationStructure,
                        .accel = current_tlas } };
                (void)prim_inst.update(tlas_writes);
            }
        }

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

        // ---- Shadow map pass (Faz 1.6 CSM) ----
        // Pick the first enabled directional light for the shadow caster.
        // No directional → shadow map is cleared to white (no shadow).
        cd::math::Vec3f csm_sun_dir { -0.4F, -0.9F, -0.2F };
        bool            csm_has_sun = false;
        for (const auto& lrow : lights)
        {
            if (!lrow.enabled) continue;
            if (lrow.light.type != cd::light::LightType::kDirectional) continue;
            csm_sun_dir = lrow.light.direction;
            csm_has_sun = true;
            break;
        }
        // Build the sun's view + ortho. Eye placed -30 m along the
        // ray, looking at origin. Up vector flips to +Z when the sun
        // is nearly vertical to avoid the look_at degeneracy.
        {
            cd::math::Vec3f sd = csm_sun_dir;
            // Normalize defensively in case the slider produced a tiny
            // vector before renormalize fired.
            const float sd_len = std::sqrt(sd.x*sd.x + sd.y*sd.y + sd.z*sd.z);
            if (sd_len > 1e-4F) { sd.x/=sd_len; sd.y/=sd_len; sd.z/=sd_len; }
            else { sd = { 0.0F, -1.0F, 0.0F }; }
            const cd::math::Vec3f eye {
                -sd.x * 30.0F, -sd.y * 30.0F, -sd.z * 30.0F };
            const cd::math::Vec3f tgt { 0.0F, 0.0F, 0.0F };
            const cd::math::Vec3f up = (std::fabs(sd.y) > 0.99F)
                ? cd::math::Vec3f{ 0.0F, 0.0F, 1.0F }
                : cd::math::Vec3f{ 0.0F, 1.0F, 0.0F };
            const auto light_view = cd::math::look_at(eye, tgt, up);
            const auto light_proj = cd::math::ortho(-25.0F, 25.0F,
                                                    -25.0F, 25.0F,
                                                    0.1F, 60.0F);
            const cd::math::Mat4f light_vp = light_proj * light_view;
            // Upload to UBO (kCpuToGpu, no staging).
            (void)device.upload_buffer(shadow_ubo, 0,
                std::span<const std::byte>(
                    reinterpret_cast<const std::byte*>(&light_vp),
                    sizeof(light_vp)));
        }

        // First-frame transition for the shadow target.
        if (!shadow_initialised_on_gpu)
        {
            std::array<cd::rhi::TextureBarrier, 1> sb {
                cd::rhi::TextureBarrier {
                    .texture = shadow_target.image,
                    .from = cd::rhi::ResourceState::kUndefined,
                    .to = cd::rhi::ResourceState::kDepthWrite,
                    .range = { .base_mip = 0, .mip_count = 1,
                               .base_layer = 0, .layer_count = 1 } } };
            cmd.barrier({}, sb);
            shadow_initialised_on_gpu = true;
        }
        else
        {
            // Subsequent frames: shader-resource → depth-write.
            std::array<cd::rhi::TextureBarrier, 1> sb {
                cd::rhi::TextureBarrier {
                    .texture = shadow_target.image,
                    .from = cd::rhi::ResourceState::kShaderResource,
                    .to = cd::rhi::ResourceState::kDepthWrite,
                    .range = { .base_mip = 0, .mip_count = 1,
                               .base_layer = 0, .layer_count = 1 } } };
            cmd.barrier({}, sb);
        }
        {
            cd::rhi::DepthStencilAttachmentInfo sda {};
            sda.view = shadow_target.view;
            sda.depth_load  = cd::rhi::LoadOp::kClear;
            sda.depth_store = cd::rhi::StoreOp::kStore;
            sda.clear.depth = 1.0F;
            cd::rhi::RenderPassBeginInfo srp {};
            srp.render_area = cd::rhi::Rect2D { {0,0}, kShadowMapSize };
            srp.color_attachments = {};
            srp.depth_stencil = &sda;
            cmd.begin_render_pass(srp);
            cmd.set_viewport(cd::rhi::Viewport {
                0.0F, 0.0F,
                static_cast<float>(kShadowMapSize.width),
                static_cast<float>(kShadowMapSize.height),
                0.0F, 1.0F });
            cmd.set_scissor(cd::rhi::Rect2D { {0,0}, kShadowMapSize });
            if (csm_has_sun)
            {
                shadow_material.apply(cmd);
                // Rebuild light_vp into a local — we already uploaded but
                // also need it as a CPU-side push for the per-caster
                // light_mvp computation. Re-derive (cheap).
                cd::math::Vec3f sd = csm_sun_dir;
                const float sl = std::sqrt(sd.x*sd.x + sd.y*sd.y + sd.z*sd.z);
                if (sl > 1e-4F) { sd.x/=sl; sd.y/=sl; sd.z/=sl; }
                else { sd = { 0.0F, -1.0F, 0.0F }; }
                const cd::math::Vec3f eye { -sd.x*30.0F, -sd.y*30.0F, -sd.z*30.0F };
                const cd::math::Vec3f tgt { 0.0F, 0.0F, 0.0F };
                const cd::math::Vec3f up = (std::fabs(sd.y) > 0.99F)
                    ? cd::math::Vec3f{ 0.0F, 0.0F, 1.0F }
                    : cd::math::Vec3f{ 0.0F, 1.0F, 0.0F };
                const auto light_view2 = cd::math::look_at(eye, tgt, up);
                const auto light_proj2 = cd::math::ortho(-25.0F, 25.0F,
                                                         -25.0F, 25.0F,
                                                         0.1F, 60.0F);
                const cd::math::Mat4f light_vp2 = light_proj2 * light_view2;
                // Casters: each ECS entity (using its mesh+transform).
                for (const auto& ent : entities)
                {
                    const auto& mesh = mesh_for(ent.kind);
                    if (!mesh.vb.is_valid()) continue;
                    auto* lt = scene.local(ent.handle);
                    if (lt == nullptr) continue;
                    cmd.bind_vertex_buffer(0, mesh.vb, 0);
                    cmd.bind_index_buffer(mesh.ib, 0, cd::rhi::IndexType::kUInt16);
                    const auto model     = cd::math::to_mat4(lt->value);
                    const auto light_mvp = light_vp2 * model;
                    cmd.push_constants(shadow_material.pipeline_layout(),
                                       cd::rhi::ShaderStage::kVertex,
                                       0, sizeof(light_mvp), &light_mvp);
                    cmd.draw_indexed(mesh.index_count, 1, 0, 0, 0);
                }
                // Casters: 5×5 PBR sphere grid (use PrimitiveVertex sphere mesh).
                cmd.bind_vertex_buffer(0, sphere_mesh.vb, 0);
                cmd.bind_index_buffer(sphere_mesh.ib, 0, cd::rhi::IndexType::kUInt16);
                constexpr int kGSh = 5;
                constexpr float kSph = 1.2F;
                for (int row = 0; row < kGSh; ++row)
                {
                    for (int col = 0; col < kGSh; ++col)
                    {
                        const float x = (static_cast<float>(col) - 2.0F) * kSph;
                        const float y = 2.2F + (static_cast<float>(row) - 2.0F) * 0.9F;
                        const float z = -4.5F;
                        cd::math::Mat4f model = cd::math::Mat4f::identity();
                        model[3][0] = x; model[3][1] = y; model[3][2] = z;
                        const auto light_mvp = light_vp2 * model;
                        cmd.push_constants(shadow_material.pipeline_layout(),
                                           cd::rhi::ShaderStage::kVertex,
                                           0, sizeof(light_mvp), &light_mvp);
                        cmd.draw_indexed(sphere_mesh.index_count, 1, 0, 0, 0);
                    }
                }
            }
            cmd.end_render_pass();
        }
        // Transition back to shader-resource for main pass sampling.
        {
            std::array<cd::rhi::TextureBarrier, 1> sb {
                cd::rhi::TextureBarrier {
                    .texture = shadow_target.image,
                    .from = cd::rhi::ResourceState::kDepthWrite,
                    .to = cd::rhi::ResourceState::kShaderResource,
                    .range = { .base_mip = 0, .mip_count = 1,
                               .base_layer = 0, .layer_count = 1 } } };
            cmd.barrier({}, sb);
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
        // Phase G — sky pulls sun direction + intensity + color from
        // the first enabled directional light. CCT slider in the
        // Lights panel now affects the SKY tint too (sunset feel at
        // 2000-3000K, neutral at D65, cold blue at 10000K).
        cd::math::Vec3f sky_sun_dir { -0.4F, -0.6F, -0.7F };
        cd::math::Vec3f sky_sun_col { 1.0F, 0.93F, 0.82F };
        float           sky_sun_strength = 0.9F;
        for (const auto& lrow : lights)
        {
            if (!lrow.enabled) continue;
            if (lrow.light.type != cd::light::LightType::kDirectional) continue;
            sky_sun_dir       = lrow.light.direction;
            sky_sun_col       = lrow.light.color;
            sky_sun_strength  = std::min(2.5F, lrow.light.intensity / 80000.0F);
            break;
        }
        spush.sun_dir[0]   = sky_sun_dir.x;
        spush.sun_dir[1]   = sky_sun_dir.y;
        spush.sun_dir[2]   = sky_sun_dir.z;
        spush.sun_dir[3]   = sky_sun_strength;
        spush.sun_color[0] = sky_sun_col.x;
        spush.sun_color[1] = sky_sun_col.y;
        spush.sun_color[2] = sky_sun_col.z;
        spush.sun_color[3] = 1.0F;  // full sky-tint blend
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
                // cd::light → render bridge: use the first enabled
                // directional light to drive the shader's key light.
                // Modulate the copper albedo by the light's color so
                // toggling the Sun OR changing CCT visibly affects
                // the spheres. (Phase 171's LitPbrMaterial does the
                // full UBO + N-light iteration; until that ships with
                // a descriptor set, this is the smallest visible
                // wiring of cd::light data into the existing shader.)
                cd::math::Vec3f light_dir { -0.4F, -0.6F, -0.7F };
                cd::math::Vec3f light_color { 1.0F, 1.0F, 1.0F };
                float           light_intensity = 0.9F;
                for (const auto& lrow : lights)
                {
                    if (!lrow.enabled) continue;
                    if (lrow.light.type != cd::light::LightType::kDirectional) continue;
                    light_dir   = lrow.light.direction;
                    light_color = lrow.light.color;  // already CCT-converted in Lights panel
                    // Map 0..200000 lux slider to ~0..2.5 shader intensity.
                    light_intensity = std::min(2.5F, lrow.light.intensity / 80000.0F);
                    break;
                }
                // Find a single warm point light, fold its color * range
                // into the ambient term (mr_amb.z) — gives the spheres a
                // visible "key + bounce" feel.
                cd::math::Vec3f point_color_contrib { 0.0F, 0.0F, 0.0F };
                for (const auto& lrow : lights)
                {
                    if (!lrow.enabled) continue;
                    if (lrow.light.type != cd::light::LightType::kPoint) continue;
                    const float k = std::min(1.0F, lrow.light.intensity / 2000.0F) * 0.15F;
                    point_color_contrib.x = lrow.light.color.x * k;
                    point_color_contrib.y = lrow.light.color.y * k;
                    point_color_contrib.z = lrow.light.color.z * k;
                    break;
                }

                cd::material::StandardPbrPush pb {};
                std::memcpy(pb.mvp, &mvp, sizeof(pb.mvp));
                // Copper base albedo, tinted by light color so CCT slider
                // produces a visible warm/cool shift on the spheres.
                pb.albedo[0] = 0.95F * (0.4F + 0.6F * light_color.x) + point_color_contrib.x;
                pb.albedo[1] = 0.64F * (0.4F + 0.6F * light_color.y) + point_color_contrib.y;
                pb.albedo[2] = 0.32F * (0.4F + 0.6F * light_color.z) + point_color_contrib.z;
                pb.albedo[3] = 1.0F;
                pb.mr_amb[0] = metallic; pb.mr_amb[1] = roughness; pb.mr_amb[2] = 0.0F; pb.mr_amb[3] = 0.0F;
                pb.camera_pos[0] = cam.eye.x; pb.camera_pos[1] = cam.eye.y; pb.camera_pos[2] = cam.eye.z; pb.camera_pos[3] = 0.0F;
                pb.light_dir[0]  = light_dir.x;
                pb.light_dir[1]  = light_dir.y;
                pb.light_dir[2]  = light_dir.z;
                pb.light_dir[3]  = light_intensity;
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
        // Per-fragment lighting now: sun + first enabled point light with
        // distance attenuation. So rotating/moving an entity (or moving
        // a light) updates its shading correctly.
        cd::math::Vec3f sun_dir { -0.4F, -0.7F, -0.6F };
        cd::math::Vec3f sun_col { 1.0F, 1.0F, 1.0F };
        float           sun_str = 0.9F;
        float           ambient_w = 0.18F;
        for (const auto& lrow : lights)
        {
            if (!lrow.enabled) continue;
            if (lrow.light.type != cd::light::LightType::kDirectional) continue;
            sun_dir = lrow.light.direction;
            sun_col = lrow.light.color;
            sun_str = std::min(2.5F, lrow.light.intensity / 80000.0F);
            break;
        }
        cd::math::Vec3f point_pos { 0,0,0 };
        cd::math::Vec3f point_col { 0,0,0 };
        float           point_range = 0.0F;
        float           point_str   = 0.0F;
        // Spot cone — xyz=direction, w=cos(outer half-angle); w<=0
        // means "point light, no cone". Read from the same lights[]
        // pass and pushed through PrimPush::spot_dir_cos.
        cd::math::Vec3f spot_dir { 0.0F, -1.0F, 0.0F };
        float           spot_cos_outer = 0.0F;
        for (const auto& lrow : lights)
        {
            if (!lrow.enabled) continue;
            if (lrow.light.type != cd::light::LightType::kPoint &&
                lrow.light.type != cd::light::LightType::kSpot) continue;
            point_pos   = lrow.light.position;
            point_col   = lrow.light.color;
            point_range = lrow.light.range;
            // Map lumens → unit-ish intensity for the shader. Frostbite
            // says I = Φ / (4π); divide further by ~5 so a 1200 lm bulb
            // at 5m matches eye expectation.
            point_str   = lrow.light.intensity / (4.0F * 3.14159265F) / 5.0F;
            if (lrow.light.type == cd::light::LightType::kSpot)
            {
                spot_dir       = lrow.light.direction;
                // cd::light::Light stores the precomputed cosines as
                // cos_outer_cone / cos_inner_cone (factories set them).
                spot_cos_outer = lrow.light.cos_outer_cone;
                // Boost spot intensity so the beam is visible — spots
                // concentrate flux into a small solid angle so the 4π
                // divide above under-reads relative to the eye.
                point_str *= 6.0F;
            }
            else
            {
                spot_cos_outer = 0.0F;  // sentinel: not a spot
            }
            break;
        }

        prim_material.apply(cmd);
        prim_inst.bind(cmd, 0);  // Faz 1.6 CSM — UBO + shadow map descriptor

        // ---- Floor (large flat quad) ----
        // Faz 1.5 — real geometry on which the planar-shadow pass can
        // project caster silhouettes. Floor sits at y = kFloorY so the
        // front-row primitives (which extend ±0.5 m around y=0) just
        // touch it.
        constexpr float kFloorY      = -0.55F;
        constexpr float kShadowLift  =  0.01F;
        {
            cmd.bind_vertex_buffer(0, floor_mesh.vb, 0);
            cmd.bind_index_buffer(floor_mesh.ib, 0, cd::rhi::IndexType::kUInt16);
            cd::math::Mat4f floor_model = cd::math::Mat4f::identity();
            floor_model[3][1] = kFloorY;  // translate quad to y = kFloorY
            const auto floor_mvp = vp * floor_model;
            PrimPush fp {};
            fp.mvp   = floor_mvp;
            fp.model = floor_model;
            // Slightly cool neutral floor — receives lighting + hemisphere AO.
            // tint[3] = 2.0 is the FS sentinel that enables the analytic
            // grid overlay (depth-tested via the floor geometry, so the
            // grid no longer shows through other objects).
            fp.tint[0] = 0.45F; fp.tint[1] = 0.46F; fp.tint[2] = 0.50F; fp.tint[3] = 2.0F;
            fp.sun_dir[0] = sun_dir.x; fp.sun_dir[1] = sun_dir.y;
            fp.sun_dir[2] = sun_dir.z; fp.sun_dir[3] = sun_str;
            fp.sun_color[0] = sun_col.x; fp.sun_color[1] = sun_col.y;
            fp.sun_color[2] = sun_col.z; fp.sun_color[3] = ambient_w;
            fp.point_pos_range[0] = point_pos.x; fp.point_pos_range[1] = point_pos.y;
            fp.point_pos_range[2] = point_pos.z; fp.point_pos_range[3] = point_range;
            fp.point_color[0] = point_col.x; fp.point_color[1] = point_col.y;
            fp.point_color[2] = point_col.z; fp.point_color[3] = point_str;
            fp.spot_dir_cos[0] = spot_dir.x; fp.spot_dir_cos[1] = spot_dir.y;
            fp.spot_dir_cos[2] = spot_dir.z; fp.spot_dir_cos[3] = spot_cos_outer;
            cmd.push_constants(prim_material.pipeline_layout(),
                               cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
                               0, sizeof(fp), &fp);
            cmd.draw_indexed(floor_mesh.index_count, 1, 0, 0, 0);
            counters.increment("draws_prim");
        }

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
            pp.model = model;
            pp.tint[0] = ent.tint.x; pp.tint[1] = ent.tint.y; pp.tint[2] = ent.tint.z; pp.tint[3] = 1.0F;
            pp.sun_dir[0] = sun_dir.x; pp.sun_dir[1] = sun_dir.y;
            pp.sun_dir[2] = sun_dir.z; pp.sun_dir[3] = sun_str;
            pp.sun_color[0] = sun_col.x; pp.sun_color[1] = sun_col.y;
            pp.sun_color[2] = sun_col.z; pp.sun_color[3] = ambient_w;
            pp.point_pos_range[0] = point_pos.x; pp.point_pos_range[1] = point_pos.y;
            pp.point_pos_range[2] = point_pos.z; pp.point_pos_range[3] = point_range;
            pp.point_color[0] = point_col.x; pp.point_color[1] = point_col.y;
            pp.point_color[2] = point_col.z; pp.point_color[3] = point_str;
            pp.spot_dir_cos[0] = spot_dir.x; pp.spot_dir_cos[1] = spot_dir.y;
            pp.spot_dir_cos[2] = spot_dir.z; pp.spot_dir_cos[3] = spot_cos_outer;
            cmd.push_constants(prim_material.pipeline_layout(),
                               cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
                               0, sizeof(pp), &pp);
            cmd.draw_indexed(mesh.index_count, 1, 0, 0, 0);
            counters.increment("draws_prim");
        }

        // ---- Planar projective shadows (Faz 1.5) ----
        // For each caster (ECS entities + 5×5 PBR sphere grid), build a
        // shadow projection matrix that flattens the geometry onto the
        // floor plane along the sun direction, then redraw with the
        // tint.w sentinel that triggers the shader's shadow-bypass
        // (flat dark output, no lighting). Hard shadows — soft shadows
        // need alpha blending in MaterialDesc (Faz 1.6 / future work).
        // Skips when sun is disabled or pointing upward.
        if (sun_str > 1e-4F && sun_dir.y < -1e-3F)
        {
            const auto S = make_planar_shadow_matrix(sun_dir, kFloorY, kShadowLift);
            PrimPush sp {};
            // Shadow tint: tint.w < 0.5 triggers shader bypass; rgb is the
            // shadow color (linear, post-tonemap output).
            sp.tint[0] = 0.04F; sp.tint[1] = 0.04F; sp.tint[2] = 0.05F; sp.tint[3] = 0.0F;
            // Zero out lighting fields — shadow path doesn't read them
            // but keep the push deterministic for SPIR-V validators.
            sp.sun_dir[0] = sp.sun_dir[1] = sp.sun_dir[2] = sp.sun_dir[3] = 0.0F;
            sp.sun_color[0] = sp.sun_color[1] = sp.sun_color[2] = sp.sun_color[3] = 0.0F;
            sp.point_pos_range[3] = 0.0F;

            // Entity casters.
            for (const auto& ent : entities)
            {
                const auto& mesh = mesh_for(ent.kind);
                if (!mesh.vb.is_valid()) continue;
                auto* lt = scene.local(ent.handle);
                if (lt == nullptr) continue;
                cmd.bind_vertex_buffer(0, mesh.vb, 0);
                cmd.bind_index_buffer(mesh.ib, 0, cd::rhi::IndexType::kUInt16);
                const auto model        = cd::math::to_mat4(lt->value);
                const auto shadow_model = S * model;
                sp.mvp   = vp * shadow_model;
                sp.model = shadow_model;
                cmd.push_constants(prim_material.pipeline_layout(),
                                   cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
                                   0, sizeof(sp), &sp);
                cmd.draw_indexed(mesh.index_count, 1, 0, 0, 0);
                counters.increment("draws_shadow");
            }

            // PBR sphere grid casters (use the PrimitiveVertex sphere
            // mesh — same shape, different vertex format. The prim
            // shader expects PrimitiveVertex, so we bind sphere_mesh
            // not pbr_sphere even though the spheres are PBR-rendered.)
            cmd.bind_vertex_buffer(0, sphere_mesh.vb, 0);
            cmd.bind_index_buffer(sphere_mesh.ib, 0, cd::rhi::IndexType::kUInt16);
            constexpr int kGS = 5;
            constexpr float kSp = 1.2F;
            for (int row = 0; row < kGS; ++row)
            {
                for (int col = 0; col < kGS; ++col)
                {
                    const float x = (static_cast<float>(col) - 2.0F) * kSp;
                    const float y = 2.2F + (static_cast<float>(row) - 2.0F) * 0.9F;
                    const float z = -4.5F;
                    cd::math::Mat4f model = cd::math::Mat4f::identity();
                    model[3][0] = x; model[3][1] = y; model[3][2] = z;
                    const auto shadow_model = S * model;
                    sp.mvp   = vp * shadow_model;
                    sp.model = shadow_model;
                    cmd.push_constants(prim_material.pipeline_layout(),
                                       cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
                                       0, sizeof(sp), &sp);
                    cmd.draw_indexed(sphere_mesh.index_count, 1, 0, 0, 0);
                    counters.increment("draws_shadow");
                }
            }
        }

        // ---- ImGui frame ----
        ctx.new_frame();

        // Palette hotkeys via ImGui (after new_frame so IO modifier
        // state is current). Multiple combos because user reported
        // Ctrl+Shift+P sometimes not firing — IME / global keyboard
        // hooks can intercept the chord. F2 + GraveAccent + the chord
        // all toggle, any one works.
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_P) ||
            ImGui::IsKeyChordPressed(ImGuiKey_F2) ||
            ImGui::IsKeyChordPressed(ImGuiKey_GraveAccent))
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
                ImGui::DockBuilderDockWindow("Streamer",  dock_bot);
                ImGui::DockBuilderDockWindow("Lights",    dock_right);
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

        // ---- Streamer (Phase 150) ----
        ImGui::Begin("Streamer");
        ImGui::Text("worker: %s   pending %zu",
                    streamer.is_running() ? "RUNNING" : "STOPPED",
                    streamer.pending_count());
        ImGui::Text("completed %u   failed %u",
                    streamer_completed.load(std::memory_order_relaxed),
                    streamer_failed.load(std::memory_order_relaxed));
        ImGui::Separator();
        if (ImGui::Button("Enqueue (low prio)"))   streamer_enqueue(0);
        ImGui::SameLine();
        if (ImGui::Button("Enqueue (high prio)"))  streamer_enqueue(100);
        ImGui::SameLine();
        if (ImGui::Button("Enqueue 8 burst"))      { for (int i=0;i<8;++i) streamer_enqueue(i*10); }
        ImGui::Separator();
        // Recent-tracked rows: id, state.
        for (auto it = streamer_tracked.rbegin(); it != streamer_tracked.rend(); ++it)
        {
            const auto st = streamer.state_of(*it);
            const char* lbl =
                st == cd::asset::StreamState::kComplete ? "COMPLETE"
              : st == cd::asset::StreamState::kInflight ? "INFLIGHT"
              : st == cd::asset::StreamState::kFailed   ? "FAILED"
              :                                           "PENDING";
            const ImVec4 col =
                st == cd::asset::StreamState::kComplete ? ImVec4(0.4F,1.0F,0.4F,1) :
                st == cd::asset::StreamState::kInflight ? ImVec4(1.0F,0.85F,0.3F,1) :
                st == cd::asset::StreamState::kFailed   ? ImVec4(1.0F,0.4F,0.4F,1) :
                                                          ImVec4(0.7F,0.7F,0.7F,1);
            ImGui::TextColored(col, "id %llu  %s",
                               static_cast<unsigned long long>(it->value()), lbl);
        }
        ImGui::End();

        // ---- Lights (Phase 171/172 — cd::light system) ----
        ImGui::Begin("Lights");
        ImGui::TextDisabled("cd::light — Frostbite + Filament model");
        ImGui::Separator();

        // Per-frame: refresh CCT→RGB, then assign every enabled light
        // into the cluster grid for the stats line.
        cluster_grid.clear();
        std::uint32_t enabled_count = 0;
        std::uint32_t cluster_hits  = 0;
        for (std::size_t i = 0; i < lights.size(); ++i)
        {
            auto& row = lights[i];
            if (row.kelvin > 0.0F)
                row.light.color = cd::light::cct_to_linear_rgb(row.kelvin);
            if (row.enabled)
            {
                ++enabled_count;
                cluster_hits += cluster_grid.assign(static_cast<std::uint32_t>(i),
                                                    row.light, row.light.position);
            }
        }

        ImGui::Text("enabled %u / %zu     cluster assignments %u",
                    enabled_count, lights.size(), cluster_hits);
        ImGui::Text("grid: %ux%ux%u  near %.1f  far %.1f",
                    cluster_desc.tiles_x, cluster_desc.tiles_y, cluster_desc.slices_z,
                    static_cast<double>(cluster_desc.near_z),
                    static_cast<double>(cluster_desc.far_z));
        ImGui::Separator();

        for (std::size_t i = 0; i < lights.size(); ++i)
        {
            auto& row = lights[i];
            ImGui::PushID(static_cast<int>(i));
            // Click on row name selects the light (so Inspector + gizmo see it).
            const bool row_sel = (selected_kind == SelKind::kLight && selected == static_cast<int>(i));
            if (row_sel)
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0F, 0.85F, 0.0F, 1.0F));
            if (ImGui::Selectable((row_sel ? std::string { "> " } + row.name : row.name).c_str(),
                                  row_sel, ImGuiSelectableFlags_AllowOverlap))
            {
                selected = static_cast<int>(i);
                selected_kind = SelKind::kLight;
            }
            if (row_sel) ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::Checkbox("##en", &row.enabled);

            // Color preview swatch — what the CCT actually produces.
            const ImVec4 col {
                row.light.color.x, row.light.color.y, row.light.color.z, 1.0F
            };
            ImGui::SameLine();
            ImGui::ColorButton("##swatch", col,
                               ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker,
                               ImVec2(24, 14));

            // Type badge.
            const char* type_str =
                row.light.type == cd::light::LightType::kDirectional ? "DIR " :
                row.light.type == cd::light::LightType::kPoint       ? "POINT" :
                row.light.type == cd::light::LightType::kSpot        ? "SPOT" :
                row.light.type == cd::light::LightType::kRectArea    ? "RECT" :
                                                                       "DISK";
            ImGui::SameLine();
            ImGui::TextDisabled("[%s]", type_str);

            // CCT + intensity sliders.
            ImGui::SliderFloat("CCT (K)", &row.kelvin, 1000.0F, 15000.0F, "%.0f K");
            const char* unit =
                row.light.type == cd::light::LightType::kDirectional ? "lx" : "lm";
            ImGui::SliderFloat("intensity", &row.light.intensity,
                               0.0F, 200000.0F, ("%.0f " + std::string { unit }).c_str());
            if (row.light.type == cd::light::LightType::kPoint ||
                row.light.type == cd::light::LightType::kSpot)
            {
                ImGui::SliderFloat("range", &row.light.range, 0.5F, 50.0F, "%.1f m");

                // Live attenuation preview at 1m, 5m, range/2.
                const float a1 = cd::light::distance_attenuation(1.0F, row.light.range);
                const float a5 = cd::light::distance_attenuation(5.0F, row.light.range);
                const float ah = cd::light::distance_attenuation(row.light.range * 0.5F, row.light.range);
                ImGui::TextDisabled("atten 1m=%.3f  5m=%.4f  r/2=%.3f",
                    static_cast<double>(a1), static_cast<double>(a5), static_cast<double>(ah));
            }
            // Direction control for any light type that has a meaningful
            // forward axis (everything except omnidirectional point). User
            // feedback: "isiklara yun veremiyorum" — give them a slider.
            // Sliders are raw xyz in [-1, 1]; renormalized after edit so
            // |dir| == 1 holds for the shading + shadow code that reads it.
            if (row.light.type == cd::light::LightType::kDirectional ||
                row.light.type == cd::light::LightType::kSpot        ||
                row.light.type == cd::light::LightType::kRectArea    ||
                row.light.type == cd::light::LightType::kDiskArea)
            {
                float dir[3] {
                    row.light.direction.x,
                    row.light.direction.y,
                    row.light.direction.z };
                if (ImGui::SliderFloat3("dir xyz", dir, -1.0F, 1.0F, "%.2f"))
                {
                    const float L = std::sqrt(dir[0]*dir[0] +
                                              dir[1]*dir[1] +
                                              dir[2]*dir[2]);
                    if (L > 1e-4F)
                    {
                        row.light.direction.x = dir[0] / L;
                        row.light.direction.y = dir[1] / L;
                        row.light.direction.z = dir[2] / L;
                    }
                }
                if (row.light.type == cd::light::LightType::kDirectional)
                {
                    ImGui::TextDisabled("sun pointing %s",
                        row.light.direction.y < 0.0F
                            ? "DOWN (casts shadow)"
                            : "UP (no shadow)");
                }
            }
            ImGui::PopID();
            if (i + 1 < lights.size()) ImGui::Separator();
        }
        ImGui::End();

        // ---- History ----
        ImGui::Begin("History");
        ImGui::Text("undo depth %zu  redo depth %zu  (bytes %zu)",
                    history.undo_depth(), history.redo_depth(), history.bytes_in_use());
        ImGui::Separator();
        for (auto it = log.rbegin(); it != log.rend(); ++it)
            ImGui::TextUnformatted(it->c_str());
        ImGui::End();

        // ---- Phase 151 — selection outline (ImGui overlay) ----
        // We use the kWireframe style: project the selected entity's
        // world position onto the screen, then draw a circle around it
        // via ImGui's foreground draw list. Cheap, no extra GPU pass,
        // and demonstrates SelectionOutline state end-to-end.
        if (selected_kind == SelKind::kEntity &&
            selected >= 0 && selected < static_cast<int>(entities.size()))
        {
            outline.set(entities[static_cast<std::size_t>(selected)].handle);
        }
        else
        {
            outline.clear();
        }
        outline.clamp_params();
        if (outline.style != cd::editor::OutlineStyle::kNone && !outline.empty())
        {
            const float vw = static_cast<float>(frame.extent.width);
            const float vh = static_cast<float>(frame.extent.height);
            auto* dl = ImGui::GetForegroundDrawList();
            const ImU32 col = ImGui::ColorConvertFloat4ToU32(
                ImVec4(outline.color.x, outline.color.y, outline.color.z, outline.opacity));
            for (const auto& e : outline.entities())
            {
                auto* lt = scene.local(e);
                if (lt == nullptr) continue;
                const auto& p = lt->value.position;
                // Project world → NDC → pixel.
                const cd::math::Vec4f wp { p.x, p.y, p.z, 1.0F };
                cd::math::Vec4f clip {};
                for (std::size_t r = 0; r < 4; ++r)
                {
                    clip[r] = vp[0][r]*wp[0] + vp[1][r]*wp[1] + vp[2][r]*wp[2] + vp[3][r]*wp[3];
                }
                if (clip[3] <= 0.0F) continue;  // behind camera
                const float ndc_x = clip[0] / clip[3];
                const float ndc_y = clip[1] / clip[3];
                const float sx = (ndc_x * 0.5F + 0.5F) * vw;
                const float sy = (1.0F - (ndc_y * 0.5F + 0.5F)) * vh;
                // Radius shrinks with distance.
                const float radius = std::max(8.0F, 60.0F / std::max(0.5F, clip[3] * 0.25F));
                dl->AddCircle(ImVec2(sx, sy), radius, col, 32, outline.thickness * 1.5F);
                // Crosshair tick marks for emphasis.
                dl->AddLine(ImVec2(sx - radius - 6.0F, sy),
                            ImVec2(sx - radius + 6.0F, sy), col, outline.thickness);
                dl->AddLine(ImVec2(sx + radius - 6.0F, sy),
                            ImVec2(sx + radius + 6.0F, sy), col, outline.thickness);
                dl->AddLine(ImVec2(sx, sy - radius - 6.0F),
                            ImVec2(sx, sy - radius + 6.0F), col, outline.thickness);
                dl->AddLine(ImVec2(sx, sy + radius - 6.0F),
                            ImVec2(sx, sy + radius + 6.0F), col, outline.thickness);
            }
        }

        // ---- World grid (floor) ----
        // Moved into the floor fragment shader (analytic XZ grid with
        // fwidth-based line width). That respects the depth buffer so
        // the grid no longer shows through entities — user-flagged
        // "grid objeler arasindan gozukmemeli". The floor mesh draw
        // above sets tint[3] = 2.0 to enable that shader branch.

        // ---- Phase D — Light source markers (world-space overlay) ----
        // Each enabled light gets a small visual in the viewport so
        // the user can SEE where the lights are placed.
        // - Directional: a yellow line from sky toward target (sun ray)
        // - Point: filled circle in light color + range ring
        // - Spot:  filled circle at apex + cone wireframe (4 lines to far disk)
        // - Rect area: 4 corners outlined in light color
        {
            const float vw = static_cast<float>(frame.extent.width);
            const float vh = static_cast<float>(frame.extent.height);
            auto* dl_m = ImGui::GetForegroundDrawList();
            auto project = [&](const cd::math::Vec3f& p) -> ImVec2 {
                const cd::math::Vec4f wp { p.x, p.y, p.z, 1.0F };
                cd::math::Vec4f c {};
                for (std::size_t r = 0; r < 4; ++r)
                    c[r] = vp[0][r]*wp[0] + vp[1][r]*wp[1] + vp[2][r]*wp[2] + vp[3][r]*wp[3];
                if (c[3] <= 0.0F) return ImVec2(-1.0F, -1.0F);
                return ImVec2(
                    (c[0] / c[3] * 0.5F + 0.5F) * vw,
                    (1.0F - (c[1] / c[3] * 0.5F + 0.5F)) * vh);
            };
            for (std::size_t li = 0; li < lights.size(); ++li)
            {
                const auto& lrow = lights[li];
                if (!lrow.enabled) continue;
                const auto& L = lrow.light;
                const bool sel = (selected_kind == SelKind::kLight && selected == static_cast<int>(li));
                const ImU32 col = ImGui::ColorConvertFloat4ToU32(
                    ImVec4(L.color.x, L.color.y, L.color.z, 1.0F));
                const ImU32 col_dim = ImGui::ColorConvertFloat4ToU32(
                    ImVec4(L.color.x * 0.6F, L.color.y * 0.6F, L.color.z * 0.6F, 0.7F));
                const ImU32 col_sel = ImGui::ColorConvertFloat4ToU32(
                    ImVec4(1.0F, 0.85F, 0.0F, 1.0F));  // golden hover-style for selected
                switch (L.type)
                {
                    case cd::light::LightType::kDirectional:
                    {
                        // Render an arrow from sky position toward scene center.
                        cd::math::Vec3f sky_origin {
                            -L.direction.x * 15.0F,
                            -L.direction.y * 15.0F,
                            -L.direction.z * 15.0F };
                        cd::math::Vec3f tip {
                            sky_origin.x + L.direction.x * 8.0F,
                            sky_origin.y + L.direction.y * 8.0F,
                            sky_origin.z + L.direction.z * 8.0F };
                        const auto p0 = project(sky_origin);
                        const auto p1 = project(tip);
                        if (p0.x >= 0.0F && p1.x >= 0.0F)
                        {
                            dl_m->AddLine(p0, p1, col, 3.0F);
                            dl_m->AddCircleFilled(p0, 8.0F, col);
                            if (sel) dl_m->AddCircle(p0, 16.0F, col_sel, 16, 3.0F);
                            dl_m->AddText(ImVec2(p0.x + 10.0F, p0.y - 8.0F),
                                          col, "SUN");
                        }
                        break;
                    }
                    case cd::light::LightType::kPoint:
                    {
                        const auto p = project(L.position);
                        if (p.x >= 0.0F)
                        {
                            dl_m->AddCircleFilled(p, 10.0F, col);
                            dl_m->AddCircle(p, 14.0F, col_dim, 12, 2.0F);
                            if (sel) dl_m->AddCircle(p, 18.0F, col_sel, 16, 3.0F);
                            // Render an approximate range ring by projecting 8
                            // points on the world-space circle at light.range.
                            for (int i = 0; i < 16; ++i)
                            {
                                const float t0 = static_cast<float>(i)     / 16.0F * 6.2831853F;
                                const float t1 = static_cast<float>(i + 1) / 16.0F * 6.2831853F;
                                cd::math::Vec3f a {
                                    L.position.x + std::cos(t0) * L.range,
                                    L.position.y,
                                    L.position.z + std::sin(t0) * L.range };
                                cd::math::Vec3f b {
                                    L.position.x + std::cos(t1) * L.range,
                                    L.position.y,
                                    L.position.z + std::sin(t1) * L.range };
                                const auto pa = project(a);
                                const auto pb = project(b);
                                if (pa.x >= 0.0F && pb.x >= 0.0F)
                                    dl_m->AddLine(pa, pb, col_dim, 1.5F);
                            }
                            dl_m->AddText(ImVec2(p.x + 14.0F, p.y - 8.0F),
                                          col, "POINT");
                        }
                        break;
                    }
                    case cd::light::LightType::kSpot:
                    {
                        const auto p_apex = project(L.position);
                        // Far disk at range along direction.
                        cd::math::Vec3f far_center {
                            L.position.x + L.direction.x * L.range,
                            L.position.y + L.direction.y * L.range,
                            L.position.z + L.direction.z * L.range };
                        // Use a tangent basis on the cone axis.
                        cd::math::Vec3f up { 0,1,0 };
                        if (std::abs(L.direction.y) > 0.95F) up = { 1,0,0 };
                        cd::math::Vec3f rgt {
                            L.direction.y*up.z - L.direction.z*up.y,
                            L.direction.z*up.x - L.direction.x*up.z,
                            L.direction.x*up.y - L.direction.y*up.x };
                        const float rgt_len = std::sqrt(rgt.x*rgt.x + rgt.y*rgt.y + rgt.z*rgt.z);
                        if (rgt_len > 1e-5F) { rgt.x/=rgt_len; rgt.y/=rgt_len; rgt.z/=rgt_len; }
                        cd::math::Vec3f bt {
                            L.direction.y*rgt.z - L.direction.z*rgt.y,
                            L.direction.z*rgt.x - L.direction.x*rgt.z,
                            L.direction.x*rgt.y - L.direction.y*rgt.x };
                        // outer cone half-angle from cos_outer
                        const float outer_angle = std::acos(std::clamp(L.cos_outer_cone, -1.0F, 1.0F));
                        const float disk_r = L.range * std::tan(outer_angle);
                        // 4 cone "edges"
                        for (int i = 0; i < 8; ++i)
                        {
                            const float t = static_cast<float>(i) / 8.0F * 6.2831853F;
                            const float ct = std::cos(t), st = std::sin(t);
                            cd::math::Vec3f edge {
                                far_center.x + (rgt.x * ct + bt.x * st) * disk_r,
                                far_center.y + (rgt.y * ct + bt.y * st) * disk_r,
                                far_center.z + (rgt.z * ct + bt.z * st) * disk_r };
                            const auto pe = project(edge);
                            if (p_apex.x >= 0.0F && pe.x >= 0.0F)
                                dl_m->AddLine(p_apex, pe, col_dim, 1.5F);
                        }
                        if (p_apex.x >= 0.0F)
                        {
                            dl_m->AddCircleFilled(p_apex, 8.0F, col);
                            if (sel) dl_m->AddCircle(p_apex, 16.0F, col_sel, 16, 3.0F);
                            dl_m->AddText(ImVec2(p_apex.x + 10.0F, p_apex.y - 8.0F),
                                          col, "SPOT");
                        }
                        break;
                    }
                    case cd::light::LightType::kRectArea:
                    case cd::light::LightType::kDiskArea:
                    {
                        // Project 4 corners.
                        const float hw = L.area_width * 0.5F, hh = L.area_height * 0.5F;
                        cd::math::Vec3f c0 {
                            L.position.x - L.area_tangent.x * hw - L.area_bitangent.x * hh,
                            L.position.y - L.area_tangent.y * hw - L.area_bitangent.y * hh,
                            L.position.z - L.area_tangent.z * hw - L.area_bitangent.z * hh };
                        cd::math::Vec3f c1 {
                            L.position.x + L.area_tangent.x * hw - L.area_bitangent.x * hh,
                            L.position.y + L.area_tangent.y * hw - L.area_bitangent.y * hh,
                            L.position.z + L.area_tangent.z * hw - L.area_bitangent.z * hh };
                        cd::math::Vec3f c2 {
                            L.position.x + L.area_tangent.x * hw + L.area_bitangent.x * hh,
                            L.position.y + L.area_tangent.y * hw + L.area_bitangent.y * hh,
                            L.position.z + L.area_tangent.z * hw + L.area_bitangent.z * hh };
                        cd::math::Vec3f c3 {
                            L.position.x - L.area_tangent.x * hw + L.area_bitangent.x * hh,
                            L.position.y - L.area_tangent.y * hw + L.area_bitangent.y * hh,
                            L.position.z - L.area_tangent.z * hw + L.area_bitangent.z * hh };
                        const auto p0 = project(c0);
                        const auto p1 = project(c1);
                        const auto p2 = project(c2);
                        const auto p3 = project(c3);
                        if (p0.x >= 0.0F && p1.x >= 0.0F && p2.x >= 0.0F && p3.x >= 0.0F)
                        {
                            const float thickness = sel ? 4.0F : 2.0F;
                            const ImU32  use_col   = sel ? col_sel : col;
                            dl_m->AddLine(p0, p1, use_col, thickness);
                            dl_m->AddLine(p1, p2, use_col, thickness);
                            dl_m->AddLine(p2, p3, use_col, thickness);
                            dl_m->AddLine(p3, p0, use_col, thickness);
                            dl_m->AddText(p0, col, "AREA");
                        }
                        break;
                    }
                }
            }
        }

        // ---- Phase 152 — axis-translation gizmo (ImGui overlay) ----
        // Project the selected entity's world position to screen,
        // draw three colored axis arrows, do hover/click drag in
        // screen-space, map back into world delta along the active
        // axis, and push a TranslateCommand on release.
        // Gizmo target can be either an entity transform OR a light's
        // position. The lambda below makes the same draw + drag code
        // path applicable to both — point/spot/area lights drag their
        // position; directional lights have no world position so they
        // skip the gizmo.
        auto gizmo_target_pos = [&]() -> cd::math::Vec3f* {
            if (selected < 0) return nullptr;
            if (selected_kind == SelKind::kEntity)
            {
                if (selected >= static_cast<int>(entities.size())) return nullptr;
                if (auto* lt = scene.local(entities[static_cast<std::size_t>(selected)].handle))
                    return &lt->value.position;
                return nullptr;
            }
            if (selected_kind == SelKind::kLight)
            {
                if (selected >= static_cast<int>(lights.size())) return nullptr;
                auto& L = lights[static_cast<std::size_t>(selected)].light;
                if (L.type == cd::light::LightType::kDirectional) return nullptr;
                return &L.position;
            }
            return nullptr;
        };

        if (!gizmo_visible || gizmo_target_pos() == nullptr)
        {
            gizmo_was_hovered = false;
        }
        if (gizmo_visible && gizmo_target_pos() != nullptr)
        {
            cd::math::Vec3f* target_pos = gizmo_target_pos();
            // For entity targets, also need transform record for full
            // rotate/scale ops; for light targets, only position drag.
            const bool target_is_entity = (selected_kind == SelKind::kEntity);
            cd::ecs::Entity sel_ent = target_is_entity
                ? entities[static_cast<std::size_t>(selected)].handle
                : cd::ecs::Entity {};
            cd::scene::LocalTransform* lt = target_is_entity
                ? scene.local(sel_ent) : nullptr;
            if (target_pos != nullptr)
            {
                gizmo.set_target(*target_pos);
                const float vw = static_cast<float>(frame.extent.width);
                const float vh = static_cast<float>(frame.extent.height);
                auto project = [&](const cd::math::Vec3f& p) -> ImVec2 {
                    const cd::math::Vec4f wp { p.x, p.y, p.z, 1.0F };
                    cd::math::Vec4f c {};
                    for (std::size_t r = 0; r < 4; ++r)
                        c[r] = vp[0][r]*wp[0] + vp[1][r]*wp[1] + vp[2][r]*wp[2] + vp[3][r]*wp[3];
                    if (c[3] <= 0.0F) return ImVec2(-1.0F, -1.0F);
                    return ImVec2(
                        (c[0] / c[3] * 0.5F + 0.5F) * vw,
                        (1.0F - (c[1] / c[3] * 0.5F + 0.5F)) * vh);
                };
                const auto& tgt = gizmo.target();
                constexpr float kAxisLen = 1.5F;
                const ImVec2 p_org = project(tgt);
                const ImVec2 p_x = project({ tgt.x + kAxisLen, tgt.y, tgt.z });
                const ImVec2 p_y = project({ tgt.x, tgt.y + kAxisLen, tgt.z });
                const ImVec2 p_z = project({ tgt.x, tgt.y, tgt.z + kAxisLen });

                if (p_org.x >= 0.0F)
                {
                    auto* dl = ImGui::GetForegroundDrawList();
                    auto axis_color_imgui = [](cd::editor::GizmoAxis a) {
                        const auto c = cd::editor::axis_color(a);
                        return ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, 1.0F));
                    };
                    const ImU32 cx = axis_color_imgui(cd::editor::GizmoAxis::kX);
                    const ImU32 cy = axis_color_imgui(cd::editor::GizmoAxis::kY);
                    const ImU32 cz = axis_color_imgui(cd::editor::GizmoAxis::kZ);

                    auto thick = [&](cd::editor::GizmoAxis a) -> float {
                        return (gizmo.hover() == a || gizmo.active_axis() == a) ? 5.0F : 3.0F;
                    };

                    dl->AddLine(p_org, p_x, cx, thick(cd::editor::GizmoAxis::kX));
                    dl->AddLine(p_org, p_y, cy, thick(cd::editor::GizmoAxis::kY));
                    dl->AddLine(p_org, p_z, cz, thick(cd::editor::GizmoAxis::kZ));
                    // Arrowheads (filled triangles).
                    auto arrowhead = [&](ImVec2 from, ImVec2 to, ImU32 col) {
                        const float dx = to.x - from.x, dy = to.y - from.y;
                        const float len = std::sqrt(dx*dx + dy*dy);
                        if (len < 1e-3F) return;
                        const float nx = dx / len, ny = dy / len;
                        const float sx = -ny, sy = nx;
                        constexpr float kHead = 10.0F;
                        const ImVec2 a = to;
                        const ImVec2 b { to.x - nx * kHead + sx * 5.0F, to.y - ny * kHead + sy * 5.0F };
                        const ImVec2 c { to.x - nx * kHead - sx * 5.0F, to.y - ny * kHead - sy * 5.0F };
                        dl->AddTriangleFilled(a, b, c, col);
                    };
                    // Mode-specific tip decoration:
                    //   translate → arrowheads
                    //   rotate    → small circles at tips
                    //   scale     → small filled cubes at tips
                    if (gizmo_mode == GizmoMode::kTranslate)
                    {
                        arrowhead(p_org, p_x, cx);
                        arrowhead(p_org, p_y, cy);
                        arrowhead(p_org, p_z, cz);
                    }
                    else if (gizmo_mode == GizmoMode::kRotate)
                    {
                        // Draw the standard 3 rotation rings on each
                        // world-axis plane. Each ring is the projection
                        // of a unit-radius circle (scaled by kAxisLen)
                        // in the plane perpendicular to its color axis.
                        constexpr int   kRingSeg = 48;
                        constexpr float kRingRad = 1.5F;
                        auto draw_ring = [&](cd::math::Vec3f u, cd::math::Vec3f v,
                                             ImU32 c, float t)
                        {
                            for (int i = 0; i < kRingSeg; ++i)
                            {
                                const float a = static_cast<float>(i)     / kRingSeg * 6.2831853F;
                                const float b = static_cast<float>(i + 1) / kRingSeg * 6.2831853F;
                                const float ca0 = std::cos(a), sa0 = std::sin(a);
                                const float cb0 = std::cos(b), sb0 = std::sin(b);
                                cd::math::Vec3f wa {
                                    tgt.x + (u.x * ca0 + v.x * sa0) * kRingRad,
                                    tgt.y + (u.y * ca0 + v.y * sa0) * kRingRad,
                                    tgt.z + (u.z * ca0 + v.z * sa0) * kRingRad };
                                cd::math::Vec3f wb {
                                    tgt.x + (u.x * cb0 + v.x * sb0) * kRingRad,
                                    tgt.y + (u.y * cb0 + v.y * sb0) * kRingRad,
                                    tgt.z + (u.z * cb0 + v.z * sb0) * kRingRad };
                                const auto pa = project(wa);
                                const auto pb = project(wb);
                                if (pa.x >= 0.0F && pb.x >= 0.0F)
                                    dl->AddLine(pa, pb, c, t);
                            }
                        };
                        const float th_x = (gizmo.hover() == cd::editor::GizmoAxis::kX) ? 4.0F : 2.0F;
                        const float th_y = (gizmo.hover() == cd::editor::GizmoAxis::kY) ? 4.0F : 2.0F;
                        const float th_z = (gizmo.hover() == cd::editor::GizmoAxis::kZ) ? 4.0F : 2.0F;
                        // Ring around X axis lives in (Y, Z) plane.
                        draw_ring({0,1,0}, {0,0,1}, cx, th_x);
                        // Ring around Y axis lives in (X, Z) plane.
                        draw_ring({1,0,0}, {0,0,1}, cy, th_y);
                        // Ring around Z axis lives in (X, Y) plane.
                        draw_ring({1,0,0}, {0,1,0}, cz, th_z);
                    }
                    else  // kScale
                    {
                        const auto cube_at = [&](ImVec2 c, ImU32 col) {
                            const ImVec2 a { c.x - 5, c.y - 5 };
                            const ImVec2 b { c.x + 5, c.y + 5 };
                            dl->AddRectFilled(a, b, col);
                        };
                        cube_at(p_x, cx);
                        cube_at(p_y, cy);
                        cube_at(p_z, cz);
                    }
                    // Mode label.
                    const char* mode_lbl =
                        gizmo_mode == GizmoMode::kTranslate ? "T" :
                        gizmo_mode == GizmoMode::kRotate    ? "R" : "S";
                    dl->AddText(ImVec2(p_org.x + 8, p_org.y + 8),
                                ImGui::ColorConvertFloat4ToU32(ImVec4(1,1,1,0.9F)),
                                mode_lbl);

                    // Hover test. Translate/Scale modes measure mouse-to-
                    // axis-line distance (arrows). Rotate mode measures
                    // mouse-to-ring polyline distance (so the user grabs a
                    // ring, not an arrow — feedback "rotation islemini
                    // yeni koydugun cemberler userinden yapabilmek
                    // istiyorum").
                    const ImVec2 mp = ImGui::GetIO().MousePos;
                    auto dist_to_seg = [](ImVec2 a, ImVec2 b, ImVec2 p) {
                        const float dx = b.x - a.x, dy = b.y - a.y;
                        const float L2 = dx*dx + dy*dy;
                        if (L2 < 1e-4F) return std::sqrt((p.x-a.x)*(p.x-a.x) + (p.y-a.y)*(p.y-a.y));
                        const float t = std::clamp(((p.x-a.x)*dx + (p.y-a.y)*dy) / L2, 0.0F, 1.0F);
                        const float qx = a.x + t * dx, qy = a.y + t * dy;
                        return std::sqrt((p.x-qx)*(p.x-qx) + (p.y-qy)*(p.y-qy));
                    };
                    cd::editor::GizmoAxis best = cd::editor::GizmoAxis::kNone;
                    float best_d = gizmo.hover_tolerance_pixels;
                    if (gizmo_mode == GizmoMode::kRotate)
                    {
                        // Sample each ring at the same resolution we draw
                        // it (48 segments); compute min distance from
                        // mouse to the ring polyline. Cheap (3 × 48 = 144
                        // segments per frame at hover-test time).
                        constexpr int   kHoverSeg = 48;
                        constexpr float kHoverRad = 1.5F;  // matches kRingRad above
                        auto ring_dist = [&](cd::math::Vec3f u,
                                             cd::math::Vec3f v) -> float
                        {
                            float min_d = std::numeric_limits<float>::infinity();
                            ImVec2 prev {};
                            bool prev_ok = false;
                            for (int i = 0; i <= kHoverSeg; ++i)
                            {
                                const float a = static_cast<float>(i) / kHoverSeg * 6.2831853F;
                                const float ca = std::cos(a), sa = std::sin(a);
                                const cd::math::Vec3f w {
                                    tgt.x + (u.x * ca + v.x * sa) * kHoverRad,
                                    tgt.y + (u.y * ca + v.y * sa) * kHoverRad,
                                    tgt.z + (u.z * ca + v.z * sa) * kHoverRad };
                                const auto pw = project(w);
                                if (pw.x >= 0.0F)
                                {
                                    if (prev_ok)
                                    {
                                        const float d = dist_to_seg(prev, pw, mp);
                                        if (d < min_d) min_d = d;
                                    }
                                    prev = pw;
                                    prev_ok = true;
                                }
                                else
                                {
                                    prev_ok = false;
                                }
                            }
                            return min_d;
                        };
                        const float dx = ring_dist({0,1,0}, {0,0,1});  // X-axis ring lives in YZ
                        const float dy = ring_dist({1,0,0}, {0,0,1});  // Y-axis ring lives in XZ
                        const float dz = ring_dist({1,0,0}, {0,1,0});  // Z-axis ring lives in XY
                        if (dx < best_d) { best_d = dx; best = cd::editor::GizmoAxis::kX; }
                        if (dy < best_d) { best_d = dy; best = cd::editor::GizmoAxis::kY; }
                        if (dz < best_d) { best_d = dz; best = cd::editor::GizmoAxis::kZ; }
                    }
                    else  // translate / scale — axis-arrow hover
                    {
                        if (auto d = dist_to_seg(p_org, p_x, mp); d < best_d) { best_d = d; best = cd::editor::GizmoAxis::kX; }
                        if (auto d = dist_to_seg(p_org, p_y, mp); d < best_d) { best_d = d; best = cd::editor::GizmoAxis::kY; }
                        if (auto d = dist_to_seg(p_org, p_z, mp); d < best_d) { best_d = d; best = cd::editor::GizmoAxis::kZ; }
                    }
                    gizmo.set_hover(best);
                    gizmo_was_hovered = (best != cd::editor::GizmoAxis::kNone);

                    const bool over_imgui_ui = ImGui::GetIO().WantCaptureMouse &&
                                                ImGui::IsAnyItemHovered();
                    // If the mouse is hovering an axis arrow AND a left-
                    // click is pending from the OS event loop, the gizmo
                    // wins over the 3D pick path — suppress the pick.
                    if (pending_pick && best != cd::editor::GizmoAxis::kNone)
                    {
                        pending_pick = false;
                    }
                    if (!gizmo.is_dragging() && best != cd::editor::GizmoAxis::kNone &&
                        ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !over_imgui_ui)
                    {
                        gizmo.begin_drag(best, *target_pos);
                        gizmo_drag_anchor      = mp;
                        gizmo_drag_world_start = *target_pos;
                        if (lt != nullptr)
                        {
                            gizmo_drag_scale_start = lt->value.scale;
                            gizmo_drag_rot_start   = lt->value.rotation;
                        }
                    }
                    if (gizmo.is_dragging())
                    {
                        // Screen-space delta along the projected axis,
                        // mapped to world delta by pixels_per_world_unit.
                        ImVec2 axis_screen_end = p_x;
                        if (gizmo.active_axis() == cd::editor::GizmoAxis::kY) axis_screen_end = p_y;
                        else if (gizmo.active_axis() == cd::editor::GizmoAxis::kZ) axis_screen_end = p_z;
                        const float ax_dx = axis_screen_end.x - p_org.x;
                        const float ax_dy = axis_screen_end.y - p_org.y;
                        const float ax_len_px = std::sqrt(ax_dx*ax_dx + ax_dy*ax_dy);
                        if (ax_len_px > 1.0F)
                        {
                            const float nx = ax_dx / ax_len_px, ny = ax_dy / ax_len_px;
                            const float mouse_dx = mp.x - gizmo_drag_anchor.x;
                            const float mouse_dy = mp.y - gizmo_drag_anchor.y;
                            const float dot_px = mouse_dx * nx + mouse_dy * ny;
                            const float world_per_px = kAxisLen / ax_len_px;
                            const float delta_world = dot_px * world_per_px;
                            // Only translate works for both entities and
                            // lights; rotate/scale need a transform record
                            // and are gated on lt != nullptr.
                            switch (gizmo_mode)
                            {
                                case GizmoMode::kTranslate:
                                {
                                    cd::math::Vec3f cur = gizmo_drag_world_start;
                                    switch (gizmo.active_axis())
                                    {
                                        case cd::editor::GizmoAxis::kX: cur.x += delta_world; break;
                                        case cd::editor::GizmoAxis::kY: cur.y += delta_world; break;
                                        case cd::editor::GizmoAxis::kZ: cur.z += delta_world; break;
                                        default: break;
                                    }
                                    *target_pos = cur;
                                    gizmo.update_drag(cur);
                                    break;
                                }
                                case GizmoMode::kScale:
                                {
                                    if (lt == nullptr) break;
                                    cd::math::Vec3f cur = gizmo_drag_scale_start;
                                    const float factor = std::exp(delta_world * 0.5F);
                                    switch (gizmo.active_axis())
                                    {
                                        case cd::editor::GizmoAxis::kX: cur.x *= factor; break;
                                        case cd::editor::GizmoAxis::kY: cur.y *= factor; break;
                                        case cd::editor::GizmoAxis::kZ: cur.z *= factor; break;
                                        default: break;
                                    }
                                    if (cur.x < 0.05F) cur.x = 0.05F;
                                    if (cur.y < 0.05F) cur.y = 0.05F;
                                    if (cur.z < 0.05F) cur.z = 0.05F;
                                    lt->value.scale = cur;
                                    break;
                                }
                                case GizmoMode::kRotate:
                                {
                                    if (lt == nullptr) break;
                                    // Compute the angle the mouse has swept around the
                                    // gizmo center since drag start (atan2 difference).
                                    // This is the natural rotation gizmo UX: dragging
                                    // tangentially around the object rotates it.
                                    const float anchor_dx = gizmo_drag_anchor.x - p_org.x;
                                    const float anchor_dy = gizmo_drag_anchor.y - p_org.y;
                                    const float cur_dx    = mp.x - p_org.x;
                                    const float cur_dy    = mp.y - p_org.y;
                                    if (std::sqrt(anchor_dx*anchor_dx + anchor_dy*anchor_dy) < 5.0F)
                                        break;  // too close to center, ignore
                                    const float a_anchor = std::atan2(anchor_dy, anchor_dx);
                                    const float a_now    = std::atan2(cur_dy,    cur_dx);
                                    float ang = a_now - a_anchor;
                                    // Wrap to (-π, π].
                                    while (ang >  3.1415926F) ang -= 6.2831853F;
                                    while (ang < -3.1415926F) ang += 6.2831853F;
                                    const float ca = std::cos(ang * 0.5F);
                                    const float sa = std::sin(ang * 0.5F);
                                    cd::math::Quatf q { 0,0,0,1 };
                                    switch (gizmo.active_axis())
                                    {
                                        case cd::editor::GizmoAxis::kX: q = { sa, 0, 0, ca }; break;
                                        case cd::editor::GizmoAxis::kY: q = { 0, sa, 0, ca }; break;
                                        case cd::editor::GizmoAxis::kZ: q = { 0, 0, sa, ca }; break;
                                        default: break;
                                    }
                                    // new_rot = q * start (apply axis-rotation in
                                    // world-space to the start orientation).
                                    const auto& a = q;
                                    const auto& b = gizmo_drag_rot_start;
                                    lt->value.rotation = cd::math::Quatf {
                                        a.w*b.x + a.x*b.w + a.y*b.z - a.z*b.y,
                                        a.w*b.y - a.x*b.z + a.y*b.w + a.z*b.x,
                                        a.w*b.z + a.x*b.y - a.y*b.x + a.z*b.w,
                                        a.w*b.w - a.x*b.x - a.y*b.y - a.z*b.z };
                                    break;
                                }
                            }
                        }
                        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
                        {
                            const auto delta = gizmo.end_drag();
                            (void)delta;
                            switch (gizmo_mode)
                            {
                                case GizmoMode::kTranslate:
                                {
                                    const float dx = target_pos->x - gizmo_drag_world_start.x;
                                    const float dy = target_pos->y - gizmo_drag_world_start.y;
                                    const float dz = target_pos->z - gizmo_drag_world_start.z;
                                    if (std::abs(dx) + std::abs(dy) + std::abs(dz) > 1e-4F)
                                    {
                                        if (target_is_entity && lt != nullptr)
                                        {
                                            // Roll back live mutation + push undoable command.
                                            *target_pos = gizmo_drag_world_start;
                                            history.push(std::make_unique<cd::editor::TranslateCommand>(
                                                scene, sel_ent, cd::math::Vec3f { dx, dy, dz }));
                                            log_push("[gizmo] entity translate (undoable)");
                                        }
                                        else
                                        {
                                            // Light translate — apply directly (no history wire yet).
                                            log_push("[gizmo] light translate applied");
                                        }
                                    }
                                    break;
                                }
                                case GizmoMode::kScale:
                                    if (lt != nullptr) log_push("[gizmo] scale applied");
                                    break;
                                case GizmoMode::kRotate:
                                    if (lt != nullptr) log_push("[gizmo] rotate applied");
                                    break;
                            }
                        }
                    }
                }
            }
        }

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
    streamer.stop();

    // ---- Cleanup ----
    destroy_mesh(device, cube_mesh);
    destroy_mesh(device, sphere_mesh);
    destroy_mesh(device, cone_mesh);
    destroy_mesh(device, cyl_mesh);
    destroy_mesh(device, torus_mesh);
    destroy_mesh(device, floor_mesh);
    destroy_mesh(device, pbr_sphere);
    depth.destroy(device);
    // Faz 1.6 CSM resources.
    shadow_target.destroy(device);
    device.destroy_sampler(shadow_sampler);
    device.destroy_buffer(shadow_ubo);
    // Faz 1.7 RT resources — wait_idle so any in-flight cmd buffers
    // that referenced these structures are guaranteed done, then
    // tear down the TLAS queue + every BLAS.
    device.wait_idle();
    if (current_tlas.is_valid()) device.destroy_acceleration_structure(current_tlas);
    while (!tlas_destroy_queue.empty()) {
        device.destroy_acceleration_structure(tlas_destroy_queue.front().h);
        tlas_destroy_queue.pop_front();
    }
    for (auto h : { blas_cube, blas_sphere, blas_cone, blas_cyl, blas_torus, blas_floor })
        if (h.is_valid()) device.destroy_acceleration_structure(h);
    std::printf("hello_engine: clean exit (%u frames).\n", frame_idx);
    return 0;
}
