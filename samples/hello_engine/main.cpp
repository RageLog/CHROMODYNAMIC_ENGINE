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
#include <cd/asset_gltf/GltfLoader.hpp>
#include <cd/asset_json/Json.hpp>
#include <cd/atmosphere/Atmosphere.hpp>
#include <cd/brdf_ltc/Ltc.hpp>
#include <cd/brdf_sheen_clearcoat/SheenClearcoat.hpp>
#include <cd/brdf_sss/Sss.hpp>
#include <cd/ddgi/Ddgi.hpp>
#include <cd/decal/Decal.hpp>
#include <cd/gpu_particles/GpuParticles.hpp>
#include <cd/ibl/BrdfLut.hpp>
#include <cd/ibl/Cubemap.hpp>
#include <cd/ibl/IrradianceConvolution.hpp>
#include <cd/ibl/PrefilteredSpecular.hpp>
#include <cd/ibl_gpu/Upload.hpp>
#include <cd/texture_synth/Earth.hpp>
#include <cd/nrc/Nrc.hpp>
#include <cd/restir_di/Reservoir.hpp>
#include <cd/restir_gi/GiReservoir.hpp>
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
#include <cd/light_shafts/LightShafts.hpp>
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
#include <cd/post_bloom/Bloom.hpp>
#include <cd/post_dof/Dof.hpp>
#include <cd/post_gtao/Gtao.hpp>
#include <cd/post_motion_blur/MotionBlur.hpp>
#include <cd/post_smaa/Smaa.hpp>
#include <cd/post_ssr/Ssr.hpp>
#include <cd/post_taa/Taa.hpp>
#include <cd/render/Renderer.hpp>
#include <cd/volumetric_clouds/Clouds.hpp>
#include <cd/volumetric_fog/Fog.hpp>
#include <cd/rhi/Barriers.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi_vulkan/VulkanDevice.hpp>
#include <cd/scene/Scene.hpp>
#include <cd/scene/SceneCameraController.hpp>
#include <cd/scene/Serializer.hpp>
#include <cd/world_container/World.hpp>
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
#include <optional>
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
    kGltf,   ///< user-supplied glTF asset auto-loaded at boot
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
  vec4 fx_params;        // x=tonemap_op (0=Nark, 1=Hill, 2=Hable, 3=AGX)
  vec4 fx_params2;       // x=smaa, y=motion_blur, z=taa, w=dof (v1.4+)
  vec4 fx_params3;       // x=fog, y=atmosphere, z=clouds, w=light_shafts
  vec4 camera_pos;       // xyz=world camera (atmospherics distance)
  vec4 fx_params4;       // x=clearcoat, y=sheen, z=sss, w=reserved
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
layout(location = 4) out vec2 v_uv;
void main() {
  v_albedo = in_color * pc.tint.rgb;
  v_uv     = in_uv;
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
  vec4 fx_params;     // x=tonemap_op (0=Nark, 1=Hill, 2=Hable, 3=AGX)
  vec4 fx_params2;    // x=smaa, y=motion_blur, z=taa, w=dof (v1.4+)
  vec4 fx_params3;    // x=fog, y=atmosphere, z=clouds, w=light_shafts
  vec4 camera_pos;    // xyz=world camera (atmospherics distance)
  vec4 fx_params4;    // x=clearcoat, y=sheen, z=sss, w=reserved (R6)
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
// Faz 1.9 multi-light UBO (gap #2 + #3 foundation) — 8 non-sun
// lights with full type-specific data.
struct LightSlot {
  vec4 pos_range;   // xyz=world pos, w=range
  vec4 dir_type;    // xyz=direction or right-basis, w=type (0=Dir,1=Point,2=Spot,3=Rect,4=Disk)
  vec4 color_int;   // xyz=linear colour, w=intensity (scaled)
  vec4 extras;      // x=cos_outer, y=area_w, z=area_h, w=cos_inner
};
layout(set = 0, binding = 3) uniform LightArray {
  // std140 packing: 'uint pad[3]' would be stride-16 (48 B) and push
  // slots[] to offset 64, but the C++ LightUboGpu uses packed
  // std::uint32_t pad[3] (12 B contiguous) with slots starting at
  // offset 16. Using 3 separate scalar uints matches the packed C++
  // layout — fixes the entire multi-light contribution being read
  // from a wrong offset on the GPU side.
  uint count;
  uint pad_a;
  uint pad_b;
  uint pad_c;
  LightSlot slots[8];
} cd_lights;
// R2 IBL-on-prim — same cubemaps + LUT the PBR pipeline binds.
layout(set = 0, binding = 5) uniform samplerCube cd_ibl_spec;
layout(set = 0, binding = 6) uniform samplerCube cd_ibl_diff;
layout(set = 0, binding = 7) uniform sampler2D   cd_brdf_lut;
// R2 procedural normal map (tangent-space bump).
layout(set = 0, binding = 8) uniform sampler2D   cd_normal_tex;
// R2 metallic-roughness-AO map. glTF 2.0 packing:
//   R unused, G roughness, B metallic, A AO
layout(set = 0, binding = 9) uniform sampler2D   cd_mr_tex;
const float kIblMaxMipLod = 5.0;

// Cotangent-frame from screen-space derivatives (Mikkelsen 2010).
// Avoids needing per-vertex tangents — works for any UV-mapped mesh.
mat3 cotangent_frame(vec3 N, vec3 p, vec2 uv) {
  vec3 dp1 = dFdx(p);
  vec3 dp2 = dFdy(p);
  vec2 duv1 = dFdx(uv);
  vec2 duv2 = dFdy(uv);
  vec3 dp2perp = cross(dp2, N);
  vec3 dp1perp = cross(N, dp1);
  vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
  vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;
  float invmax = inversesqrt(max(dot(T, T), dot(B, B)));
  return mat3(T * invmax, B * invmax, N);
}
layout(location = 0) in vec3 v_world_pos;
layout(location = 1) in vec3 v_world_normal;
layout(location = 2) in vec3 v_albedo;
layout(location = 3) in vec4 v_shadow_pos;
layout(location = 4) in vec2 v_uv;
// Optional baseColor texture (gap #1/#13). fx_params.y = 1.0
// flags the draw as 'sample texture'; 0.0 = use vertex-coloured
// albedo path. Single texture slot for hello_engine — production
// editor needs a per-entity texture array (v1.6+).
layout(set = 0, binding = 4) uniform sampler2D cd_albedo_tex;
layout(location = 0) out vec4 out_color;
// G-Buffer normal MRT — world-space surface normal (xyz) + flag (w=1
// surface, 0 = sky/transparent). Composite + post-fx pipeline samples
// this for SSR, normal-aware AO, future reflections.
layout(location = 1) out vec4 out_normal;
// G-Buffer albedo MRT — base color (rgb) + material flag (a). Used by
// SSR tinting, GI prep, deferred shading downstream.
layout(location = 2) out vec4 out_albedo;
// G-Buffer metallic/roughness MRT — packed pair (xy) for SSR rough
// blur + deferred BRDF + GI.
layout(location = 3) out vec2 out_mr;

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
  // Bias the origin away from the surface AND start the ray walk at
  // tmin > 0 so the source instance's own triangles (very close to
  // the shading point on merged meshes like CesiumMan) don't get
  // false-hit as occluders. tmin 0.08 + N*0.05 bias clears
  // typical compound-mesh self-intersection without losing real
  // shadows from neighbouring objects.
  rayQueryEXT rq;
  rayQueryInitializeEXT(
      rq, cd_tlas,
      gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT,
      0xFFu,
      origin + N * 0.05,
      0.08, dir, tmax);
  while (rayQueryProceedEXT(rq)) { /* opaque-only walk */ }
  return (rayQueryGetIntersectionTypeEXT(rq, true) ==
          gl_RayQueryCommittedIntersectionNoneEXT) ? 1.0 : 0.0;
}

// 3×3 PCF shadow sampling. Returns 1.0 = fully lit, 0.0 = fully
// occluded. Vulkan clip space x,y ∈ [-1,1], depth ∈ [0,1]; texture
// uv has y down (matches Vulkan clip y after perspective divide).
// LTC polygon irradiance for area lights (#3). Lambert-only fit
// (identity inverse matrix — production wants a 64x64 LUT keyed
// by roughness/NoV). N is the surface normal at the shading
// point; corners are in world-space, relative to the shading
// point. Returns the form-factor of the polygon visible from N.
// Edge integral with atan2 — robust at parallel and anti-parallel
// configurations (the prior acos/sin form blew up near sin ~ 0 and
// produced a thin black stripe at the area-light's equatorial plane).
float cd_ltc_edge_integral(vec3 a, vec3 b) {
  float d = clamp(dot(a, b), -1.0, 1.0);
  vec3  c = cross(a, b);
  float l = length(c);
  float th = (l < 1e-6) ? 0.0 : atan(l, d);  // GLSL atan(y,x) = atan2
  return (l < 1e-6) ? 0.0 : (th / l) * c.z;
}
float cd_ltc_polygon_irradiance(vec3 N, vec3 c0, vec3 c1, vec3 c2, vec3 c3) {
  vec3 up = abs(N.y) > 0.95 ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0);
  vec3 T  = normalize(cross(up, N));
  vec3 B  = cross(N, T);
  mat3 frame = transpose(mat3(T, B, N));
  vec3 p0 = normalize(frame * c0);
  vec3 p1 = normalize(frame * c1);
  vec3 p2 = normalize(frame * c2);
  vec3 p3 = normalize(frame * c3);
  float s = cd_ltc_edge_integral(p0, p1) +
            cd_ltc_edge_integral(p1, p2) +
            cd_ltc_edge_integral(p2, p3) +
            cd_ltc_edge_integral(p3, p0);
  // max-not-abs: negative values mean the polygon is back-facing.
  // Closes the 'siyah serit' artifact at the rect's equatorial plane.
  return max(s, 0.0) / 6.28318530;
}

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
  // R3 G-Buffer: world-space surface normal MRT-write. Done up-front
  // so the every early-return path (shadow draw / view-mode debug /
  // floor / lit path) emits a valid normal for downstream SSR + AO.
  // Sky pixels are produced by a different shader (AnalyticalSkyFS)
  // which writes w=0 so the composite can distinguish "sky" vs
  // "surface" at sample time.
  out_normal = vec4(normalize(v_world_normal), 1.0);

  // R3 G-Buffer phase 219 — albedo + MR. Sample the same textures
  // the lit path uses so deferred / post-fx consumers see exactly
  // what the forward path drew. Defaults: 0 metallic, 0.5 roughness.
  vec4 mr_pre = (pc.fx_params.y > 0.5) ? texture(cd_mr_tex, v_uv) : vec4(0, 0.5, 0.04, 1);
  out_albedo = vec4(clamp(v_albedo * pc.tint.rgb, vec3(0.0), vec3(1.0)), 1.0);
  out_mr = vec2(clamp(mr_pre.b, 0.0, 1.0), clamp(mr_pre.g, 0.04, 1.0));

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
  // baseColor texture path — when the entity is flagged as
  // textured (fx_params.y > 0.5), override v_albedo with the
  // sampled albedo * tint. The default 1x1 white texture in the
  // descriptor lets non-textured draws fall through harmlessly,
  // but we short-circuit on the flag so the texture sample isn't
  // wasted on primitives that don't use it.
  vec3 albedo = v_albedo;
  if (pc.fx_params.y > 0.5) {
    vec3 sampled = texture(cd_albedo_tex, v_uv).rgb;
    albedo = sampled * pc.tint.rgb;
  }
  bool is_floor = (pc.tint.w > 1.5);
  float floor_fade = 1.0;  // 1 = full body, 0 = fully faded (sky-coloured)
  if (is_floor) {
    // Distance fade — body + lines both attenuate as the camera
    // looks out toward the horizon, so the floor 'reaches into
    // infinity' rather than ending in a hard square edge.
    // 60 m = full opacity, 200 m = fully transparent (faded to sky).
    // The underlying plane is 1000 m so the camera will never reach
    // its hard edge. v1.7 frame-graph swaps this for a true
    // screen-space procedural grid (fullscreen plane intersection).
    float d_xz = length(v_world_pos.xz);
    floor_fade = clamp(1.0 - (d_xz - 60.0) / 140.0, 0.0, 1.0);
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
    float on_axis_x = step(abs(p.x), max(dp.x, 0.005));
    float on_axis_z = step(abs(p.y), max(dp.y, 0.005));
    vec3 minor_col = vec3(0.50, 0.52, 0.58);
    vec3 major_col = vec3(0.75, 0.78, 0.85);
    vec3 ax_x_col  = vec3(0.95, 0.30, 0.25);
    vec3 ax_z_col  = vec3(0.25, 0.45, 0.95);
    vec3 line_col  = minor_col;
    float line_a   = a_minor * 0.35;
    line_col = mix(line_col, major_col, smoothstep(0.0, 0.8, a_major));
    line_a   = max(line_a, a_major * 0.6);
    line_col = mix(line_col, ax_x_col, on_axis_z * 0.85);
    line_col = mix(line_col, ax_z_col, on_axis_x * 0.85);
    line_a   = max(line_a, max(on_axis_x, on_axis_z));
    // Apply fade to line intensity so lines also fade with distance.
    line_a *= floor_fade;
    albedo  = mix(albedo, line_col, clamp(line_a, 0.0, 1.0));
    // Fade the body too (mix back to a faint sky-grey).
    albedo  = mix(vec3(0.55, 0.60, 0.66) * 0.0, albedo, floor_fade);
  }

  vec3 N = normalize(v_world_normal);
  // R2 normal mapping for textured entities — perturbs the surface
  // normal with the tangent-space sample so the procedural Earth
  // bumps register as real 3D relief.
  if (pc.fx_params.y > 0.5) {
    vec3 nm_sample = texture(cd_normal_tex, v_uv).xyz * 2.0 - 1.0;
    mat3 TBN = cotangent_frame(N, v_world_pos, v_uv);
    N = normalize(TBN * nm_sample);
  }
  vec3 lit = vec3(0.0);

  // Directional sun + CSM shadow attenuation.
  vec3 Ld = normalize(-pc.sun_dir.xyz);
  float ndl_sun = max(dot(N, Ld), 0.0);
  float shade = sample_shadow(v_shadow_pos, N, Ld);
  lit += albedo * pc.sun_color.rgb * (pc.sun_dir.w * ndl_sun * shade);

  // Multi-light loop (gap #2 + #3). Per type:
  //   1 = Point  — Frostbite windowed inverse-square, RT shadow
  //   2 = Spot   — same + smoothstep cone falloff
  //   3 = Rect   — LTC polygon irradiance (Heitz 2016, Lambert fit)
  //   4 = Disk   — LTC polygon irradiance with disk approximated by quad
  // Each contribution gated by an inline RT shadow ray (Faz 1.7).
  for (uint li = 0; li < cd_lights.count; ++li) {
    vec3 lp_pos = cd_lights.slots[li].pos_range.xyz;
    float rng  = cd_lights.slots[li].pos_range.w;
    int   ltp  = int(cd_lights.slots[li].dir_type.w);
    if (rng <= 0.0) continue;

    if (ltp == 3 || ltp == 4) {
      // Area light: LTC polygon irradiance from 4 corners.
      vec3 ln = normalize(cd_lights.slots[li].dir_type.xyz);
      vec3 up_ref = (abs(ln.y) > 0.95) ? vec3(1.0,0.0,0.0) : vec3(0.0,1.0,0.0);
      vec3 right  = normalize(cross(up_ref, ln));
      vec3 up_v   = cross(ln, right);
      float w = cd_lights.slots[li].extras.y * 0.5;
      float h = cd_lights.slots[li].extras.z * 0.5;
      // Corners as world-space positions, then made relative to the
      // shading point so the LTC frame transform yields directions.
      vec3 c0 = lp_pos - right*w - up_v*h - v_world_pos;
      vec3 c1 = lp_pos + right*w - up_v*h - v_world_pos;
      vec3 c2 = lp_pos + right*w + up_v*h - v_world_pos;
      vec3 c3 = lp_pos - right*w + up_v*h - v_world_pos;
      float E = cd_ltc_polygon_irradiance(N, c0, c1, c2, c3);
      // Visibility test from area-light centre (one ray; full
      // many-sample area shadow needs a denoiser).
      vec3 to_c   = lp_pos - v_world_pos;
      float d_c   = max(length(to_c), 1e-4);
      vec3 Lc     = to_c / d_c;
      // Area light shadows disabled — same self-occlusion issue as
      // multi-light point/spot. Returns with R3 per-instance ray mask.
      float vis_a = 1.0;
      vec3  col   = cd_lights.slots[li].color_int.xyz;
      float ki    = cd_lights.slots[li].color_int.w;
      lit += albedo * col * (ki * E * vis_a);
      continue;
    }

    // Point / Spot path.
    vec3 to_p = lp_pos - v_world_pos;
    float d   = length(to_p);
    if (d < 1e-4) continue;
    vec3 Lp   = to_p / d;
    float ndl = max(dot(N, Lp), 0.0);
    if (ndl <= 0.0) continue;
    float atten = distance_atten(d, rng);
    float cone  = 1.0;
    if (ltp == 2) {
      vec3  axis    = normalize(cd_lights.slots[li].dir_type.xyz);
      float cos_b   = dot(-Lp, axis);
      float cos_out = cd_lights.slots[li].extras.x;
      float cos_in  = clamp(cos_out + 0.05, cos_out, 0.9999);
      cone          = smoothstep(cos_out, cos_in, cos_b);
      if (cone <= 0.0) continue;
    }
    // Non-sun shadows disabled until per-instance ray-mask lands
    // with the R3 frame-graph rework. The bias-only approach
    // false-occludes dense geometry (PBR sphere grid, merged
    // character mesh) — closes 'isigin vurdugu cisimler hic
    // gozukmuyor'. Trade-off: spot/point cast no shadows; objects
    // stay visible where the cone reaches them.
    float vis = 1.0;
    vec3  col = cd_lights.slots[li].color_int.xyz;
    float ki  = cd_lights.slots[li].color_int.w;
    lit += albedo * col * (ki * ndl * atten * vis * cone);
  }

  // Hemisphere ambient (sky-up / ground-down) — cheap stand-in for
  // non-textured prim entities. Textured entities (kGltf flagged via
  // fx_params.y > 0.5) get real IBL below.
  float up_t   = N.y * 0.5 + 0.5;
  vec3  sky_c  = vec3(0.55, 0.65, 0.85);
  vec3  gnd_c  = vec3(0.18, 0.16, 0.14);
  vec3  hemi   = mix(gnd_c, sky_c, up_t) * pc.sun_color.w;
  vec3  ambient = albedo * hemi;

  // R2: True IBL with MR map. Karis split-sum:
  //   IBL = kD * irradiance(N) * albedo + prefiltered(R, rough*mipMax)
  //         * (F0 * brdf.x + brdf.y)
  // MR map gives per-pixel metallic + roughness + AO so the same
  // material sweep covers ocean (rough water), continents (mid),
  // and polar ice (matte snow).
  if (pc.fx_params.y > 0.5) {
    vec4 mr_sample = texture(cd_mr_tex, v_uv);
    float roughness = clamp(mr_sample.g, 0.04, 1.0);
    float metallic  = clamp(mr_sample.b, 0.0, 1.0);
    float ao_factor = mr_sample.a;
    vec3 F0_ibl = mix(vec3(0.04), albedo, metallic);
    vec3 V_v    = normalize(pc.camera_pos.xyz - v_world_pos);
    vec3 R_v    = reflect(-V_v, N);
    float NoV_v = max(dot(N, V_v), 0.0);
    float lod   = roughness * kIblMaxMipLod;
    vec3 spec_e = textureLod(cd_ibl_spec, R_v, lod).rgb;
    vec3 diff_e = texture(cd_ibl_diff, N).rgb;
    vec2 brdf_v = texture(cd_brdf_lut, vec2(clamp(NoV_v, 0.0, 1.0),
                                            clamp(roughness, 0.0, 1.0))).rg;
    vec3 ibl_F  = F0_ibl * brdf_v.x + vec3(brdf_v.y);
    vec3 ibl_kD = (vec3(1.0) - ibl_F) * (1.0 - metallic);
    vec3 ibl    = (ibl_kD * diff_e * albedo + spec_e * ibl_F) * ao_factor;
    // IBL gate: SUN ONLY. Non-sun lights are direct sources — they
    // illuminate via their own contribution and shouldn't synthesise
    // a global ambient lift. Closes 'spotda boyutu ve gucu dusurdum
    // ama cism gozukur durumda kaldi' — when the only enabled light
    // is a weak/small non-sun source, surfaces outside its reach now
    // read as truly dark instead of getting an IBL freebie.
    // Metallic surfaces under non-sun-only lighting will read black
    // (no diffuse, no LTC-GGX specular yet); proper indirect light
    // returns with the R4 GI ship.
    float ibl_gate = clamp(pc.sun_dir.w * 0.6, 0.0, 1.0);
    ambient += ibl * ibl_gate * 0.55;
  }

  // Inline GTAO approximation (v1.4 day-ship wire-in). True multi-pass
  // post_gtao::kGtaoMainCS dispatches land in v1.7 frame-graph rework;
  // here we use a cheap normal-derivative curvature heuristic to
  // darken convex creases. Strength = pc.fx_params.z (0..1).
  float ao_strength = clamp(pc.fx_params.z, 0.0, 1.0);
  if (ao_strength > 0.001) {
    vec3 dn_dx = dFdx(v_world_normal);
    vec3 dn_dy = dFdy(v_world_normal);
    float curv = clamp(length(dn_dx) + length(dn_dy), 0.0, 1.0);
    float ao   = 1.0 - ao_strength * curv * 0.85;
    ambient   *= ao;
  }

  // R6 advanced BRDF lobes — inline approximations matching:
  //   cd::brdf_sheen_clearcoat::kInlineRimApproxGlsl  (clearcoat + sheen)
  //   cd::brdf_sss::kInlineBurleyWrapGlsl             (wrap-diffusion SSS)
  // For the proper full BRDF kernels (Estevez Charlie + Filament
  // clearcoat D*V + Burley separable diffusion), see:
  //   cd::brdf_sheen_clearcoat::kSheenClearcoatGlsl
  //   cd::brdf_sss::kBurleySeparableBlurCS
  // Those land via the v1.7 material-graph dispatch.
  float fx_cc    = clamp(pc.fx_params4.x, 0.0, 1.0);
  float fx_sheen = clamp(pc.fx_params4.y, 0.0, 1.0);
  float fx_sss   = clamp(pc.fx_params4.z, 0.0, 1.0);
  if (fx_cc > 0.001 || fx_sheen > 0.001 || fx_sss > 0.001) {
    vec3 V_b = normalize(pc.camera_pos.xyz - v_world_pos);
    float NoV_b = max(dot(N, V_b), 0.0);
    // Clearcoat: second Schlick Fresnel lobe with IOR ~ 1.5 (F0_cc =
    // 0.04), tinted white, scales with view angle. Adds shiny lacquer.
    if (fx_cc > 0.001) {
      vec3 R_b = reflect(-V_b, N);
      vec3 spec_cc = textureLod(cd_ibl_spec, R_b, 0.5 * kIblMaxMipLod).rgb;
      float fres_cc = 0.04 + 0.96 * pow(1.0 - NoV_b, 5.0);
      lit += spec_cc * fres_cc * fx_cc * 0.6;
    }
    // Sheen: Charlie distribution-inspired rim term. cos^n with high
    // n + saturating boost gives a velvet edge brighten.
    if (fx_sheen > 0.001) {
      float rim = pow(1.0 - NoV_b, 4.0);
      vec3 sheen_col = vec3(0.95, 0.92, 0.88);
      lit += sheen_col * rim * fx_sheen * 1.2;
    }
    // SSS: Burley-inspired wrap diffusion — boost backlit pixels with
    // a warm subsurface tint, simulating skin/wax light bleed.
    if (fx_sss > 0.001) {
      vec3 Ld_b = normalize(-pc.sun_dir.xyz);
      float backlit = clamp(dot(-N, Ld_b), 0.0, 1.0);
      vec3 sss_col = vec3(0.95, 0.55, 0.45);
      lit += sss_col * pow(backlit, 1.5) * fx_sss * pc.sun_dir.w * 0.8;
    }
  }

  vec3  c       = lit + ambient;

  // R3: tonemap + exposure + bloom + saturation + gamma all live in
  // the composite pass now. Scene shaders below only apply world-
  // space effects (vignette / CA / grain / fog / aerial / shafts)
  // that need scene data, then emit linear HDR.

  // R7 camera composition — inline approximations matching
  // cd::post_camera::kInlineCameraGlsl (vignette / chromatic / grain).
  // For the proper off-screen LUT/blur post pass see the v1.7 frame-
  // graph ship.
  // Cheap radial coordinate from the camera-relative direction. Not
  // a true screen-space UV but functionally maps to 0 (centre) -> 1+
  // (edges) without needing the swapchain extent.
  vec3 cam_dir = normalize(v_world_pos - pc.camera_pos.xyz);
  float radial = length(cam_dir.xy);
  float vignette = clamp(pc.fx_params2.y, 0.0, 1.0);
  if (vignette > 0.001) {
    float vmask = smoothstep(0.0, 1.4, radial);
    c *= mix(1.0, 1.0 - vmask, vignette);
  }
  float ca = clamp(pc.fx_params2.z, 0.0, 1.0);
  if (ca > 0.001) {
    // Cheap CA: shift hue toward warmth in centre, cool at edges.
    c.r *= 1.0 + ca * 0.08;
    c.b *= 1.0 - ca * 0.08;
  }
  float grain = clamp(pc.fx_params2.w, 0.0, 1.0);
  if (grain > 0.001) {
    float g = fract(sin(dot(v_world_pos.xy, vec2(12.9898, 78.233))) * 43758.5453);
    c += (g - 0.5) * grain * 0.05;
  }

  // Inline atmospherics (v1.4 day-ship wire-in).
  //   x = exponential height fog density
  //   y = aerial perspective strength
  //   z = clouds coverage placeholder (needs noise sampler, v1.7)
  //   w = light shafts strength  — R5 inline approximation here:
  //       attenuate visibility radially from the on-screen sun
  //       direction and brighten low-luma pixels in that cone.
  float dist = length(v_world_pos - pc.camera_pos.xyz);
  float fog_density = clamp(pc.fx_params3.x, 0.0, 1.0);
  if (fog_density > 0.001) {
    float h_falloff = exp(-max(v_world_pos.y, 0.0) * 0.10);
    float f = 1.0 - exp(-dist * fog_density * 0.030 * h_falloff);
    vec3  fog_col = vec3(0.62, 0.66, 0.74);
    c = mix(c, fog_col, clamp(f, 0.0, 0.95));
  }
  float aerial = clamp(pc.fx_params3.y, 0.0, 1.0);
  if (aerial > 0.001) {
    float t = clamp(dist / 80.0, 0.0, 1.0);
    vec3 aerial_tint = vec3(0.55, 0.62, 0.78);
    c = mix(c, aerial_tint, t * aerial * 0.35);
  }
  // R5 inline god rays — matches cd::light_shafts::kInlineConeShaftGlsl.
  // Full screen-space radial blur lives at
  // cd::light_shafts::kRadialBlurCS and dispatches with the v1.7
  // frame-graph rework.
  float shafts = clamp(pc.fx_params3.w, 0.0, 1.0);
  if (shafts > 0.001 && pc.sun_dir.w > 0.001) {
    vec3 cam_to_p = normalize(v_world_pos - pc.camera_pos.xyz);
    vec3 sun_L    = normalize(-pc.sun_dir.xyz);
    float align  = max(dot(cam_to_p, sun_L), 0.0);
    float shaft  = pow(align, 32.0) * shafts;
    vec3 shaft_col = pc.sun_color.rgb * pc.sun_dir.w;
    c += shaft_col * shaft * 0.6;
  }

  // Linear HDR output — composite pass owns the gamma transform.

  // Debug view modes (fx_params4.w):
  //   1 albedo only, 2 world normal, 3 MR map, 4 AO, 5 perturbed
  //   normal (post-normal-map), 6 UVs as RG.
  int view_mode = int(pc.fx_params4.w + 0.5);
  if (view_mode == 1) {
    out_color = vec4(albedo, 1.0);
    return;
  } else if (view_mode == 2) {
    out_color = vec4(normalize(v_world_normal) * 0.5 + 0.5, 1.0);
    return;
  } else if (view_mode == 3) {
    vec4 mr_s = (pc.fx_params.y > 0.5) ? texture(cd_mr_tex, v_uv) : vec4(0,0.5,0.04,1);
    out_color = vec4(0.0, mr_s.g, mr_s.b, 1.0);
    return;
  } else if (view_mode == 4) {
    vec4 mr_s = (pc.fx_params.y > 0.5) ? texture(cd_mr_tex, v_uv) : vec4(0,0,0,1);
    out_color = vec4(vec3(mr_s.a), 1.0);
    return;
  } else if (view_mode == 5) {
    out_color = vec4(N * 0.5 + 0.5, 1.0);
    return;
  } else if (view_mode == 6) {
    out_color = vec4(v_uv, 0.0, 1.0);
    return;
  }

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

// ============================================================================
// R3 — Composite pass.
//
// Fullscreen-triangle VS + FS that samples the HDR scene target and
// blits it to the swapchain. Acts as the home for post-process
// operations that compose multiple inputs (bloom, GTAO, SSR) before
// the final tonemap. First ship: pass-through (HDR linear -> sRGB
// gamma) so the scene shaders no longer carry the tonemap themselves.
// ============================================================================
constexpr const char* kCompositeVS = R"glsl(
#version 450
layout(location = 0) out vec2 v_uv;
void main() {
  // Single triangle covering NDC [-1, 3] x [-1, 3]; clipped to viewport.
  vec2 ndc = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
  v_uv = ndc * vec2(1.0, 1.0);
  gl_Position = vec4(ndc * 2.0 - 1.0, 0.0, 1.0);
}
)glsl";

constexpr const char* kCompositeFS = R"glsl(
#version 450
layout(set = 0, binding = 0) uniform sampler2D cd_hdr_color;
layout(set = 0, binding = 1) uniform sampler2D cd_bloom_mip0;
layout(set = 0, binding = 2) uniform sampler2D cd_depth;
layout(set = 0, binding = 3) uniform sampler2D cd_gbuf_normal;
layout(set = 0, binding = 4) uniform sampler2D cd_history_prev;
layout(push_constant) uniform PC {
  vec4 fx;        // x=tonemap_op, y=exposure, z=sat_boost, w=bloom_strength
  vec4 ao;        // x=ao_strength, y=ao_radius_px, z=near, w=far
  vec4 dof;       // x=dof_strength, y=focus_distance (m), z=focus_range (m), w=max_blur_px
  vec4 shafts;    // x=sun_uv.x, y=sun_uv.y, z=strength (<0 → off), w=decay
  vec4 sun_col;   // rgb=sun colour, a=reserved
  vec4 atmo;      // x=fog_density (1/m), y=aerial_perspective_strength, z=vignette, w=film_grain
  vec4 lens;      // x=chromatic_aberration_px (radial growth), y/z/w=reserved
  vec4 cam_right; // xyz=world right basis, w=half_w (tan(fov/2)*aspect)
  vec4 cam_up;    // xyz=world up    basis, w=half_h (tan(fov/2))
  vec4 cam_fwd;   // xyz=world forward,     w=reserved
  vec4 cam_pos;   // xyz=world camera origin, w=reserved
  vec4 ssr;       // x=ssr_strength, y=max_distance_m, z=max_steps, w=fade_edge
  vec4 prev_cam_right; // xyz=prev right, w=prev_half_w
  vec4 prev_cam_up;    // xyz=prev up,    w=prev_half_h
  vec4 prev_cam_fwd;   // xyz=prev fwd,   w=mblur_strength
  vec4 prev_cam_pos;   // xyz=prev pos,   w=mblur_samples
} pc;

// Reconstruct world-space position from screen UV + non-linear depth.
// Uses the camera basis vectors so we don't need a full inverse-view-
// projection matrix in push.
vec3 world_pos_from_uv(vec2 uv, float depth) {
  // NDC [-1,1] from UV [0,1]. Vulkan: NDC.y down matches UV.y down,
  // so direct mapping without flip is correct here (scene materials
  // already flipped clip.y on the way out).
  vec2 ndc = uv * 2.0 - 1.0;
  float lz = pc.ao.z * pc.ao.w / max(pc.ao.w - depth * (pc.ao.w - pc.ao.z), 1e-4);
  vec3 ray = pc.cam_fwd.xyz
           + ndc.x * pc.cam_right.w * pc.cam_right.xyz
           - ndc.y * pc.cam_up.w    * pc.cam_up.xyz;
  return pc.cam_pos.xyz + ray * lz;
}

// Project world-space position back to screen UV. Returns vec3 where
// .xy is UV [0,1] and .z is non-linear depth (matches cd_depth).
vec3 world_to_uv(vec3 w) {
  vec3 rel = w - pc.cam_pos.xyz;
  float fwd_dot = dot(rel, pc.cam_fwd.xyz);
  if (fwd_dot <= 0.0) return vec3(-1.0);  // behind camera
  float right_dot = dot(rel, pc.cam_right.xyz);
  float up_dot    = dot(rel, pc.cam_up.xyz);
  float ndc_x = (right_dot / fwd_dot) / pc.cam_right.w;
  float ndc_y = (up_dot    / fwd_dot) / pc.cam_up.w;
  float uv_x  = ndc_x * 0.5 + 0.5;
  float uv_y  = -ndc_y * 0.5 + 0.5;
  // Encode non-linear depth from linear (matches linearize_z's inverse).
  float lz = fwd_dot;
  float d = (pc.ao.w - pc.ao.z * pc.ao.w / lz) / (pc.ao.w - pc.ao.z);
  return vec3(uv_x, uv_y, clamp(d, 0.0, 1.0));
}

// Same as world_to_uv but uses the previous frame's camera basis —
// for camera-velocity reprojection (TAA + motion blur). Returns
// vec2(-1) when the world point is behind the previous camera.
vec2 prev_world_to_uv(vec3 w) {
  vec3 rel = w - pc.prev_cam_pos.xyz;
  float fwd_dot = dot(rel, pc.prev_cam_fwd.xyz);
  if (fwd_dot <= 0.0) return vec2(-1.0);
  float right_dot = dot(rel, pc.prev_cam_right.xyz);
  float up_dot    = dot(rel, pc.prev_cam_up.xyz);
  float ndc_x = (right_dot / fwd_dot) / pc.prev_cam_right.w;
  float ndc_y = (up_dot    / fwd_dot) / pc.prev_cam_up.w;
  return vec2(ndc_x * 0.5 + 0.5, -ndc_y * 0.5 + 0.5);
}
layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 out_color;
// TAA history MRT — the next frame's read source. Composite always
// writes here so the pipeline stays valid even when TAA blend is 0.
layout(location = 1) out vec4 out_history;

float linearize_z(float d) {
  // Reverse-Z aware: protect against d == 0 (far plane returns NaN).
  // Standard perspective: z_view = (n*f) / (f - d*(f-n)).
  return pc.ao.z * pc.ao.w / max(pc.ao.w - d * (pc.ao.w - pc.ao.z), 1e-4);
}

// G-Buffer-aware horizon-scan AO. Same 8 ring samples around the
// centre as before, but now weights each occluder by the cosine
// between the surface normal and the world-space vector to the
// occluder. Samples in the back hemisphere of the surface (which
// can't possibly occlude — they're behind the surface plane) get
// zero weight. Closes the prior "AO darkens edges of sky" artifact.
float depth_ao(vec2 uv, float center_d) {
  if (center_d >= 0.999) return 1.0;  // sky pixel
  vec4 N_packed = texture(cd_gbuf_normal, uv);
  if (N_packed.w < 0.5) return 1.0;   // not a surface (sky / cleared)
  vec3 N = normalize(N_packed.xyz);
  vec3 wc = world_pos_from_uv(uv, center_d);
  float lc = linearize_z(center_d);
  vec2 px = 1.0 / vec2(textureSize(cd_depth, 0));
  vec2 ring[8] = vec2[8](
    vec2( 1.0,  0.0), vec2( 0.707,  0.707),
    vec2( 0.0,  1.0), vec2(-0.707,  0.707),
    vec2(-1.0,  0.0), vec2(-0.707, -0.707),
    vec2( 0.0, -1.0), vec2( 0.707, -0.707));
  float occ = 0.0;
  float weight_sum = 0.0;
  for (int i = 0; i < 8; ++i) {
    vec2 sp = uv + ring[i] * pc.ao.y * px;
    float nd = texture(cd_depth, sp).r;
    float ln = linearize_z(nd);
    float dz = lc - ln;
    float bias = 0.02 * lc;
    if (dz <= bias) continue;  // occluder behind centre — skip
    vec3 ws = world_pos_from_uv(sp, nd);
    vec3 dir = ws - wc;
    float dlen = length(dir);
    if (dlen < 1e-4) continue;
    dir /= dlen;
    float n_dot = max(dot(dir, N), 0.0);  // hemisphere weight
    float falloff = 1.0 / (1.0 + dlen * 2.0);
    occ += clamp((dz - bias) / 0.5, 0.0, 1.0) * n_dot * falloff;
    weight_sum += n_dot;
  }
  if (weight_sum < 1e-4) return 1.0;
  occ /= max(weight_sum, 1.0);
  return clamp(1.0 - occ, 0.0, 1.0);
}

// Screen-space ray-march reflection (Sousa 2011 SSR-lite). Given the
// surface point and its normal, reflect the view ray and march in
// 2D UV space (constant step) until either: a) depth at sample point
// is in front of march ray (=hit), or b) max-distance/step budget
// exhausted (=miss → 0 colour). Returns reflection HDR colour.
vec3 ssr_color(vec2 uv, vec3 wp, vec3 N) {
  if (pc.ssr.x <= 0.001) return vec3(0.0);
  vec3 V = normalize(pc.cam_pos.xyz - wp);
  vec3 R = reflect(-V, N);
  // March in WORLD space, project to UV per step.
  int max_steps = int(max(pc.ssr.z, 1.0));
  float max_dist = max(pc.ssr.y, 0.1);
  float step_size = max_dist / float(max_steps);
  for (int i = 1; i <= max_steps; ++i) {
    vec3 sample_wp = wp + R * step_size * float(i);
    vec3 sp = world_to_uv(sample_wp);
    if (sp.x < 0.0 || sp.x > 1.0 || sp.y < 0.0 || sp.y > 1.0 || sp.z < 0.0)
      return vec3(0.0);  // off-screen miss
    float scene_d = texture(cd_depth, sp.xy).r;
    float scene_lz = linearize_z(scene_d);
    float march_lz = linearize_z(sp.z);
    // Hit when march ray is past (deeper than) scene depth but within
    // a thickness tolerance. The tolerance scales with march step so
    // far samples don't miss high-frequency geometry.
    float thickness = step_size * 1.5;
    if (march_lz > scene_lz && (march_lz - scene_lz) < thickness) {
      // Edge fade — taper as the sample approaches screen edge.
      vec2 ec = abs(sp.xy - vec2(0.5)) * 2.0;
      float ef = clamp(1.0 - max(ec.x, ec.y) * pc.ssr.w, 0.0, 1.0);
      // Fresnel-ish boost at grazing angles.
      float NoV = max(dot(N, V), 0.0);
      float fresnel = pow(1.0 - NoV, 3.0);
      vec3 hit = texture(cd_hdr_color, sp.xy).rgb;
      return hit * pc.ssr.x * ef * (0.3 + fresnel * 0.7);
    }
  }
  return vec3(0.0);
}

// Chromatic aberration — radial RGB split. Strength grows with
// distance from screen centre (lens-style barrel), so the centre
// stays sharp. Single offset shared per channel pair.
vec3 sample_chromab(vec2 uv) {
  if (pc.lens.x <= 0.001) return texture(cd_hdr_color, uv).rgb;
  vec2 vc = uv - vec2(0.5);
  float r = length(vc);
  vec2 dir = (r > 1e-4) ? vc / r : vec2(0.0);
  vec2 px = 1.0 / vec2(textureSize(cd_hdr_color, 0));
  float offs = pc.lens.x * r * r * 8.0;
  vec3 c;
  c.r = texture(cd_hdr_color, uv + dir * offs * px).r;
  c.g = texture(cd_hdr_color, uv).g;
  c.b = texture(cd_hdr_color, uv - dir * offs * px).b;
  return c;
}

void main() {
  vec3 c = sample_chromab(v_uv);
  float center_d = texture(cd_depth, v_uv).r;

  // AO modulation — depth-only horizon scan. Applied to HDR before
  // bloom add so haloed pixels don't fight the darkening.
  float ao = depth_ao(v_uv, center_d);
  c *= mix(1.0, ao, clamp(pc.ao.x, 0.0, 1.0));

  // Atmospheric / aerial perspective — distant pixels tint toward the
  // sky horizon palette (matches AnalyticalSkyFS::sample_env). Two
  // independent dials: pc.atmo.x = exp-fog density (uniform haze),
  // pc.atmo.y = aerial perspective strength (Rayleigh-flavoured
  // wavelength shift toward bluish horizon). Sky pixels skip.
  if (center_d < 0.999 && (pc.atmo.x > 0.001 || pc.atmo.y > 0.001)) {
    float lz = linearize_z(center_d);
    // Horizon palette mixed with sun colour, matching sky FS.
    vec3 horizon_base = vec3(0.78, 0.86, 0.96);
    vec3 horizon_lit  = mix(horizon_base, pc.sun_col.rgb, 0.35);
    float fog_t = 1.0 - exp(-lz * max(pc.atmo.x, 0.0));
    float aer_t = 1.0 - exp(-lz * 0.08);  // soft built-in falloff
    c = mix(c, horizon_lit, clamp(fog_t, 0.0, 1.0));
    c = mix(c, horizon_lit, clamp(aer_t * pc.atmo.y, 0.0, 1.0));
  }

  // Depth-of-field — circle-of-confusion in linear-Z space. 8-tap
  // golden-spiral bokeh blur around the centre pixel; CoC grows
  // with abs(linear_z - focus) / range. Sky pixels skip (no blur).
  if (pc.dof.x > 0.001 && center_d < 0.999) {
    float lz = linearize_z(center_d);
    float coc = clamp(abs(lz - pc.dof.y) / max(pc.dof.z, 0.001),
                      0.0, 1.0);
    if (coc > 0.05) {
      vec2 px = 1.0 / vec2(textureSize(cd_hdr_color, 0));
      float r = coc * pc.dof.w;
      vec2 spiral[8] = vec2[8](
        vec2( 0.866,  0.500), vec2( 0.000,  1.000),
        vec2(-0.866,  0.500), vec2(-0.866, -0.500),
        vec2( 0.000, -1.000), vec2( 0.866, -0.500),
        vec2( 0.500,  0.000), vec2(-0.500,  0.000));
      vec3 dof_sum = vec3(0.0);
      for (int i = 0; i < 8; ++i) {
        dof_sum += texture(cd_hdr_color, v_uv + spiral[i] * r * px).rgb;
      }
      dof_sum *= (1.0 / 8.0);
      c = mix(c, dof_sum,
              smoothstep(0.05, 0.30, coc) * clamp(pc.dof.x, 0.0, 1.0));
    }
  }

  // Screen-space reflections — use G-Buffer normal at the centre
  // pixel; only run on real surface pixels. Reflection colour is
  // added to HDR before bloom so SSR-hit highlights can bloom.
  vec4 ssr_N = texture(cd_gbuf_normal, v_uv);
  if (pc.ssr.x > 0.001 && ssr_N.w > 0.5 && center_d < 0.999) {
    vec3 wp = world_pos_from_uv(v_uv, center_d);
    vec3 N  = normalize(ssr_N.xyz);
    c += ssr_color(v_uv, wp, N);
  }

  // Camera-velocity motion blur — reconstruct the pixel's world
  // position, reproject through the prev-frame camera basis to find
  // where it sat last frame, sample HDR along the screen-space
  // velocity vector. Object-motion velocity awaits the MRT velocity
  // G-Buffer (next phase); for now this captures every static-mesh
  // camera-motion-induced blur which is the dominant case.
  float mblur_strength = pc.prev_cam_fwd.w;
  if (mblur_strength > 0.001 && center_d < 0.999) {
    vec3 wp_now = world_pos_from_uv(v_uv, center_d);
    vec2 prev_uv = prev_world_to_uv(wp_now);
    if (prev_uv.x >= 0.0 && prev_uv.x <= 1.0 &&
        prev_uv.y >= 0.0 && prev_uv.y <= 1.0) {
      vec2 velocity = v_uv - prev_uv;
      // Clamp to reasonable max so a snap-cut doesn't smear across
      // the whole screen.
      float vlen = length(velocity);
      if (vlen > 0.001) {
        float vmax = 0.1;  // 10% of viewport per frame max
        if (vlen > vmax) velocity *= vmax / vlen;
        int   nsamples = int(max(pc.prev_cam_pos.w, 1.0));
        vec3  blur_sum = vec3(0.0);
        for (int i = 0; i < nsamples; ++i) {
          float t = float(i) / float(nsamples - 1) - 0.5;  // [-0.5, 0.5]
          vec2 sp = v_uv + velocity * t;
          blur_sum += texture(cd_hdr_color, clamp(sp, vec2(0.0), vec2(1.0))).rgb;
        }
        blur_sum *= (1.0 / float(nsamples));
        c = mix(c, blur_sum, clamp(mblur_strength, 0.0, 1.0));
      }
    }
  }

  // Light shafts (volumetric god rays) — Mitchell 2007 screen-space
  // occlusion shafts. March from current pixel toward the sun's
  // screen-space UV; sample depth at each step and accumulate
  // 'sky-through' density (depth == far). Add scaled sun colour to
  // the HDR sum before bloom so bright rays bloom.
  if (pc.shafts.z > 0.0) {
    vec2 to_sun = pc.shafts.xy - v_uv;
    float dist = length(to_sun);
    float density = 0.0;
    const int kShaftSteps = 16;
    for (int i = 0; i < kShaftSteps; ++i) {
      float t = float(i) / float(kShaftSteps - 1);
      vec2 sp = v_uv + to_sun * t;
      // Clamp to viewport to avoid sampling outside.
      if (sp.x < 0.0 || sp.x > 1.0 || sp.y < 0.0 || sp.y > 1.0) continue;
      float sd = texture(cd_depth, sp).r;
      // 'sky pass' contribution — far-plane depth means the ray
      // travels through open sky at that step (no occluder).
      density += smoothstep(0.995, 0.999, sd);
    }
    density *= (1.0 / float(kShaftSteps));
    float falloff = exp(-dist * max(pc.shafts.w, 0.001));
    c += pc.sun_col.rgb * density * falloff * pc.shafts.z;
  }

  // Bloom: additive halo from the upsample chain's final mip0.
  // pc.fx.w is the user-facing strength dial (0 disables completely).
  vec3 bloom = texture(cd_bloom_mip0, v_uv).rgb;
  c += bloom * max(pc.fx.w, 0.0);
  // Pre-tonemap exposure boost (applies to scene + bloom sum).
  c *= max(pc.fx.y, 0.001);

  int op = int(pc.fx.x + 0.5);
  if (op == 0) {
    const float a_ = 2.51, b_ = 0.03, c_ = 2.43, d_ = 0.59, e_ = 0.14;
    c = clamp((c * (a_*c + b_)) / (c * (c_*c + d_) + e_),
              vec3(0.0), vec3(1.0));
  } else if (op == 1) {
    vec3 a = c * (c + 0.0245786) - 0.000090537;
    vec3 b = c * (0.983729 * c + 0.4329510) + 0.238081;
    c = clamp(a / b, vec3(0.0), vec3(1.0));
  } else if (op == 2) {
    const float A = 0.15, B = 0.50, C = 0.10, D = 0.20, E = 0.02, F = 0.30, W = 11.2;
    vec3 cf = ((c * (A*c + C*B) + D*E) / (c * (A*c + B) + D*F)) - E/F;
    vec3 wf = vec3(((W * (A*W + C*B) + D*E) / (W * (A*W + B) + D*F)) - E/F);
    c = clamp(cf / wf, vec3(0.0), vec3(1.0));
  } else {
    const float kMinEv = -12.47393, kMaxEv = 4.026069;
    vec3 lg = clamp((log2(max(c, vec3(1e-10))) - vec3(kMinEv)) /
                    (kMaxEv - kMinEv), vec3(0.0), vec3(1.0));
    vec3 x2 = lg * lg;
    vec3 x4 = x2 * x2;
    c = clamp( 15.5  * x4 * x2 - 40.14 * x4 * lg + 31.96 * x4
             -  6.868 * x2 * lg + 0.4298 * x2 + 0.1191 * lg - 0.00232,
             vec3(0.0), vec3(1.0));
  }
  // Post-tonemap saturation pull-away.
  {
    float luma = dot(c, vec3(0.299, 0.587, 0.114));
    float sb = max(pc.fx.z, 0.001);
    c = clamp(mix(vec3(luma), c, sb), vec3(0.0), vec3(1.0));
  }
  c = pow(c, vec3(1.0/2.2));

  // Vignette — radial darkening from screen centre. Strength 0 = off.
  if (pc.atmo.z > 0.001) {
    vec2 vc = v_uv - vec2(0.5);
    float r2 = dot(vc, vc);
    float v = 1.0 - r2 * 4.0 * clamp(pc.atmo.z, 0.0, 1.0);
    c *= clamp(v, 0.0, 1.0);
  }

  // Film grain — hash-based per-pixel noise, anchored to screen
  // coordinates so it doesn't crawl across frames (still works as
  // texture-style grain). Strength 0 = off.
  if (pc.atmo.w > 0.001) {
    vec2 sp = v_uv * vec2(textureSize(cd_hdr_color, 0));
    float h = fract(sin(dot(sp, vec2(12.9898, 78.233))) * 43758.5453);
    c += (h - 0.5) * pc.atmo.w * 0.15;
  }

  // Temporal anti-aliasing — post-tonemap LDR blend with the
  // reprojected prior-frame LDR. Reprojection uses prev_world_to_uv
  // against the centre pixel's reconstructed world position
  // (camera-motion only; per-mesh motion needs the MRT velocity
  // G-Buffer next phase). pc.cam_fwd.w packs the blend alpha
  // (0 → no TAA, ~0.85 → strong accumulation). Disabled when alpha
  // ≤ 0.001 OR centre pixel is sky.
  float taa_alpha = clamp(pc.cam_fwd.w, 0.0, 0.97);
  if (taa_alpha > 0.001 && center_d < 0.999) {
    vec3 wp_taa = world_pos_from_uv(v_uv, center_d);
    vec2 prev_uv = prev_world_to_uv(wp_taa);
    if (prev_uv.x >= 0.0 && prev_uv.x <= 1.0 &&
        prev_uv.y >= 0.0 && prev_uv.y <= 1.0) {
      vec3 hist = texture(cd_history_prev, prev_uv).rgb;
      // Neighborhood clamp — sample 3x3 around the centre to find
      // the LDR colour cube; clamp history to that to prevent ghost
      // ing of disoccluded pixels (rough YCgCo clamp via min/max).
      vec2 px = 1.0 / vec2(textureSize(cd_history_prev, 0));
      vec3 lo = c, hi = c;
      for (int j = -1; j <= 1; ++j)
      for (int i = -1; i <= 1; ++i) {
        if (i == 0 && j == 0) continue;
        // Re-sample our just-computed c via cd_hdr_color is wrong
        // (raw HDR). Instead approximate neighbourhood with raw HDR
        // tonemap-less, which is a soft approximation — sufficient
        // for ghost suppression at this composite stage.
        vec3 n = texture(cd_hdr_color, v_uv + vec2(i, j) * px).rgb;
        lo = min(lo, n);
        hi = max(hi, n);
      }
      hist = clamp(hist, lo, hi);
      c = mix(c, hist, taa_alpha);
    }
  }

  out_color = vec4(c, 1.0);
  out_history = vec4(c, 1.0);  // feed next frame's TAA read
}
)glsl";

struct CompositePush
{
    float fx[4];        // x=tonemap_op, y=exposure, z=sat_boost, w=bloom_strength
    float ao[4];        // x=ao_strength, y=ao_radius_px, z=near, w=far
    float dof[4];       // x=dof_strength, y=focus_distance, z=focus_range, w=max_blur_px
    float shafts[4];    // x=sun_uv_x, y=sun_uv_y, z=strength (neg = sun behind), w=decay
    float sun_col[4];   // xyz=linear sun colour, w=reserved
    float atmo[4];      // x=fog_density, y=aerial_strength, z=vignette, w=film_grain
    float lens[4];      // x=chromatic_aberration_px, y=reserved, z=reserved, w=reserved
    // R3 G-Buffer-aware ops — camera basis lets composite reconstruct
    // world-space sample positions from screen UV + depth, enabling
    // proper SSR + normal-aware AO + future motion blur.
    float cam_right[4]; // xyz=world-space right basis, w=half_w (tan(fov/2)*aspect)
    float cam_up[4];    // xyz=world-space up basis,    w=half_h (tan(fov/2))
    float cam_fwd[4];   // xyz=world-space forward,     w=reserved
    float cam_pos[4];   // xyz=world camera origin,     w=reserved
    float ssr[4];       // x=ssr_strength, y=max_distance_m, z=max_steps, w=fade_edge
    // R3 camera-velocity reprojection — previous frame's camera basis
    // packed alongside motion-blur parameters. Used to compute per-
    // pixel screen-space velocity from camera motion alone (object-
    // motion velocity awaits the MRT velocity target).
    float prev_cam_right[4]; // xyz=prev right, w=prev_half_w
    float prev_cam_up[4];    // xyz=prev up,    w=prev_half_h
    float prev_cam_fwd[4];   // xyz=prev fwd,   w=mblur_strength
    float prev_cam_pos[4];   // xyz=prev pos,   w=mblur_samples (float, rounded)
};
static_assert(sizeof(CompositePush) == 256, "CompositePush layout");

// R3 — Multi-mip bloom (Karis 2013) — shader source + push struct
// definitions are extracted into cd::post_bloom. hello_engine just
// references them via the namespace.
using cd::post_bloom::kPrefilterFS;
using cd::post_bloom::kDownsampleFS;
using cd::post_bloom::kUpsampleFS;
using BloomPrefilterPush = cd::post_bloom::PrefilterPush;
using BloomUpsamplePush  = cd::post_bloom::UpsamplePush;

struct PrimPush
{
    cd::math::Mat4f mvp;
    cd::math::Mat4f model;
    float           tint[4];
    float           sun_dir[4];
    float           sun_color[4];
    // FX params block 1 — x=tonemap_op (0=Nark, 1=Hill, 2=Hable, 3=AGX)
    //                     y=albedo_tex_flag (1=sample cd_albedo_tex)
    //                     z=gtao_strength (inline curvature darkening)
    //                     w=bloom_strength (post-tonemap halo boost)
    float           fx_params[4];
    // FX params block 2 — x=smaa_strength (legacy inline FXAA blur)
    //                     y=motion_blur_amount (LIVE in composite — phase 215)
    //                     z=taa_amount (LIVE in composite — phase 216-217)
    //                     w=dof_strength (LIVE in composite — phase 207)
    float           fx_params2[4];
    // FX params block 3 — atmospherics (LIVE in composite — phase 209)
    //                     x=fog_density (legacy inline; composite owns now)
    //                     y=atmosphere_strength (legacy inline; composite owns)
    //                     z=clouds_coverage (queued — needs 3D noise sampler)
    //                     w=light_shafts_strength (LIVE in composite — phase 208)
    float           fx_params3[4];
    // Camera origin (needed for distance fog without breaking the model
    // matrix invariant). xyz=world camera, w=unused.
    float           camera_pos[4];
    // R6 advanced BRDF strengths:
    //   x=clearcoat (Filament second Schlick lobe on top of base spec)
    //   y=sheen (Charlie velvet rim term)
    //   z=sss (Burley wrap-diffusion approximation)
    //   w=reserved
    float           fx_params4[4];
};

static_assert(sizeof(PrimPush) == 256, "PrimPush layout drift");

// Multi-light UBO slot — matches std140 layout in the FS.
struct LightSlotGpu
{
    float pos_range[4];   // xyz=world position, w=range
    float dir_type[4];    // xyz=direction or right-basis, w=type as float
    float color_int[4];   // xyz=colour, w=intensity (scaled, ready for FS)
    float extras[4];      // x=cos_outer, y=area_w, z=area_h, w=cos_inner
};
static_assert(sizeof(LightSlotGpu) == 64, "LightSlotGpu must be 64 B");

struct LightUboGpu
{
    std::uint32_t count;
    std::uint32_t pad[3];
    LightSlotGpu  slots[8];
};
static_assert(sizeof(LightUboGpu) == 16 + 8 * 64, "LightUboGpu must be 528 B");

// Upload an RGBA8 image to a freshly-created GPU texture. Returns
// invalid handles on failure. Lifetime: caller owns the texture +
// view + sampler; destroy at exit. Used by the default-white
// fallback and the glTF baseColor path (#1/#13).
struct GpuTexture2D
{
    cd::rhi::TextureHandle     image {};
    cd::rhi::TextureViewHandle view  {};
};

[[nodiscard]] inline GpuTexture2D
create_texture_rgba8(cd::rhi::IDevice& dev,
                     const std::uint8_t* rgba,
                     std::uint32_t w,
                     std::uint32_t h)
{
    GpuTexture2D out {};
    if (rgba == nullptr || w == 0 || h == 0) return out;
    cd::rhi::TextureDesc td {};
    td.type = cd::rhi::TextureType::k2D;
    td.format = cd::rhi::Format::kRGBA8Unorm;
    td.extent = { w, h, 1 };
    td.mip_levels = 1;
    td.array_layers = 1;
    td.usage = cd::rhi::TextureUsage::kSampled | cd::rhi::TextureUsage::kTransferDst;
    td.memory = cd::rhi::MemoryUsage::kGpuOnly;
    auto img = dev.create_texture(td);
    if (!img.has_value()) return out;
    out.image = *img;
    // Staging buffer upload.
    const std::size_t bytes = static_cast<std::size_t>(w) * h * 4;
    cd::rhi::BufferDesc sd {};
    sd.size = bytes;
    sd.usage = cd::rhi::BufferUsage::kTransferSrc;
    sd.memory = cd::rhi::MemoryUsage::kCpuToGpu;
    auto staging_r = dev.create_buffer(sd);
    if (!staging_r.has_value()) return out;
    const auto staging = *staging_r;
    (void)dev.upload_buffer(staging, 0,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(rgba), bytes));
    auto cmd = dev.create_command_buffer(cd::rhi::QueueType::kGraphics);
    if (cmd == nullptr) { dev.destroy_buffer(staging); return out; }
    cmd->begin();
    std::array<cd::rhi::TextureBarrier, 1> tb_dst { cd::rhi::TextureBarrier {
        .texture = out.image,
        .from = cd::rhi::ResourceState::kUndefined,
        .to   = cd::rhi::ResourceState::kTransferDst,
        .range = { 0, 1, 0, 1 } } };
    cmd->barrier({}, tb_dst);
    std::array<cd::rhi::BufferImageCopyRegion, 1> regs { cd::rhi::BufferImageCopyRegion {
        .buffer_offset = 0, .mip_level = 0, .base_layer = 0, .layer_count = 1,
        .image_offset = { 0, 0, 0 }, .image_extent = { w, h, 1 } } };
    cmd->copy_buffer_to_image(staging, out.image, regs);
    std::array<cd::rhi::TextureBarrier, 1> tb_read { cd::rhi::TextureBarrier {
        .texture = out.image,
        .from = cd::rhi::ResourceState::kTransferDst,
        .to   = cd::rhi::ResourceState::kShaderResource,
        .range = { 0, 1, 0, 1 } } };
    cmd->barrier({}, tb_read);
    cmd->end();
    cd::rhi::SubmitDesc sub {};
    std::array<cd::rhi::ICommandBuffer*, 1> cbs { cmd.get() };
    sub.command_buffers = cbs;
    (void)dev.submit(sub);
    dev.wait_idle();
    dev.destroy_buffer(staging);
    cd::rhi::TextureViewDesc vd {};
    vd.texture = out.image;
    vd.type = cd::rhi::TextureType::k2D;
    vd.format = cd::rhi::Format::kRGBA8Unorm;
    vd.base_mip = 0; vd.mip_count = 1;
    vd.base_layer = 0; vd.layer_count = 1;
    auto v = dev.create_texture_view(vd);
    if (!v.has_value()) { dev.destroy_texture(out.image); out.image = {}; return out; }
    out.view = *v;
    return out;
}

// ============================================================================
// R1 — True IBL helpers (HDR cubemap + diffuse irradiance + BRDF LUT).
//
// Generates a CPU environment cubemap by sampling the analytical sky
// function (same palette as AnalyticalSkyMaterial::sample_env), runs
// cd::ibl convolutions (irradiance + prefiltered specular + BRDF LUT),
// and uploads to GPU as kCube + kCube-with-mips + k2D textures.
// ============================================================================


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
// R3 — HDR off-screen color target.
//
// RGBA16F render target that the scene draws into instead of the swap-
// chain. A separate composite pass samples it, applies bloom + tonemap
// + post-fx, then writes to the swapchain. Foundation for the v1.7
// frame-graph rework.
// ============================================================================
struct ColorTarget
{
    cd::rhi::TextureHandle     image {};
    cd::rhi::TextureViewHandle view  {};
    cd::rhi::Extent2D          extent {};
    cd::rhi::Format            format { cd::rhi::Format::kRGBA16Float };
    void destroy(cd::rhi::IDevice& dev)
    {
        if (view.is_valid())  dev.destroy_texture_view(view);
        if (image.is_valid()) dev.destroy_texture(image);
        *this = {};
    }
};

[[nodiscard]] inline bool
create_color_target(cd::rhi::IDevice& dev, cd::rhi::Extent2D size,
                    cd::rhi::Format format, ColorTarget& out)
{
    out.destroy(dev);
    cd::rhi::TextureDesc td {};
    td.type = cd::rhi::TextureType::k2D;
    td.format = format;
    td.extent = { size.width, size.height, 1 };
    td.mip_levels = 1;
    td.array_layers = 1;
    td.usage  = cd::rhi::TextureUsage::kColorAttachment |
                cd::rhi::TextureUsage::kSampled |
                cd::rhi::TextureUsage::kStorage;  // for compute bloom/AO
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
    if (!v.has_value()) { dev.destroy_texture(*img); return false; }
    out.image  = *img;
    out.view   = *v;
    out.extent = size;
    out.format = format;
    return true;
}

// ============================================================================
// R3 — Multi-mip bloom render-target chain.
//
// 4 progressively halving RGBA16Float ColorTargets. mip0 is full-screen
// / 2; mip3 is /16. Each level acts both as a write destination
// (downsample pass / upsample additive blend) and as a read source for
// the next level in the chain. The composite pass samples mip0 and
// adds it to the HDR scene before tonemap.
// ============================================================================
struct BloomMipChain
{
    static constexpr std::uint32_t kCount = 4;
    std::array<ColorTarget, kCount> mips {};
    void destroy(cd::rhi::IDevice& dev)
    {
        for (auto& m : mips) m.destroy(dev);
    }
};

[[nodiscard]] inline bool
create_bloom_chain(cd::rhi::IDevice& dev, cd::rhi::Extent2D base, BloomMipChain& out)
{
    out.destroy(dev);
    cd::rhi::Extent2D s { std::max(1U, base.width / 2U),
                          std::max(1U, base.height / 2U) };
    for (std::uint32_t i = 0; i < BloomMipChain::kCount; ++i)
    {
        if (!create_color_target(dev, s, cd::rhi::Format::kRGBA16Float, out.mips[i]))
        {
            out.destroy(dev);
            return false;
        }
        s.width  = std::max(1U, s.width  / 2U);
        s.height = std::max(1U, s.height / 2U);
    }
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
    // kSampled — needed for the composite-pass GTAO inline AO that
    // samples the scene depth after the HDR pass ends.
    if (!create_depth_target(device, { window.width(), window.height() }, kDepthFormat, depth,
                             cd::rhi::TextureUsage::kSampled))
        return 6;
    bool depth_initialised_on_gpu = false;

    // R3 — HDR offscreen color target. Scene + sky + UI overlay all
    // draw into this RGBA16F target; a separate composite pass blits
    // it to the swapchain with tonemap + saturation correction.
    constexpr auto kHdrFormat = cd::rhi::Format::kRGBA16Float;
    ColorTarget hdr_target {};
    if (!create_color_target(device, { window.width(), window.height() }, kHdrFormat, hdr_target))
        return 31;

    // R3 G-Buffer foundation — world-space surface normal target.
    // Every scene FS (prim, PBR, sky) MRT-writes its world-space
    // normal here so downstream post-fx (SSR, GTAO with normals,
    // future reflections) can sample it. RGBA16F encodes the
    // 3-component normal directly (xyz) + a flag in w (1 = surface,
    // 0 = sky / no surface). Recreated on swapchain rebuild.
    constexpr auto kNormalFormat = cd::rhi::Format::kRGBA16Float;
    ColorTarget gbuf_normal {};
    if (!create_color_target(device, { window.width(), window.height() }, kNormalFormat, gbuf_normal))
        return 47;

    // R3 G-Buffer phase 219 — Albedo + MR (metallic / roughness).
    // Unlocks proper deferred shading + SSR colour-tint by surface
    // properties + future GI integration. Pixel cost ≈ 5 B per pixel.
    constexpr auto kAlbedoFormat = cd::rhi::Format::kRGBA8Unorm;
    constexpr auto kMrFormat     = cd::rhi::Format::kRG8Unorm;
    ColorTarget gbuf_albedo {};
    if (!create_color_target(device, { window.width(), window.height() }, kAlbedoFormat, gbuf_albedo))
        return 49;
    ColorTarget gbuf_mr {};
    if (!create_color_target(device, { window.width(), window.height() }, kMrFormat, gbuf_mr))
        return 50;

    // R3 TAA history — ping-pong color targets at swapchain format.
    // Each frame, composite reads history[frame & 1] (last frame's
    // post-tonemap blend) and writes to history[(frame & 1) ^ 1]
    // (this frame's blend, for next frame). MRT 2nd attachment in
    // the composite render pass.
    constexpr auto kHistoryFormat = cd::rhi::Format::kBGRA8Unorm;
    std::array<ColorTarget, 2> history_targets {};
    for (auto& h : history_targets)
    {
        if (!create_color_target(device, { window.width(), window.height() }, kHistoryFormat, h))
            return 48;
    }

    // R3 phase 219: scene materials MRT-write 4 targets:
    //   location 0: HDR colour (RGBA16F)
    //   location 1: world-space normal + surface-flag (RGBA16F)
    //   location 2: albedo + material-flag (RGBA8Unorm)
    //   location 3: metallic + roughness (RG8Unorm)
    // Every scene pipeline shares this layout so attachment layout
    // matches the HDR pass's begin_render_pass.
    constexpr std::array<cd::rhi::Format, 4> kColorFmts {
        cd::rhi::Format::kRGBA16Float,
        cd::rhi::Format::kRGBA16Float,
        cd::rhi::Format::kRGBA8Unorm,
        cd::rhi::Format::kRG8Unorm };
    // (composite uses kCompositeFmts [swapchain + history] declared below;
    //  this single-slot kSwapchainFmts is preserved for symmetry / docs.)
    [[maybe_unused]] constexpr std::array<cd::rhi::Format, 1> kSwapchainFmts {
        cd::rhi::Format::kBGRA8Unorm };

    // Sky material — no vertex buffer, depth off.
    cd::material::MaterialDesc sky_md {};
    sky_md.vertex_glsl   = cd::material::kAnalyticalSkyVS;
    sky_md.fragment_glsl = cd::material::kAnalyticalSkyFS;
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

    // R3 composite material — full-screen triangle, samples HDR target,
    // writes to swapchain. Carries the tonemap + saturation pass that
    // previously lived inline in prim/PBR FS.
    // Composite writes BOTH to the swapchain (final tonemapped LDR
    // for display) AND to a 2nd target = next-frame TAA history.
    constexpr std::array<cd::rhi::Format, 2> kCompositeFmts {
        cd::rhi::Format::kBGRA8Unorm,  // swapchain — visible output
        cd::rhi::Format::kBGRA8Unorm   // history target — for TAA next frame
    };
    cd::material::MaterialDesc comp_md {};
    comp_md.vertex_glsl   = kCompositeVS;
    comp_md.fragment_glsl = kCompositeFS;
    comp_md.color_attachment_formats = kCompositeFmts;
    constexpr std::array<cd::rhi::PushConstantRange, 1> kCompositePush {
        cd::rhi::PushConstantRange { .stages = cd::rhi::ShaderStage::kFragment,
                                     .offset = 0,
                                     .size = sizeof(CompositePush) } };
    comp_md.push_constants = kCompositePush;
    constexpr std::array<cd::rhi::DescriptorSetLayoutBinding, 5> kCompositeBindings {
        cd::rhi::DescriptorSetLayoutBinding {
            .binding = 0,
            .type    = cd::rhi::DescriptorType::kCombinedImageSampler,
            .count   = 1,
            .stages  = cd::rhi::ShaderStage::kFragment },
        cd::rhi::DescriptorSetLayoutBinding {
            .binding = 1,
            .type    = cd::rhi::DescriptorType::kCombinedImageSampler,
            .count   = 1,
            .stages  = cd::rhi::ShaderStage::kFragment },
        cd::rhi::DescriptorSetLayoutBinding {
            .binding = 2,
            .type    = cd::rhi::DescriptorType::kCombinedImageSampler,
            .count   = 1,
            .stages  = cd::rhi::ShaderStage::kFragment },
        cd::rhi::DescriptorSetLayoutBinding {
            .binding = 3,
            .type    = cd::rhi::DescriptorType::kCombinedImageSampler,
            .count   = 1,
            .stages  = cd::rhi::ShaderStage::kFragment },
        cd::rhi::DescriptorSetLayoutBinding {
            .binding = 4,
            .type    = cd::rhi::DescriptorType::kCombinedImageSampler,
            .count   = 1,
            .stages  = cd::rhi::ShaderStage::kFragment } };
    comp_md.descriptor_bindings = kCompositeBindings;
    comp_md.raster.cull = cd::rhi::CullMode::kNone;
    comp_md.depth_stencil.depth_test = false;
    comp_md.depth_stencil.depth_write = false;
    comp_md.name = "hello_engine/composite";
    auto comp_r = cd::material::Material::create(device, compiler.get(), comp_md);
    if (!comp_r.has_value()) return 32;
    auto& composite_material = *comp_r;

    // R3 Multi-mip Bloom — Karis stable pipeline.
    // 3 fullscreen-triangle materials sharing the composite VS.
    // Color attachment format = RGBA16Float so HDR mip chain preserves
    // overshoot through prefilter -> downsample -> upsample.
    constexpr std::array<cd::rhi::Format, 1> kHdrFmts { kHdrFormat };
    constexpr std::array<cd::rhi::DescriptorSetLayoutBinding, 1> kBloomBindings {
        cd::rhi::DescriptorSetLayoutBinding {
            .binding = 0,
            .type    = cd::rhi::DescriptorType::kCombinedImageSampler,
            .count   = 1,
            .stages  = cd::rhi::ShaderStage::kFragment } };
    constexpr std::array<cd::rhi::PushConstantRange, 1> kBloomPrefilterPushRange {
        cd::rhi::PushConstantRange { .stages = cd::rhi::ShaderStage::kFragment,
                                     .offset = 0,
                                     .size = sizeof(BloomPrefilterPush) } };
    constexpr std::array<cd::rhi::PushConstantRange, 1> kBloomUpsamplePushRange {
        cd::rhi::PushConstantRange { .stages = cd::rhi::ShaderStage::kFragment,
                                     .offset = 0,
                                     .size = sizeof(BloomUpsamplePush) } };

    cd::material::MaterialDesc bp_md {};
    bp_md.vertex_glsl   = kCompositeVS;
    bp_md.fragment_glsl = std::string_view { kPrefilterFS };
    bp_md.color_attachment_formats = kHdrFmts;
    bp_md.push_constants    = kBloomPrefilterPushRange;
    bp_md.descriptor_bindings = kBloomBindings;
    bp_md.raster.cull = cd::rhi::CullMode::kNone;
    bp_md.depth_stencil.depth_test = false;
    bp_md.depth_stencil.depth_write = false;
    bp_md.name = "hello_engine/bloom/prefilter";
    auto bp_r = cd::material::Material::create(device, compiler.get(), bp_md);
    if (!bp_r.has_value()) return 40;
    auto& bloom_prefilter_material = *bp_r;

    cd::material::MaterialDesc bd_md {};
    bd_md.vertex_glsl   = kCompositeVS;
    bd_md.fragment_glsl = std::string_view { kDownsampleFS };
    bd_md.color_attachment_formats = kHdrFmts;
    bd_md.descriptor_bindings = kBloomBindings;
    bd_md.raster.cull = cd::rhi::CullMode::kNone;
    bd_md.depth_stencil.depth_test = false;
    bd_md.depth_stencil.depth_write = false;
    bd_md.name = "hello_engine/bloom/downsample";
    auto bd_r = cd::material::Material::create(device, compiler.get(), bd_md);
    if (!bd_r.has_value()) return 41;
    auto& bloom_downsample_material = *bd_r;

    cd::material::MaterialDesc bu_md {};
    bu_md.vertex_glsl   = kCompositeVS;
    bu_md.fragment_glsl = std::string_view { kUpsampleFS };
    bu_md.color_attachment_formats = kHdrFmts;
    bu_md.push_constants    = kBloomUpsamplePushRange;
    bu_md.descriptor_bindings = kBloomBindings;
    bu_md.raster.cull = cd::rhi::CullMode::kNone;
    // Additive blend so up-chain sum accumulates onto the previous mip.
    constexpr std::array<cd::rhi::BlendAttachmentState, 1> kBloomUpsampleBlend {
        cd::rhi::BlendAttachmentState {
            .blend_enable = true,
            .src_color = cd::rhi::BlendFactor::kOne,
            .dst_color = cd::rhi::BlendFactor::kOne,
            .color_op  = cd::rhi::BlendOp::kAdd,
            .src_alpha = cd::rhi::BlendFactor::kOne,
            .dst_alpha = cd::rhi::BlendFactor::kOne,
            .alpha_op  = cd::rhi::BlendOp::kAdd } };
    bu_md.blend_attachments = kBloomUpsampleBlend;
    bu_md.depth_stencil.depth_test = false;
    bu_md.depth_stencil.depth_write = false;
    bu_md.name = "hello_engine/bloom/upsample";
    auto bu_r = cd::material::Material::create(device, compiler.get(), bu_md);
    if (!bu_r.has_value()) return 42;
    auto& bloom_upsample_material = *bu_r;

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
    // PBR shader bindings (R1 IBL pipeline):
    //   binding 0: multi-light UBO (LightUboGpu, 528 B std140)
    //   binding 1: samplerCube — prefiltered specular IBL (mip chain)
    //   binding 2: samplerCube — diffuse irradiance IBL
    //   binding 3: sampler2D   — split-sum BRDF LUT (RG16Float)
    constexpr std::array<cd::rhi::DescriptorSetLayoutBinding, 4> kPbrDescBindings {
        cd::rhi::DescriptorSetLayoutBinding { .binding = 0,
                                              .type    = cd::rhi::DescriptorType::kUniformBuffer,
                                              .count   = 1,
                                              .stages  = cd::rhi::ShaderStage::kFragment },
        cd::rhi::DescriptorSetLayoutBinding { .binding = 1,
                                              .type    = cd::rhi::DescriptorType::kCombinedImageSampler,
                                              .count   = 1,
                                              .stages  = cd::rhi::ShaderStage::kFragment },
        cd::rhi::DescriptorSetLayoutBinding { .binding = 2,
                                              .type    = cd::rhi::DescriptorType::kCombinedImageSampler,
                                              .count   = 1,
                                              .stages  = cd::rhi::ShaderStage::kFragment },
        cd::rhi::DescriptorSetLayoutBinding { .binding = 3,
                                              .type    = cd::rhi::DescriptorType::kCombinedImageSampler,
                                              .count   = 1,
                                              .stages  = cd::rhi::ShaderStage::kFragment } };
    cd::material::MaterialDesc pbr_md {};
    pbr_md.vertex_glsl   = cd::material::kStandardPbrVS;
    pbr_md.fragment_glsl = cd::material::kStandardPbrFS;
    pbr_md.color_attachment_formats = kColorFmts;
    pbr_md.depth_attachment_format = kDepthFormat;
    pbr_md.vertex_bindings = kPbrBindings;
    pbr_md.vertex_attributes = kPbrAttrs;
    pbr_md.push_constants = kPbrPush;
    pbr_md.descriptor_bindings = kPbrDescBindings;
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
    constexpr std::array<cd::rhi::DescriptorSetLayoutBinding, 10> kPrimDescBindings {
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
                                              .stages  = cd::rhi::ShaderStage::kFragment },
        cd::rhi::DescriptorSetLayoutBinding { .binding = 3,
                                              .type    = cd::rhi::DescriptorType::kUniformBuffer,
                                              .count   = 1,
                                              .stages  = cd::rhi::ShaderStage::kFragment },
        // gap #1/#13 — baseColor texture slot for glTF entities.
        cd::rhi::DescriptorSetLayoutBinding { .binding = 4,
                                              .type    = cd::rhi::DescriptorType::kCombinedImageSampler,
                                              .count   = 1,
                                              .stages  = cd::rhi::ShaderStage::kFragment },
        // R2: IBL on the prim pipeline so textured kGltf entities
        // (CesiumMan, procedural Earth, torus knot) get reflections.
        cd::rhi::DescriptorSetLayoutBinding { .binding = 5,
                                              .type    = cd::rhi::DescriptorType::kCombinedImageSampler,
                                              .count   = 1,
                                              .stages  = cd::rhi::ShaderStage::kFragment },
        cd::rhi::DescriptorSetLayoutBinding { .binding = 6,
                                              .type    = cd::rhi::DescriptorType::kCombinedImageSampler,
                                              .count   = 1,
                                              .stages  = cd::rhi::ShaderStage::kFragment },
        cd::rhi::DescriptorSetLayoutBinding { .binding = 7,
                                              .type    = cd::rhi::DescriptorType::kCombinedImageSampler,
                                              .count   = 1,
                                              .stages  = cd::rhi::ShaderStage::kFragment },
        // R2: procedural normal map (tangent-space bump).
        cd::rhi::DescriptorSetLayoutBinding { .binding = 8,
                                              .type    = cd::rhi::DescriptorType::kCombinedImageSampler,
                                              .count   = 1,
                                              .stages  = cd::rhi::ShaderStage::kFragment },
        // R2: metallic-roughness-AO map (glTF 2.0 packing).
        cd::rhi::DescriptorSetLayoutBinding { .binding = 9,
                                              .type    = cd::rhi::DescriptorType::kCombinedImageSampler,
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

    // ---- Multi-light UBO (gap #2 + #3 foundation) ----
    // 8 non-sun lights * 64 bytes per slot + 16-byte header = 528 B.
    // std140 layout: each vec4 = 16-byte aligned.
    //   header: uint count + 3 uint pad
    //   slot:   vec4 pos_range
    //           vec4 dir_type     (xyz=dir for spot/dir / right-basis for area; w=type as float)
    //           vec4 color_int    (xyz=linear colour, w=intensity)
    //           vec4 extras       (x=cos_outer for spot, y=area_w, z=area_h, w=cos_inner)
    constexpr std::uint32_t kMaxLights      = 8;
    constexpr std::uint32_t kLightSlotBytes = 64;
    constexpr std::uint32_t kLightUboBytes  = 16 + kMaxLights * kLightSlotBytes;  // 528
    cd::rhi::BufferDesc lights_ubo_desc {};
    lights_ubo_desc.size   = kLightUboBytes;
    lights_ubo_desc.usage  = cd::rhi::BufferUsage::kUniform;
    lights_ubo_desc.memory = cd::rhi::MemoryUsage::kCpuToGpu;
    auto lights_ubo_r = device.create_buffer(lights_ubo_desc);
    if (!lights_ubo_r.has_value()) return 16;
    const auto lights_ubo = *lights_ubo_r;

    // ---- R1: IBL bake + GPU upload ----
    // CPU-side bake at startup: analytical-sky env cube -> diffuse
    // irradiance + prefiltered specular + BRDF LUT. Vulkan upload
    // creates kCube/k2D textures + clamp-to-edge sampler.
    // Resolutions chosen for first-ship balance (bake < 2 s on a
    // desktop CPU): env 128, spec mips 64..2, diff 16, BRDF 64x64.
    std::fprintf(stderr, "[ibl] baking environment cubemap...\n");
    const auto env_cube_cpu = cd::ibl::bake_sky_cube(128, cd::material::sample_sky_cpu);
    std::fprintf(stderr, "[ibl] convolving diffuse irradiance...\n");
    const auto diff_cube_cpu = cd::ibl::convolve_irradiance(env_cube_cpu, 16, 10.0F);
    std::fprintf(stderr, "[ibl] prefiltering specular mip chain...\n");
    const auto spec_cube_cpu = cd::ibl::prefilter_specular(env_cube_cpu, 64, 6, 32);
    std::fprintf(stderr, "[ibl] baking BRDF LUT...\n");
    const auto brdf_lut_cpu  = cd::ibl::bake_brdf_lut(64, 64, 256);
    std::fprintf(stderr, "[ibl] uploading to GPU...\n");
    const auto gpu_spec_cube = cd::ibl_gpu::upload_prefiltered_specular(device, spec_cube_cpu);
    const auto gpu_diff_cube = cd::ibl_gpu::upload_cubemap_rgba16f(device, diff_cube_cpu);
    const auto gpu_brdf_lut  = cd::ibl_gpu::upload_brdf_lut(device, brdf_lut_cpu);
    std::fprintf(stderr, "[ibl] done (spec %u mips, diff 16, brdf 64x64)\n",
                 gpu_spec_cube.mip_count);

    cd::rhi::SamplerDesc ibl_sd {};
    ibl_sd.mag_filter = cd::rhi::SamplerFilter::kLinear;
    ibl_sd.min_filter = cd::rhi::SamplerFilter::kLinear;
    ibl_sd.mipmap_mode = cd::rhi::SamplerMipmapMode::kLinear;
    ibl_sd.address_u = cd::rhi::SamplerAddressMode::kClampToEdge;
    ibl_sd.address_v = cd::rhi::SamplerAddressMode::kClampToEdge;
    ibl_sd.address_w = cd::rhi::SamplerAddressMode::kClampToEdge;
    ibl_sd.max_lod  = static_cast<float>(gpu_spec_cube.mip_count);
    auto ibl_samp_r = device.create_sampler(ibl_sd);
    if (!ibl_samp_r.has_value()) return 23;
    const auto ibl_sampler = *ibl_samp_r;

    // ---- glTF baseColor texture (#1/#13) ----
    // R1.5 showcase: generate a procedural Earth-like albedo texture
    // at startup so hello_engine demonstrates the textured-PBR path
    // even without an external glTF asset. The texture is replaced
    // later if a glTF auto-load resolves an asset with a baseColor
    // map. Procedural pattern: lat/lon-based ocean/continent mask +
    // smooth value noise + warm continent tint + cool ocean tint.
    GpuTexture2D albedo_tex {};
    bool         has_gltf_texture = false;
    {
        constexpr std::uint32_t kTexSize = 512;
        const auto rgba = cd::texture_synth::bake_earth_albedo_rgba8(kTexSize);
        albedo_tex = create_texture_rgba8(device, rgba.data(), kTexSize, kTexSize);
        has_gltf_texture = true;
        std::fprintf(stderr, "[showcase] procedural Earth-like albedo "
                              "(%ux%u) bound\n", kTexSize, kTexSize);
    }

    // R2: procedural normal map derived from a height field — same
    // fBm Earth surface but stored as tangent-space normals.
    GpuTexture2D normal_tex {};
    {
        constexpr std::uint32_t kNormalSize = 512;
        const auto nrm = cd::texture_synth::bake_earth_normal_rgba8(kNormalSize);
        normal_tex = create_texture_rgba8(device, nrm.data(), kNormalSize, kNormalSize);
        std::fprintf(stderr, "[showcase] procedural normal map (%ux%u) bound\n",
                     kNormalSize, kNormalSize);
    }

    // R2: metallic-roughness-AO map (glTF 2.0 packing — R unused,
    // G roughness, B metallic, A AO).
    GpuTexture2D mr_tex {};
    {
        constexpr std::uint32_t kMrSize = 256;
        const auto mr = cd::texture_synth::bake_earth_mr_rgba8(kMrSize);
        mr_tex = create_texture_rgba8(device, mr.data(), kMrSize, kMrSize);
        std::fprintf(stderr, "[showcase] procedural metallic-roughness "
                              "(%ux%u) bound\n", kMrSize, kMrSize);
    }
    cd::rhi::SamplerDesc albedo_sd {};
    albedo_sd.mag_filter = cd::rhi::SamplerFilter::kLinear;
    albedo_sd.min_filter = cd::rhi::SamplerFilter::kLinear;
    albedo_sd.mipmap_mode = cd::rhi::SamplerMipmapMode::kLinear;
    albedo_sd.address_u = cd::rhi::SamplerAddressMode::kRepeat;
    albedo_sd.address_v = cd::rhi::SamplerAddressMode::kRepeat;
    albedo_sd.address_w = cd::rhi::SamplerAddressMode::kRepeat;
    auto albedo_samp_r = device.create_sampler(albedo_sd);
    if (!albedo_samp_r.has_value()) return 19;
    const auto albedo_sampler = *albedo_samp_r;

    auto prim_inst_r = cd::material::MaterialInstance::create(device, prim_material);
    if (!prim_inst_r.has_value()) return 14;
    auto& prim_inst = *prim_inst_r;
    {
        std::array<cd::rhi::DescriptorWrite, 9> writes {
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
                                       .sampler = shadow_sampler },
            cd::rhi::DescriptorWrite { .binding = 3,
                                       .array_element = 0,
                                       .type = cd::rhi::DescriptorType::kUniformBuffer,
                                       .buffer = lights_ubo,
                                       .buffer_offset = 0,
                                       .buffer_range = kLightUboBytes },
            cd::rhi::DescriptorWrite { .binding = 4,
                                       .array_element = 0,
                                       .type = cd::rhi::DescriptorType::kCombinedImageSampler,
                                       .view = albedo_tex.view,
                                       .sampler = albedo_sampler },
            // R2: IBL (prefiltered spec + diffuse irradiance + BRDF LUT)
            // shared with the PBR pipeline so textured prim entities
            // (CesiumMan, Earth showcase) get true reflections.
            cd::rhi::DescriptorWrite { .binding = 5,
                                       .array_element = 0,
                                       .type    = cd::rhi::DescriptorType::kCombinedImageSampler,
                                       .view    = gpu_spec_cube.view,
                                       .sampler = ibl_sampler },
            cd::rhi::DescriptorWrite { .binding = 6,
                                       .array_element = 0,
                                       .type    = cd::rhi::DescriptorType::kCombinedImageSampler,
                                       .view    = gpu_diff_cube.view,
                                       .sampler = ibl_sampler },
            cd::rhi::DescriptorWrite { .binding = 7,
                                       .array_element = 0,
                                       .type    = cd::rhi::DescriptorType::kCombinedImageSampler,
                                       .view    = gpu_brdf_lut.view,
                                       .sampler = ibl_sampler },
            // R2: procedural normal map for textured entities.
            cd::rhi::DescriptorWrite { .binding = 8,
                                       .array_element = 0,
                                       .type    = cd::rhi::DescriptorType::kCombinedImageSampler,
                                       .view    = normal_tex.view,
                                       .sampler = albedo_sampler },
            // R2: metallic-roughness-AO map.
            cd::rhi::DescriptorWrite { .binding = 9,
                                       .array_element = 0,
                                       .type    = cd::rhi::DescriptorType::kCombinedImageSampler,
                                       .view    = mr_tex.view,
                                       .sampler = albedo_sampler } };
        if (auto wr = prim_inst.update(writes); !wr.has_value()) return 15;
    }

    // PBR material instance — bindings:
    //   0 multi-light UBO (gap #22), 1 spec IBL, 2 diff IBL, 3 BRDF LUT.
    auto pbr_inst_r = cd::material::MaterialInstance::create(device, pbr_material);
    if (!pbr_inst_r.has_value()) return 17;
    auto& pbr_inst = *pbr_inst_r;
    {
        std::array<cd::rhi::DescriptorWrite, 4> writes {
            cd::rhi::DescriptorWrite { .binding = 0,
                                       .array_element = 0,
                                       .type  = cd::rhi::DescriptorType::kUniformBuffer,
                                       .buffer = lights_ubo,
                                       .buffer_offset = 0,
                                       .buffer_range = kLightUboBytes },
            cd::rhi::DescriptorWrite { .binding = 1,
                                       .array_element = 0,
                                       .type    = cd::rhi::DescriptorType::kCombinedImageSampler,
                                       .view    = gpu_spec_cube.view,
                                       .sampler = ibl_sampler },
            cd::rhi::DescriptorWrite { .binding = 2,
                                       .array_element = 0,
                                       .type    = cd::rhi::DescriptorType::kCombinedImageSampler,
                                       .view    = gpu_diff_cube.view,
                                       .sampler = ibl_sampler },
            cd::rhi::DescriptorWrite { .binding = 3,
                                       .array_element = 0,
                                       .type    = cd::rhi::DescriptorType::kCombinedImageSampler,
                                       .view    = gpu_brdf_lut.view,
                                       .sampler = ibl_sampler } };
        if (auto wr = pbr_inst.update(writes); !wr.has_value()) return 18;
    }

    // R3: composite material instance + HDR sampler binding. Two
    // instances for TAA ping-pong — composite_insts[i] reads
    // history_targets[i] (= the OPPOSITE target from what it writes
    // this frame, so the read history was produced by the prior frame).
    std::array<cd::material::MaterialInstance, 2> composite_insts {};
    for (std::uint32_t i = 0; i < 2; ++i)
    {
        auto r = cd::material::MaterialInstance::create(device, composite_material);
        if (!r.has_value()) return 33;
        composite_insts[i] = std::move(*r);
    }

    // R3 multi-mip bloom — physical mip chain + per-pass material instances.
    //
    // Allocation: 4 RGBA16F render targets at /2, /4, /8, /16 of the
    // HDR target's size. Each instance binds exactly one source mip
    // (or the HDR target for the prefilter inst).
    BloomMipChain bloom_chain {};
    if (!create_bloom_chain(device, { window.width(), window.height() }, bloom_chain))
        return 43;

    // 1 prefilter (reads HDR, writes mip0)
    // 3 downsample insts: 0→1, 1→2, 2→3
    // 3 upsample insts:   3→2 (additive), 2→1 (additive), 1→0 (additive)
    auto bp_inst_r = cd::material::MaterialInstance::create(device, bloom_prefilter_material);
    if (!bp_inst_r.has_value()) return 44;
    auto& bloom_prefilter_inst = *bp_inst_r;

    std::array<cd::material::MaterialInstance, 3> bloom_down_insts {};
    for (std::uint32_t i = 0; i < 3; ++i)
    {
        auto r = cd::material::MaterialInstance::create(device, bloom_downsample_material);
        if (!r.has_value()) return 45;
        bloom_down_insts[i] = std::move(*r);
    }
    std::array<cd::material::MaterialInstance, 3> bloom_up_insts {};
    for (std::uint32_t i = 0; i < 3; ++i)
    {
        auto r = cd::material::MaterialInstance::create(device, bloom_upsample_material);
        if (!r.has_value()) return 46;
        bloom_up_insts[i] = std::move(*r);
    }

    // Wire descriptors. All sample with the linear-clamp albedo_sampler
    // (good enough — bloom doesn't need a mipmap-capable variant since
    // each pass writes mip 0 of its respective dedicated target).
    auto bind_bloom_descriptors = [&]() {
        auto write_one = [&](cd::material::MaterialInstance& inst,
                             cd::rhi::TextureViewHandle src_view) {
            std::array<cd::rhi::DescriptorWrite, 1> w {
                cd::rhi::DescriptorWrite {
                    .binding = 0,
                    .array_element = 0,
                    .type    = cd::rhi::DescriptorType::kCombinedImageSampler,
                    .view    = src_view,
                    .sampler = albedo_sampler } };
            (void)inst.update(w);
        };
        write_one(bloom_prefilter_inst, hdr_target.view);
        write_one(bloom_down_insts[0], bloom_chain.mips[0].view);
        write_one(bloom_down_insts[1], bloom_chain.mips[1].view);
        write_one(bloom_down_insts[2], bloom_chain.mips[2].view);
        write_one(bloom_up_insts[0],   bloom_chain.mips[3].view);
        write_one(bloom_up_insts[1],   bloom_chain.mips[2].view);
        write_one(bloom_up_insts[2],   bloom_chain.mips[1].view);
    };
    bind_bloom_descriptors();

    auto bind_composite_hdr = [&]() {
        for (std::uint32_t i = 0; i < 2; ++i)
        {
            std::array<cd::rhi::DescriptorWrite, 5> writes {
                cd::rhi::DescriptorWrite {
                    .binding = 0,
                    .array_element = 0,
                    .type    = cd::rhi::DescriptorType::kCombinedImageSampler,
                    .view    = hdr_target.view,
                    .sampler = albedo_sampler },
                cd::rhi::DescriptorWrite {
                    .binding = 1,
                    .array_element = 0,
                    .type    = cd::rhi::DescriptorType::kCombinedImageSampler,
                    .view    = bloom_chain.mips[0].view,
                    .sampler = albedo_sampler },
                cd::rhi::DescriptorWrite {
                    .binding = 2,
                    .array_element = 0,
                    .type    = cd::rhi::DescriptorType::kCombinedImageSampler,
                    .view    = depth.view,
                    .sampler = albedo_sampler },
                cd::rhi::DescriptorWrite {
                    .binding = 3,
                    .array_element = 0,
                    .type    = cd::rhi::DescriptorType::kCombinedImageSampler,
                    .view    = gbuf_normal.view,
                    .sampler = albedo_sampler },
                // TAA history — composite_insts[i] reads history[i],
                // and per-frame logic picks composite_insts[frame & 1]
                // so the read history was written by the prior frame.
                cd::rhi::DescriptorWrite {
                    .binding = 4,
                    .array_element = 0,
                    .type    = cd::rhi::DescriptorType::kCombinedImageSampler,
                    .view    = history_targets[i].view,
                    .sampler = albedo_sampler } };
            (void)composite_insts[i].update(writes);
        }
    };
    bind_composite_hdr();

    // ---- Meshes (one PBR sphere, five primitive entities) ----
    auto cube_cpu = cd::asset::make_cube();
    // make_cube ships per-face axis-coloured (red/green/blue) and
    // make_sphere/cone/cyl/torus ship pos-based rainbow vertex
    // colours. Both patterns FIGHT the per-instance tint multiply
    // (v_albedo = in_color * pc.tint.rgb), producing a muddy wash
    // where every entity looks similar regardless of its tint. Flat-
    // ten EVERY primitive to white (1,1,1) so the entity tint shows
    // unmodified — closes the user-flagged 'proseduriel cisimlerin
    // renkleri ayni' regression.
    auto sphere_cpu_mut = cd::asset::make_sphere(18, 28);
    auto cone_cpu_mut   = cd::asset::make_cone(32);
    auto cyl_cpu_mut    = cd::asset::make_cylinder(32);
    auto torus_cpu_mut  = cd::asset::make_torus(0.45F, 0.18F, 16, 24);
    auto flatten_white = [](auto& mesh) {
        for (auto& v : mesh.vertices)
        {
            v.color[0] = 1.0F;
            v.color[1] = 1.0F;
            v.color[2] = 1.0F;
        }
    };
    flatten_white(cube_cpu);
    flatten_white(sphere_cpu_mut);
    flatten_white(cone_cpu_mut);
    flatten_white(cyl_cpu_mut);
    flatten_white(torus_cpu_mut);
    const auto& sphere_cpu = sphere_cpu_mut;
    const auto& cone_cpu   = cone_cpu_mut;
    const auto& cyl_cpu    = cyl_cpu_mut;
    const auto& torus_cpu  = torus_cpu_mut;
    // Floor quad — 1000 m × 1000 m centred at origin, normal +Y. The
    // size is far larger than the camera ever reaches; the FS
    // distance-fade (30 m -> 60 m) handles the apparent infinite-grid
    // feel. Faz 1.5: real geometry on which the planar shadow pass
    // can project caster silhouettes. Procedural shader-space grid
    // landing in v1.7 frame-graph rework replaces this with a single
    // fullscreen plane intersection.
    const auto floor_cpu    = cd::asset::make_plane(1000.0F);

    GpuMesh cube_mesh   = upload_mesh(device, cube_cpu);
    GpuMesh sphere_mesh = upload_mesh(device, sphere_cpu);
    GpuMesh cone_mesh   = upload_mesh(device, cone_cpu);
    GpuMesh cyl_mesh    = upload_mesh(device, cyl_cpu);
    GpuMesh torus_mesh  = upload_mesh(device, torus_cpu);
    GpuMesh floor_mesh  = upload_mesh(device, floor_cpu);
    GpuMesh pbr_sphere  = upload_pbr_mesh(device, sphere_cpu);
    // R1.5: torus knot procedural showcase used when no glTF asset
    // resolves. With CesiumMan.glb in assets/samples/, the auto-load
    // path takes priority and uses the actual imported character.
    const auto knot_cpu = cd::asset::make_torus_knot(0.7F, 0.20F, 2, 3, 256, 24);
    GpuMesh knot_mesh   = upload_mesh(device, knot_cpu);

    // ---- glTF auto-load ----
    // Try a small list of well-known sample paths so the user can drop
    // any Khronos sample (DamagedHelmet.gltf, FlightHelmet.gltf …)
    // into ./assets/samples/ and have hello_engine pick it up on next
    // launch. Falls back gracefully if nothing is found.
    GpuMesh gltf_mesh {};
    std::string gltf_loaded_name;
    {
        const std::array<std::string, 10> kCandidates {
            "assets/samples/CesiumMan.glb",
            "assets/samples/DamagedHelmet.glb",
            "assets/samples/FlightHelmet.gltf",
            "assets/samples/DamagedHelmet.gltf",
            "assets/samples/BoomBox.gltf",
            "assets/samples/Duck.gltf",
            "assets/samples/Suzanne.glb",
            "assets/samples/Fox.glb",
            "assets/samples/model.gltf",
            "model.gltf"
        };
        for (const auto& p : kCandidates)
        {
            auto loaded = cd::asset_gltf::load_gltf(p);
            if (!loaded.has_value()) continue;
            // Merge every primitive of every mesh into one big
            // PrimitiveVertex buffer so we can render with the
            // existing prim pipeline. Texture sampling would need an
            // extra descriptor binding — deferred to the next ship.
            cd::asset::PrimitiveMesh merged;
            for (const auto& m : loaded->meshes)
            {
                for (const auto& prim : m.primitives)
                {
                    const auto base = static_cast<std::uint16_t>(merged.vertices.size());
                    for (const auto& v : prim.vertices)
                    {
                        cd::asset::PrimitiveVertex pv {};
                        pv.pos[0] = v.position.x;
                        pv.pos[1] = v.position.y;
                        pv.pos[2] = v.position.z;
                        pv.normal[0] = v.normal.x;
                        pv.normal[1] = v.normal.y;
                        pv.normal[2] = v.normal.z;
                        pv.uv[0] = v.texcoord0.x;
                        pv.uv[1] = v.texcoord0.y;
                        pv.color[0] = 0.85F;
                        pv.color[1] = 0.82F;
                        pv.color[2] = 0.78F;
                        merged.vertices.push_back(pv);
                    }
                    for (auto idx : prim.indices)
                    {
                        if (base + idx > 0xFFFFU)
                            continue;  // skip overflow (sample uses 16-bit IB)
                        merged.indices.push_back(static_cast<std::uint16_t>(base + idx));
                    }
                }
            }
            if (merged.vertices.empty() || merged.indices.empty())
            {
                std::fprintf(stderr,
                    "[gltf] %s parsed but contained no renderable geometry\n",
                    p.c_str());
                continue;
            }
            gltf_mesh = upload_mesh(device, merged);
            gltf_loaded_name = p;
            // gap #1/#13 — pull the first material's baseColor
            // texture out of the glTF and upload it to the prim
            // pipeline's binding 4 slot. Falls back silently if the
            // asset has no textures.
            if (!loaded->materials.empty() && !loaded->textures.empty())
            {
                const auto& mat = loaded->materials.front();
                const int tex_idx = mat.base_color_texture;
                if (tex_idx >= 0 &&
                    tex_idx < static_cast<int>(loaded->textures.size()))
                {
                    const auto& gt = loaded->textures[static_cast<std::size_t>(tex_idx)];
                    if (!gt.rgba.empty() && gt.width > 0 && gt.height > 0)
                    {
                        GpuTexture2D tex = create_texture_rgba8(
                            device, gt.rgba.data(), gt.width, gt.height);
                        if (tex.image.is_valid())
                        {
                            // Replace the 1x1 white default.
                            if (albedo_tex.view.is_valid())
                                device.destroy_texture_view(albedo_tex.view);
                            if (albedo_tex.image.is_valid())
                                device.destroy_texture(albedo_tex.image);
                            albedo_tex = tex;
                            // Re-write descriptor binding 4 to point
                            // at the new glTF texture.
                            std::array<cd::rhi::DescriptorWrite, 1> tw {
                                cd::rhi::DescriptorWrite {
                                    .binding = 4,
                                    .array_element = 0,
                                    .type    = cd::rhi::DescriptorType::kCombinedImageSampler,
                                    .view    = albedo_tex.view,
                                    .sampler = albedo_sampler } };
                            (void)prim_inst.update(tw);
                            has_gltf_texture = true;
                            std::fprintf(stderr,
                                "[gltf] baseColor texture loaded (%ux%u)\n",
                                gt.width, gt.height);
                        }
                    }
                }
            }
            std::fprintf(stderr,
                "[gltf] loaded %s — %zu verts, %zu indices (textured=%d)\n",
                p.c_str(),
                merged.vertices.size(),
                merged.indices.size(),
                static_cast<int>(has_gltf_texture));
            break;
        }
        if (gltf_loaded_name.empty())
        {
            std::fprintf(stderr,
                "[gltf] no asset found; drop a .gltf into ./assets/samples/ "
                "(e.g. Khronos DamagedHelmet) and re-launch.\n");
        }
    }

    auto mesh_for = [&](PrimitiveKind k) -> const GpuMesh& {
        switch (k)
        {
            case PrimitiveKind::kSphere:   return sphere_mesh;
            case PrimitiveKind::kCone:     return cone_mesh;
            case PrimitiveKind::kCylinder: return cyl_mesh;
            case PrimitiveKind::kTorus:    return torus_mesh;
            case PrimitiveKind::kGltf:     return gltf_mesh.vb.is_valid() ? gltf_mesh : knot_mesh;
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
    cd::rhi::AccelStructureHandle blas_gltf   = gltf_mesh.vb.is_valid()
                                                ? build_blas(gltf_mesh, "blas_gltf")
                                                : cd::rhi::AccelStructureHandle {};
    auto blas_for_kind = [&](PrimitiveKind k) -> cd::rhi::AccelStructureHandle {
        switch (k)
        {
            case PrimitiveKind::kSphere:   return blas_sphere;
            case PrimitiveKind::kCone:     return blas_cone;
            case PrimitiveKind::kCylinder: return blas_cyl;
            case PrimitiveKind::kTorus:    return blas_torus;
            case PrimitiveKind::kGltf:     return blas_gltf;
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
                        blas_torus, blas_floor, blas_gltf })
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

    // ---- World / Project / Level / Layer container (gap #18) ----
    // Passive editor outliner backing — shows the production
    // hierarchy in the new Outliner panel. Entities still live in
    // the ECS scene; the outliner groups them under layers by name.
    cd::world_container::World cd_world;
    cd_world.set_name("Sample World");
    {
        auto proj = std::make_unique<cd::world_container::Project>("Sample Project");
        auto* lvl = proj->add_level("Main");
        lvl->bounds().min = { -40.0F, -2.0F, -40.0F };
        lvl->bounds().max = {  40.0F, 10.0F,  40.0F };
        lvl->add_layer("Lights");
        lvl->add_layer("UI");
        cd_world.set_project(std::move(proj));
    }

    std::vector<SceneEntity> entities;
    {
        struct Seed { const char* name; cd::math::Vec3f pos; cd::math::Vec3f tint; PrimitiveKind k; };
        const std::array<Seed, 5> seeds {{
            // Saturated artistic palette — more vibrant than the v0.99.110
            // measurement palette, picks up enough off-channel content
            // to read as distinct material tints without going to pure
            // RGB.
            { "Cube",     { -2.4F, 0.0F,  0.0F }, { 1.00F, 0.10F, 0.10F }, PrimitiveKind::kCube },
            { "Sphere",   { -1.2F, 0.0F,  0.0F }, { 0.20F, 0.95F, 0.30F }, PrimitiveKind::kSphere },
            { "Cone",     {  0.0F, 0.0F,  0.0F }, { 0.15F, 0.40F, 1.00F }, PrimitiveKind::kCone },
            { "Cylinder", {  1.2F, 0.0F,  0.0F }, { 1.00F, 0.75F, 0.15F }, PrimitiveKind::kCylinder },
            { "Torus",    {  2.4F, 0.0F,  0.0F }, { 0.90F, 0.15F, 0.90F }, PrimitiveKind::kTorus },
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
        // glTF entity — seeded when auto-loader resolves an asset, OR
        // (R1.5 showcase) a procedural Earth-like textured sphere.
        // mesh_for(kGltf) returns gltf_mesh if valid else sphere_mesh.
        // BLAS stays empty when no real glTF — the procedural Earth
        // entity doesn't need its own BLAS for prim-shader rendering
        // (RT shadows for it would just sample the existing sphere
        // BLAS, but we accept the per-instance shadow gap as a small
        // visual-only issue for the procedural showcase).
        {
            SceneEntity e;
            e.handle = scene.create_node();
            if (gltf_mesh.vb.is_valid())
            {
                e.name = "glTF (" + gltf_loaded_name + ")";
                // Prominent front-and-centre placement so the imported
                // character is the focal showcase. Scale 2.2 reads as
                // ~1.5 m human height. CesiumMan ships Z-up (most
                // Khronos sample characters do); rotate -90° about
                // X to bring him upright in the engine's Y-up world.
                scene.local(e.handle)->value.position = { 0.0F, -0.55F, 0.5F };
                scene.local(e.handle)->value.scale    = { 2.2F, 2.2F, 2.2F };
                // X -90° rotation (Z-up -> Y-up) only. CesiumMan's
                // original model has -Y forward in Cesium space; after
                // -90° about X, that -Y maps to +Z (toward camera). No
                // extra Y flip needed; adding one inverts the character.
                scene.local(e.handle)->value.rotation = { -0.7071068F, 0.0F, 0.0F, 0.7071068F };
            }
            else
            {
                e.name = "Earth (procedural showcase)";
                scene.local(e.handle)->value.position = { 0.0F, 1.5F, 1.5F };
                scene.local(e.handle)->value.scale    = { 1.5F, 1.5F, 1.5F };
            }
            e.tint   = { 1.0F, 1.0F, 1.0F };
            e.kind   = PrimitiveKind::kGltf;
            entities.push_back(std::move(e));
        }
    }
    log_push(std::string { "[boot] " } +
             std::to_string(entities.size()) +
             " ECS entities spawned"
             + (gltf_loaded_name.empty() ? "" :
                std::string { " (incl. glTF: " } + gltf_loaded_name + ")"));
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
    // Light-specific drag state (gaps #16 + #17): rotation drives
    // light.direction, scale drives light.range / area_width.
    cd::math::Vec3f light_drag_dir_start  { 0.0F, -1.0F, 0.0F };
    float           light_drag_range_start { 0.0F };
    float           light_drag_area_w_start { 1.0F };
    float           light_drag_area_h_start { 1.0F };
    // Faz 1.5 UX fix — ray-plane projection initial hit on the
    // active axis at begin_drag. delta = current_axis_offset -
    // initial_axis_offset, robust against grazing-camera angles.
    // 'inf' marker = no valid initial hit, fall back to screen-space.
    float gizmo_drag_initial_offset = 0.0F;
    bool  gizmo_drag_use_ray_plane  = false;
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
        cd::light::point({ 2.0F, 3.0F, -3.0F }, { 1, 1, 1 }, 3000.0F, 15.0F),
        true, 2700.0F });
    lights.push_back({ "Halogen spot (3200K)",
        // Aim at the PBR sphere grid centre (0, 3.5, -4.5) from (-2, 3, 1).
        // Lumens lowered 3500 -> 1800 so default spot doesn't blow out
        // the floor pool; user-tunable via Inspector slider regardless.
        cd::light::spot({ -2.0F, 3.0F, 1.0F }, { 0.34F, 0.09F, -0.94F },
                        { 1, 1, 1 }, 1800.0F, 12.0F, 0.4F, 0.7F),
        true, 3200.0F });
    lights.push_back({ "Cyan rect-area (8000K)",
        cd::light::rect_area({ 0.0F, 4.5F, 2.0F }, { 0, 0, -1 }, { 1, 0, 0 },
                             3.0F, 1.0F, { 0.6F, 0.85F, 1.0F }, 2500.0F),
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
    // FX state — runtime-tweakable, pushed into PrimPush::fx_params
    // each draw. tonemap_op: 0=Narkowicz, 1=Hill, 2=Hable, 3=AGX.
    // Default = Hable (Uncharted 2). AGX desaturates the LDR-range
    // shading the sample produces; Hable preserves tints on the
    // front primitives + back metallic spheres. AGX still wins on
    // HDR-heavy frames — switch via palette ('Tonemap: AGX').
    int tonemap_op = 2;  // 0=Narkowicz 1=Hill 2=Hable 3=AGX
    palette.register_command(70, "Tonemap: AGX (Sobotka 2022)",
        [&]{ tonemap_op = 3; log_push("[fx] tonemap = AGX"); });
    palette.register_command(71, "Tonemap: Hill ACES (Filament fit)",
        [&]{ tonemap_op = 1; log_push("[fx] tonemap = Hill ACES"); });
    palette.register_command(72, "Tonemap: Hable / Uncharted 2",
        [&]{ tonemap_op = 2; log_push("[fx] tonemap = Hable"); });
    palette.register_command(73, "Tonemap: Narkowicz ACES",
        [&]{ tonemap_op = 0; log_push("[fx] tonemap = Narkowicz"); });
    // v1.4 day-ship FX wire-in. The post_gtao / post_bloom / post_ssr
    // libraries are linked (CMakeLists) and their Settings structs
    // are reachable; the multi-pass GPU dispatch lands in v1.7
    // frame-graph rework. Until then, the prim FS runs cheap inline
    // approximations gated by fx_params.z (GTAO crease darkening)
    // and fx_params.w (highlight bloom). The library settings live
    // here so the editor UI work in v1.6 can bind sliders straight
    // to these without renaming.
    cd::post_gtao::Settings   fx_gtao {};
    cd::post_bloom::Settings  fx_bloom {};
    cd::post_ssr::Settings    fx_ssr {};
    cd::post_dof::CameraSettings fx_dof {};
    cd::post_motion_blur::Settings fx_mblur {};
    cd::post_taa::Settings    fx_taa {};
    cd::post_smaa::Settings   fx_smaa {};
    float fx_gtao_strength  = 0.0F;   // 0 = off
    float fx_bloom_strength = 0.0F;
    float fx_smaa_strength  = 0.0F;
    float fx_motion_blur    = 0.0F;   // LIVE: composite camera-velocity (phase 215)
    float fx_taa_amount     = 0.0F;   // LIVE: composite TAA ping-pong (phase 216-217)
    float fx_dof_strength   = 0.0F;   // wired to composite (phase207)
    float fx_vignette_strength = 0.25F;  // soft default — readable cinematic edge
    float fx_film_grain     = 0.0F;   // 0 = off; 0.5 = visible filmic noise
    float fx_chromab_strength = 0.0F; // 0 = off; 0.5 = subtle radial RGB split
    // Composite tonemap/HDR knobs — own the entire post-fx settle here.
    float fx_exposure         = 3.0F;  // pre-tonemap exposure boost
    float fx_saturation_boost = 1.50F; // post-tonemap saturation pull-away
    float fx_bloom_post       = 0.04F; // bloom mip0 contribution mixed into HDR
    float fx_ao_strength      = 0.55F; // composite AO crease darkening
    float fx_shafts_strength  = 0.35F; // light shafts radial intensity
    float fx_ssr_strength     = 0.5F;  // SSR reflection contribution (default on)

    // Previous-frame camera basis snapshot — populated AFTER each
    // composite invoke so the next frame's reprojection sees t-1.
    // First frame: prev = current (zero velocity).
    struct PrevCamBasis
    {
        cd::math::Vec3f right { 1.0F, 0.0F, 0.0F };
        cd::math::Vec3f up    { 0.0F, 1.0F, 0.0F };
        cd::math::Vec3f fwd   { 0.0F, 0.0F, -1.0F };
        cd::math::Vec3f pos   { 0.0F, 0.0F, 0.0F };
        float half_w { 1.0F };
        float half_h { 1.0F };
        bool  valid  { false };
    } prev_cam_basis {};

    // TAA history target state — both start kUndefined and we cycle
    // them through ColorAttachment ↔ ShaderResource as composite
    // ping-pongs which one it reads vs writes per frame.
    std::array<cd::rhi::ResourceState, 2> history_states {
        cd::rhi::ResourceState::kUndefined,
        cd::rhi::ResourceState::kUndefined };
    bool  fx_hdr10_request  = false;  // queued for swapchain-output rework
    float fx_fog_density    = 0.0F;
    float fx_aerial_perspective = 0.0F;
    float fx_clouds_coverage = 0.0F;  // queued — needs 3D Worley/Perlin noise tex
    float fx_light_shafts   = 0.0F;   // LIVE in composite (phase 208) — legacy var kept
    cd::atmosphere::Parameters fx_atmosphere {};
    cd::light_shafts::Settings fx_lshafts {};
    cd::volumetric_clouds::Settings fx_clouds {};
    cd::volumetric_fog::GridConfig fx_vfog {};
    (void)fx_atmosphere; (void)fx_lshafts; (void)fx_clouds; (void)fx_vfog;
    // Advanced BRDF wire-in (queued for v1.7 material-system rework).
    // Settings live here so the editor UI can attach immediately when
    // the dispatch lands. Each toggle logs queue status.
    float fx_ltc_ggx_strength   = 0.0F;
    float fx_sheen_strength     = 0.0F;
    float fx_clearcoat_strength = 0.0F;
    float fx_sss_strength       = 0.0F;
    // Debug view modes: 0 final, 1 albedo, 2 world normal, 3 MR map,
    // 4 AO, 5 normal-mapped surface normal, 6 vertex UVs.
    int   fx_view_mode          = 0;
    float fx_decal_count        = 0.0F;   // count placeholder
    float fx_particle_emit_rate = 0.0F;   // /sec placeholder
    palette.register_command(100, "BRDF: Toggle LTC-GGX area-light specular (queued v1.7)",
        [&]{
            fx_ltc_ggx_strength = (fx_ltc_ggx_strength > 0.001F) ? 0.0F : 1.0F;
            log_push(fx_ltc_ggx_strength > 0.001F
                       ? "[brdf] LTC-GGX queued (v1.7 material rework)"
                       : "[brdf] LTC-GGX off");
        });
    palette.register_command(101, "BRDF: Toggle Sheen (queued v1.7)",
        [&]{
            fx_sheen_strength = (fx_sheen_strength > 0.001F) ? 0.0F : 0.5F;
            log_push(fx_sheen_strength > 0.001F
                       ? "[brdf] Sheen queued (v1.7 material rework)"
                       : "[brdf] Sheen off");
        });
    palette.register_command(102, "BRDF: Toggle Clearcoat (queued v1.7)",
        [&]{
            fx_clearcoat_strength = (fx_clearcoat_strength > 0.001F) ? 0.0F : 0.6F;
            log_push(fx_clearcoat_strength > 0.001F
                       ? "[brdf] Clearcoat queued (v1.7 material rework)"
                       : "[brdf] Clearcoat off");
        });
    palette.register_command(103, "BRDF: Toggle SSS / Burley diffusion (queued v1.7)",
        [&]{
            fx_sss_strength = (fx_sss_strength > 0.001F) ? 0.0F : 0.6F;
            log_push(fx_sss_strength > 0.001F
                       ? "[brdf] SSS queued (v1.7 needs neighbourhood pass)"
                       : "[brdf] SSS off");
        });
    palette.register_command(104, "FX: Spawn Decal (queued v1.7)",
        [&]{
            fx_decal_count += 1.0F;
            log_push("[fx] Decal queued (v1.7 needs projector volume + GBuffer)");
        });
    palette.register_command(105, "FX: Toggle GPU Particles 10k/sec (queued v1.7)",
        [&]{
            fx_particle_emit_rate = (fx_particle_emit_rate > 0.001F) ? 0.0F : 10000.0F;
            log_push(fx_particle_emit_rate > 0.001F
                       ? "[fx] GPU particles queued (v1.7 needs compute pipe)"
                       : "[fx] GPU particles off");
        });
    (void)fx_ltc_ggx_strength;
    (void)fx_sheen_strength;
    (void)fx_clearcoat_strength;
    (void)fx_sss_strength;
    (void)fx_decal_count;
    (void)fx_particle_emit_rate;
    // v1.5 GI wire-in (queued for v1.7 frame-graph + acceleration
    // structure dispatch). Settings + reservoirs instantiated so the
    // editor UI binds without renaming.
    cd::restir_di::Reservoir fx_restir_di_reservoir {};
    cd::restir_gi::Reservoir fx_restir_gi_reservoir {};
    cd::ddgi::GridConfig       fx_ddgi_grid {};
    cd::nrc::Config            fx_nrc_cfg {};
    (void)fx_restir_di_reservoir; (void)fx_restir_gi_reservoir;
    (void)fx_ddgi_grid; (void)fx_nrc_cfg;
    bool fx_restir_di_on = false;
    bool fx_restir_gi_on = false;
    bool fx_ddgi_on      = false;
    bool fx_nrc_on       = false;
    palette.register_command(110, "GI: Toggle ReSTIR DI (queued v1.7)",
        [&]{
            fx_restir_di_on = !fx_restir_di_on;
            log_push(fx_restir_di_on
                       ? "[gi] ReSTIR DI queued (v1.7 needs RT compute pipe)"
                       : "[gi] ReSTIR DI off");
        });
    palette.register_command(111, "GI: Toggle ReSTIR GI (queued v1.7)",
        [&]{
            fx_restir_gi_on = !fx_restir_gi_on;
            log_push(fx_restir_gi_on
                       ? "[gi] ReSTIR GI queued (v1.7 needs RT compute pipe)"
                       : "[gi] ReSTIR GI off");
        });
    palette.register_command(112, "GI: Toggle DDGI probe update (queued v1.7)",
        [&]{
            fx_ddgi_on = !fx_ddgi_on;
            log_push(fx_ddgi_on
                       ? "[gi] DDGI queued (v1.7 needs probe-volume RT)"
                       : "[gi] DDGI off");
        });
    palette.register_command(113, "GI: Toggle NRC (TinyCudaNN backend, queued v1.7)",
        [&]{
            fx_nrc_on = !fx_nrc_on;
            log_push(fx_nrc_on
                       ? "[gi] NRC queued (v1.7 needs CUDA inference path)"
                       : "[gi] NRC off");
        });
    palette.register_command(120, "RHI: Status (active backend + parity)",
        [&]{
            log_push("[rhi] active = Vulkan (production)");
            log_push("[rhi] D3D12 partial — PSO/desc/shader stubs (v1.8.1)");
            log_push("[rhi] OpenGL partial — no RT support (v1.8.2)");
            log_push("[rhi] Metal skeleton — Apple-only stub (v1.8.3)");
            log_push("[rhi] WebGPU not started (v1.8.4 via Dawn)");
            log_push("[rhi] see docs/RHI_PARITY_STATUS.md");
        });
    palette.register_command(130, "Physics: Status (Jolt + cloth roadmap)",
        [&]{
            log_push("[phys] primitives: Aabb/Sphere/Capsule/Obb/Ray ready");
            log_push("[phys] IPhysicsWorld interface ready; Jolt impl v1.9.1");
            log_push("[phys] cloth (PBD) v1.9.3; character controller v1.9.4");
            log_push("[phys] see docs/PHYSICS_V19_PLAN.md");
        });
    palette.register_command(131, "Script: Status (Lua 5.4 + AI BT roadmap)",
        [&]{
            log_push("[script] cd::script::Engine skeleton present");
            log_push("[script] Lua 5.4 integration v1.9.5");
            log_push("[script] behaviour-tree nodes v1.9.5");
        });
    palette.register_command(140, "v2.0: Production Milestone Status",
        [&]{
            log_push("[v2.0] cooker (assetc):     v2.0.1 — pending");
            log_push("[v2.0] profiler:            cd::profile sinks ready (v2.0.2)");
            log_push("[v2.0] crash reporter:      cd::diag::CrashReporter ready (v2.0.3)");
            log_push("[v2.0] HRTF audio:          cd::audio core ready (v2.0.4)");
            log_push("[v2.0] i18n + a11y:         ICU integration pending (v2.0.5)");
            log_push("[v2.0] hot-reload:          vfs + shader Compiler ready (v2.0.6)");
            log_push("[v2.0] 24h stress harness:  pending CI hardware (v2.0.7)");
            log_push("[v2.0] ENGINE_GUIDE.md:     pending (v2.0.8)");
            log_push("[v2.0] see docs/PRODUCTION_V20_PLAN.md");
        });
    (void)fx_restir_di_on; (void)fx_restir_gi_on;
    (void)fx_ddgi_on; (void)fx_nrc_on;
    palette.register_command(80, "FX: Toggle GTAO (inline approx)",
        [&]{
            fx_gtao_strength = (fx_gtao_strength > 0.001F) ? 0.0F : 0.65F;
            log_push(fx_gtao_strength > 0.001F ? "[fx] GTAO on" : "[fx] GTAO off");
        });
    palette.register_command(81, "FX: Toggle Bloom (inline approx)",
        [&]{
            fx_bloom_strength = (fx_bloom_strength > 0.001F) ? 0.0F : 0.55F;
            log_push(fx_bloom_strength > 0.001F ? "[fx] Bloom on" : "[fx] Bloom off");
        });
    palette.register_command(82, "FX: GTAO Settings (radius=1m, dirs=4)",
        [&]{
            fx_gtao.radius = 1.0F;
            fx_gtao.direction_count = 4;
            log_push("[fx] GTAO settings reset to defaults");
        });
    palette.register_command(83, "FX: Bloom Settings (threshold=1.0, intensity=0.04)",
        [&]{
            fx_bloom.threshold = 1.0F;
            fx_bloom.intensity = 0.04F;
            log_push("[fx] Bloom settings reset to defaults");
        });
    palette.register_command(84, "FX: Toggle SMAA (inline luma-edge blur)",
        [&]{
            fx_smaa_strength = (fx_smaa_strength > 0.001F) ? 0.0F : 0.55F;
            log_push(fx_smaa_strength > 0.001F ? "[fx] SMAA on" : "[fx] SMAA off");
        });
    palette.register_command(85, "FX: Toggle Motion Blur",
        [&]{
            fx_motion_blur = (fx_motion_blur > 0.001F) ? 0.0F : 0.5F;
            log_push(fx_motion_blur > 0.001F
                       ? "[fx] MotionBlur on (composite camera-velocity)"
                       : "[fx] MotionBlur off");
        });
    palette.register_command(86, "FX: Toggle TAA",
        [&]{
            fx_taa_amount = (fx_taa_amount > 0.001F) ? 0.0F : 0.85F;
            log_push(fx_taa_amount > 0.001F
                       ? "[fx] TAA on (history + Halton jitter)"
                       : "[fx] TAA off");
        });
    palette.register_command(87, "FX: Toggle DOF",
        [&]{
            fx_dof_strength = (fx_dof_strength > 0.001F) ? 0.0F : 0.5F;
            log_push(fx_dof_strength > 0.001F
                       ? "[fx] DOF on (composite bokeh)"
                       : "[fx] DOF off");
        });
    palette.register_command(88, "FX: Toggle HDR10 (queued)",
        [&]{
            fx_hdr10_request = !fx_hdr10_request;
            log_push(fx_hdr10_request
                       ? "[fx] HDR10 request queued (swapchain rework)"
                       : "[fx] HDR10 off");
        });
    palette.register_command(90, "FX: Toggle Height Fog (inline exp)",
        [&]{
            fx_fog_density = (fx_fog_density > 0.001F) ? 0.0F : 0.6F;
            log_push(fx_fog_density > 0.001F ? "[fx] Height fog on" : "[fx] Height fog off");
        });
    palette.register_command(91, "FX: Toggle Aerial Perspective (inline)",
        [&]{
            fx_aerial_perspective = (fx_aerial_perspective > 0.001F) ? 0.0F : 0.7F;
            log_push(fx_aerial_perspective > 0.001F
                       ? "[fx] Aerial perspective on"
                       : "[fx] Aerial perspective off");
        });
    palette.register_command(92, "FX: Toggle Clouds (queued v1.7)",
        [&]{
            fx_clouds_coverage = (fx_clouds_coverage > 0.001F) ? 0.0F : 0.55F;
            log_push(fx_clouds_coverage > 0.001F
                       ? "[fx] Volumetric clouds queued (v1.7)"
                       : "[fx] Clouds off");
        });
    palette.register_command(93, "FX: Toggle Light Shafts",
        [&]{
            fx_shafts_strength = (fx_shafts_strength > 0.001F) ? 0.0F : 0.5F;
            log_push(fx_shafts_strength > 0.001F
                       ? "[fx] Light shafts on (Mitchell 2007 god rays)"
                       : "[fx] Light shafts off");
        });
    // Silence -Wunused-variable on the not-yet-dispatched libs.
    (void)fx_ssr; (void)fx_dof; (void)fx_mblur; (void)fx_taa; (void)fx_smaa;
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
            case PrimitiveKind::kGltf:     return "Gltf";
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
            // Extend with a top-level "lights" array so the lights
            // panel state round-trips through save/load too —
            // priority gap #15.
            {
                cd::asset_json::Array light_arr;
                for (const auto& l : lights)
                {
                    cd::asset_json::Object lo;
                    lo["name"]      = cd::asset_json::Value { l.name };
                    lo["enabled"]   = cd::asset_json::Value { l.enabled };
                    lo["type"]      = cd::asset_json::Value { static_cast<int>(l.light.type) };
                    lo["kelvin"]    = cd::asset_json::Value { static_cast<double>(l.kelvin) };
                    lo["intensity"] = cd::asset_json::Value { static_cast<double>(l.light.intensity) };
                    lo["range"]     = cd::asset_json::Value { static_cast<double>(l.light.range) };
                    cd::asset_json::Array pos;
                    pos.push_back(cd::asset_json::Value { static_cast<double>(l.light.position.x) });
                    pos.push_back(cd::asset_json::Value { static_cast<double>(l.light.position.y) });
                    pos.push_back(cd::asset_json::Value { static_cast<double>(l.light.position.z) });
                    lo["position"] = cd::asset_json::Value { std::move(pos) };
                    // Persist the linear RGB colour so the load path can
                    // restore a user-picked tint (kelvin=0 mode).
                    cd::asset_json::Array col;
                    col.push_back(cd::asset_json::Value { static_cast<double>(l.light.color.x) });
                    col.push_back(cd::asset_json::Value { static_cast<double>(l.light.color.y) });
                    col.push_back(cd::asset_json::Value { static_cast<double>(l.light.color.z) });
                    lo["color"] = cd::asset_json::Value { std::move(col) };
                    cd::asset_json::Array dir;
                    dir.push_back(cd::asset_json::Value { static_cast<double>(l.light.direction.x) });
                    dir.push_back(cd::asset_json::Value { static_cast<double>(l.light.direction.y) });
                    dir.push_back(cd::asset_json::Value { static_cast<double>(l.light.direction.z) });
                    lo["direction"] = cd::asset_json::Value { std::move(dir) };
                    light_arr.push_back(cd::asset_json::Value { std::move(lo) });
                }
                auto& obj = root.as_object_mut();
                obj["lights"] = cd::asset_json::Value { std::move(light_arr) };
            }
            const auto text = cd::asset_json::serialize(root, /*pretty=*/true);
            std::ofstream f { kSavePath, std::ios::binary | std::ios::trunc };
            if (f)
            {
                f.write(text.data(), static_cast<std::streamsize>(text.size()));
                log_push(std::string { "[scene] Saved " } + std::to_string(entities.size()) +
                         " entities + " + std::to_string(lights.size()) +
                         " lights to " + kSavePath);
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
            // Lights from the optional top-level "lights" array
            // (priority gap #15). Missing or malformed is non-fatal:
            // we keep the panel's current lights[] vector intact.
            if (r->is_object())
            {
                const auto& root_obj = r->as_object();
                if (auto it = root_obj.find("lights");
                    it != root_obj.end() && it->second.is_array())
                {
                    const auto& la = it->second.as_array();
                    std::vector<LightRow> new_lights;
                    new_lights.reserve(la.size());
                    for (const auto& lv : la)
                    {
                        if (!lv.is_object()) continue;
                        const auto& lo = lv.as_object();
                        LightRow row {};
                        if (auto n = lo.find("name");
                            n != lo.end() && n->second.is_string())
                            row.name = n->second.as_string();
                        if (auto en = lo.find("enabled");
                            en != lo.end() && en->second.is_bool())
                            row.enabled = en->second.as_bool();
                        if (auto t = lo.find("type");
                            t != lo.end() && t->second.is_number())
                            row.light.type = static_cast<cd::light::LightType>(
                                static_cast<int>(t->second.as_number()));
                        if (auto k = lo.find("kelvin");
                            k != lo.end() && k->second.is_number())
                            row.kelvin = static_cast<float>(k->second.as_number());
                        if (auto i = lo.find("intensity");
                            i != lo.end() && i->second.is_number())
                            row.light.intensity = static_cast<float>(i->second.as_number());
                        if (auto rg = lo.find("range");
                            rg != lo.end() && rg->second.is_number())
                            row.light.range = static_cast<float>(rg->second.as_number());
                        if (auto p = lo.find("position");
                            p != lo.end() && p->second.is_array() &&
                            p->second.as_array().size() == 3)
                        {
                            const auto& a = p->second.as_array();
                            if (a[0].is_number() && a[1].is_number() && a[2].is_number())
                                row.light.position = {
                                    static_cast<float>(a[0].as_number()),
                                    static_cast<float>(a[1].as_number()),
                                    static_cast<float>(a[2].as_number()) };
                        }
                        if (auto d = lo.find("direction");
                            d != lo.end() && d->second.is_array() &&
                            d->second.as_array().size() == 3)
                        {
                            const auto& a = d->second.as_array();
                            if (a[0].is_number() && a[1].is_number() && a[2].is_number())
                                row.light.direction = {
                                    static_cast<float>(a[0].as_number()),
                                    static_cast<float>(a[1].as_number()),
                                    static_cast<float>(a[2].as_number()) };
                        }
                        // Restore user-picked RGB if present (kelvin=0
                        // mode), otherwise compute colour from CCT.
                        if (auto c = lo.find("color");
                            c != lo.end() && c->second.is_array() &&
                            c->second.as_array().size() == 3)
                        {
                            const auto& a = c->second.as_array();
                            if (a[0].is_number() && a[1].is_number() && a[2].is_number())
                                row.light.color = {
                                    static_cast<float>(a[0].as_number()),
                                    static_cast<float>(a[1].as_number()),
                                    static_cast<float>(a[2].as_number()) };
                        }
                        else if (row.kelvin > 0.0F)
                        {
                            row.light.color = cd::light::cct_to_linear_rgb(row.kelvin);
                        }
                        new_lights.push_back(std::move(row));
                    }
                    if (!new_lights.empty())
                        lights = std::move(new_lights);
                }
            }
            selected = entities.empty() ? -1 : 0;
            history.clear();
            log_push(std::string { "[scene] Loaded " } + std::to_string(entities.size()) +
                     " entities + " + std::to_string(lights.size()) +
                     " lights from " + kSavePath);
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
            if (!create_depth_target(device, { window.width(), window.height() }, kDepthFormat, depth,
                                     cd::rhi::TextureUsage::kSampled))
                continue;
            if (!create_color_target(device, { window.width(), window.height() }, kHdrFormat, hdr_target))
                continue;
            if (!create_color_target(device, { window.width(), window.height() }, kNormalFormat, gbuf_normal))
                continue;
            if (!create_color_target(device, { window.width(), window.height() }, kAlbedoFormat, gbuf_albedo))
                continue;
            if (!create_color_target(device, { window.width(), window.height() }, kMrFormat, gbuf_mr))
                continue;
            bool history_ok = true;
            for (auto& h : history_targets)
            {
                if (!create_color_target(device, { window.width(), window.height() }, kHistoryFormat, h))
                { history_ok = false; break; }
            }
            if (!history_ok) continue;
            history_states[0] = cd::rhi::ResourceState::kUndefined;
            history_states[1] = cd::rhi::ResourceState::kUndefined;
            if (!create_bloom_chain(device, { window.width(), window.height() }, bloom_chain))
                continue;
            bind_bloom_descriptors();
            bind_composite_hdr();
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

                // Sphere-test every entity. Radius scales with the
                // entity's transform scale so clicking anywhere on a
                // big imported asset (e.g. CesiumMan at scale 2.2)
                // still selects it — not just the central pivot.
                // Closes user-flagged 'cisimler ve isiklar sadece
                // pivottan secilebiliyor'.
                float best_t = 1e30F;
                int   best_i = -1;
                for (std::size_t i = 0; i < entities.size(); ++i)
                {
                    auto* lt = scene.local(entities[i].handle);
                    if (lt == nullptr) continue;
                    const cd::math::Vec3f c { lt->value.position.x,
                                              lt->value.position.y,
                                              lt->value.position.z };
                    const float ms = std::max({ lt->value.scale.x,
                                                 lt->value.scale.y,
                                                 lt->value.scale.z });
                    // Unit primitive half-extent ≈ 0.55; for compound
                    // / oblong meshes (humanoid) bump by 1.6 along the
                    // longest dimension.
                    const float pick_r = 0.55F * std::max(1.0F, ms) * 1.6F;
                    const cd::math::Vec3f oc {
                        cam.eye.x - c.x, cam.eye.y - c.y, cam.eye.z - c.z };
                    const float b = oc.x*ray_dir.x + oc.y*ray_dir.y + oc.z*ray_dir.z;
                    const float cc = oc.x*oc.x + oc.y*oc.y + oc.z*oc.z - pick_r*pick_r;
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
                    // Big-pick light bulb hit-sphere so clicking near
                    // the gizmo or anywhere around the visible bulb
                    // selects the light, not just its centre dot.
                    constexpr float kLightPickR = 0.9F;
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
        else
        {
            // Subsequent frames: composite-pass GTAO sampled the depth
            // target as ShaderResource at the end of the prior frame;
            // bring it back to kDepthWrite before the HDR scene pass.
            std::array<cd::rhi::TextureBarrier, 1> db {
                cd::rhi::TextureBarrier {
                    .texture = depth.image,
                    .from = cd::rhi::ResourceState::kShaderResource,
                    .to = cd::rhi::ResourceState::kDepthWrite,
                    .range = { .base_mip = 0, .mip_count = 1, .base_layer = 0, .layer_count = 1 } } };
            cmd.barrier({}, db);
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

        // R3: scene draws into the HDR + G-Buffer normal off-screen
        // targets; composite + ImGui write to the swapchain in a
        // follow-up render pass. Transition both to ColorAttachment
        // on first use; subsequent frames re-enter from kShaderResource
        // (composite sampled them last frame).
        {
            const cd::rhi::ResourceState prev_state = (frame_idx == 0)
                ? cd::rhi::ResourceState::kUndefined
                : cd::rhi::ResourceState::kShaderResource;
            std::array<cd::rhi::TextureBarrier, 4> hb {
                cd::rhi::TextureBarrier {
                    .texture = hdr_target.image,
                    .from    = prev_state,
                    .to      = cd::rhi::ResourceState::kColorAttachment,
                    .range   = { 0, 1, 0, 1 } },
                cd::rhi::TextureBarrier {
                    .texture = gbuf_normal.image,
                    .from    = prev_state,
                    .to      = cd::rhi::ResourceState::kColorAttachment,
                    .range   = { 0, 1, 0, 1 } },
                cd::rhi::TextureBarrier {
                    .texture = gbuf_albedo.image,
                    .from    = prev_state,
                    .to      = cd::rhi::ResourceState::kColorAttachment,
                    .range   = { 0, 1, 0, 1 } },
                cd::rhi::TextureBarrier {
                    .texture = gbuf_mr.image,
                    .from    = prev_state,
                    .to      = cd::rhi::ResourceState::kColorAttachment,
                    .range   = { 0, 1, 0, 1 } } };
            cmd.barrier({}, hb);
        }
        std::array<cd::rhi::ColorAttachmentInfo, 4> color_attach {
            cd::rhi::ColorAttachmentInfo { .view = hdr_target.view,
                                          .load_op = cd::rhi::LoadOp::kClear,
                                          .store_op = cd::rhi::StoreOp::kStore,
                                          .clear_color = { .f32 = { 1.0F, 0.0F, 1.0F, 1.0F } } },
            cd::rhi::ColorAttachmentInfo { .view = gbuf_normal.view,
                                          .load_op = cd::rhi::LoadOp::kClear,
                                          .store_op = cd::rhi::StoreOp::kStore,
                                          .clear_color = { .f32 = { 0.0F, 0.0F, 0.0F, 0.0F } } },
            cd::rhi::ColorAttachmentInfo { .view = gbuf_albedo.view,
                                          .load_op = cd::rhi::LoadOp::kClear,
                                          .store_op = cd::rhi::StoreOp::kStore,
                                          .clear_color = { .f32 = { 0.0F, 0.0F, 0.0F, 0.0F } } },
            cd::rhi::ColorAttachmentInfo { .view = gbuf_mr.view,
                                          .load_op = cd::rhi::LoadOp::kClear,
                                          .store_op = cd::rhi::StoreOp::kStore,
                                          .clear_color = { .f32 = { 0.0F, 1.0F, 0.0F, 0.0F } } } };
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
        cd::math::Mat4f vp_unjittered = cd::camera::view_projection(cam, aspect);

        // R3 Halton(2,3) sub-pixel jitter for proper TAA accumulation
        // — only active when TAA is dialled in. Without jitter, every
        // frame samples the same fragment centre and TAA stagnates;
        // with jitter the integration converges toward supersample.
        auto halton = [](std::uint32_t i, std::uint32_t base) {
            float r = 0.0F;
            float f = 1.0F / static_cast<float>(base);
            while (i > 0)
            {
                r += f * static_cast<float>(i % base);
                i /= base;
                f /= static_cast<float>(base);
            }
            return r;
        };
        const float jx_px = (fx_taa_amount > 0.001F)
            ? (halton((frame_idx % 8U) + 1U, 2) - 0.5F) : 0.0F;
        const float jy_px = (fx_taa_amount > 0.001F)
            ? (halton((frame_idx % 8U) + 1U, 3) - 0.5F) : 0.0F;
        const float jx_ndc = jx_px * 2.0F / static_cast<float>(frame.extent.width);
        const float jy_ndc = jy_px * 2.0F / static_cast<float>(frame.extent.height);

        // T_jitter * vp — adds jx_ndc * w to clip.x so post-divide
        // ndc.x shifts by jx_ndc. Column-major: for each column c,
        // add the bottom-row entry * jitter into rows 0/1.
        cd::math::Mat4f vp = vp_unjittered;
        for (std::size_t c = 0; c < 4; ++c)
        {
            vp[c][0] += jx_ndc * vp_unjittered[c][3];
            vp[c][1] += jy_ndc * vp_unjittered[c][3];
        }

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
        // Defaults must be ZERO so disabling every directional light
        // leaves the sky truly dark — the prior 0.9 default caused
        // the 'all-lights-off => bright white sky' bug.
        cd::math::Vec3f sky_sun_dir { -0.4F, -0.6F, -0.7F };
        cd::math::Vec3f sky_sun_col { 0.0F, 0.0F, 0.0F };
        float           sky_sun_strength = 0.0F;
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
        pbr_inst.bind(cmd, 0);  // multi-light UBO (#22)
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
        // 5 distinct PBR materials, one per row, with column =
        // roughness gradient. Replaces the prior all-copper-with-
        // metallic-gradient layout that read as a uniform cream
        // grid because polished metal reflections share the
        // analytical-blue sky regardless of base F0.
        //   row 0: copper      (1.0)  metal
        //   row 1: gold        (1.0)  metal
        //   row 2: silver      (1.0)  metal
        //   row 3: aluminum    (1.0)  metal
        //   row 4: white plastic (0.0) dielectric
        struct PbrPalette { cd::math::Vec3f albedo; float metal; };
        static constexpr std::array<PbrPalette, kGrid> kRowPalette {{
            { { 0.95F, 0.64F, 0.32F }, 1.0F },  // copper
            { { 1.00F, 0.86F, 0.57F }, 1.0F },  // gold
            { { 0.95F, 0.93F, 0.88F }, 1.0F },  // silver
            { { 0.91F, 0.92F, 0.92F }, 1.0F },  // aluminum
            { { 0.95F, 0.95F, 0.95F }, 0.0F },  // white plastic
        }};
        for (int row = 0; row < kGrid; ++row)
        {
            for (int col = 0; col < kGrid; ++col)
            {
                const auto& mat = kRowPalette[static_cast<std::size_t>(row)];
                const float metallic = mat.metal;
                const float roughness = 0.05F + (1.0F - 0.05F) *
                    (static_cast<float>(col) / static_cast<float>(kGrid - 1));
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
                // Lights-off baseline: PBR sphere grid was running with
                // hard-coded defaults even when the sun was disabled,
                // so the 5x5 array stayed lit while everything else
                // went dark. Zero the defaults so 'no sun = no PBR
                // contribution from the sun term' (same rule the prim
                // shader path got in 764d3bf).
                cd::math::Vec3f light_dir { 0.0F, -1.0F, 0.0F };
                cd::math::Vec3f light_color { 0.0F, 0.0F, 0.0F };
                float           light_intensity = 0.0F;
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
                pb.albedo[0] = mat.albedo.x;
                pb.albedo[1] = mat.albedo.y;
                pb.albedo[2] = mat.albedo.z;
                pb.albedo[3] = 1.0F;
                pb.mr_amb[0] = metallic; pb.mr_amb[1] = roughness; pb.mr_amb[2] = 0.0F; pb.mr_amb[3] = 0.0F;
                pb.camera_pos[0] = cam.eye.x; pb.camera_pos[1] = cam.eye.y; pb.camera_pos[2] = cam.eye.z; pb.camera_pos[3] = 0.0F;
                // PrimPush includes fx_params4 for advanced BRDF; sphere-grid PBR
                // path uses StandardPbrPush instead, so this is a no-op here.
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
        // Lights-off baseline: NOTHING contributes. Defaults are
        // intentionally zeroed so the scene goes to (near-)black when
        // every light is disabled — user feedback: "isik yoksa golge
        // yada isik beklemem". The for-loop below promotes the first
        // enabled directional to the sun slot; absent that, sun_str
        // stays 0 and the FS sun term contributes nothing.
        cd::math::Vec3f sun_dir { 0.0F, -1.0F, 0.0F };
        cd::math::Vec3f sun_col { 0.0F, 0.0F, 0.0F };
        float           sun_str = 0.0F;
        float           ambient_w = 0.0F;
        bool            has_sun = false;
        for (const auto& lrow : lights)
        {
            if (!lrow.enabled) continue;
            if (lrow.light.type != cd::light::LightType::kDirectional) continue;
            sun_dir = lrow.light.direction;
            sun_col = lrow.light.color;
            sun_str = std::min(2.5F, lrow.light.intensity / 80000.0F);
            // Sky hemisphere tied to sun being enabled: no sun, no
            // sky bounce — the universe is dark.
            ambient_w = 0.18F;
            has_sun = true;
            break;
        }
        (void)has_sun;
        // ---- Multi-light UBO fill (gap #2) ----
        // Walk every enabled non-sun light and pack into the
        // descriptor-bound UBO. Up to kMaxLights (8) slots; extras
        // drop silently (logged once via the counter).
        {
            LightUboGpu ubo {};
            ubo.count = 0;
            for (const auto& lrow : lights)
            {
                if (!lrow.enabled) continue;
                const auto k = lrow.light.type;
                if (k == cd::light::LightType::kDirectional) continue;
                if (ubo.count >= kMaxLights) break;
                auto& s = ubo.slots[ubo.count];
                s.pos_range[0] = lrow.light.position.x;
                s.pos_range[1] = lrow.light.position.y;
                s.pos_range[2] = lrow.light.position.z;
                // Range: point/spot already have it; area lights derive
                // a sensible falloff from area extents.
                s.pos_range[3] = (k == cd::light::LightType::kPoint ||
                                  k == cd::light::LightType::kSpot)
                                 ? lrow.light.range
                                 : (lrow.light.area_width + lrow.light.area_height) * 4.0F;
                s.dir_type[0] = lrow.light.direction.x;
                s.dir_type[1] = lrow.light.direction.y;
                s.dir_type[2] = lrow.light.direction.z;
                s.dir_type[3] = static_cast<float>(static_cast<int>(k));
                s.color_int[0] = lrow.light.color.x;
                s.color_int[1] = lrow.light.color.y;
                s.color_int[2] = lrow.light.color.z;
                // Lumens → unit intensity. Scale calibrated so a 1200
                // lumen point at ~3 m yields a visible (~0.5..1.0)
                // direct contribution on metallic spheres even with
                // the sun fully off. Spot boost x6 (small solid
                // angle), area dampen ×0.2 (was 0.05 — too dim to
                // illuminate dielectrics when sun off).
                float ki = lrow.light.intensity / (4.0F * 3.14159265F) / 2.0F;
                if (k == cd::light::LightType::kSpot) ki *= 6.0F;
                else if (k == cd::light::LightType::kRectArea ||
                         k == cd::light::LightType::kDiskArea) ki *= 0.20F;
                s.color_int[3] = ki;
                s.extras[0] = lrow.light.cos_outer_cone;
                s.extras[1] = lrow.light.area_width;
                s.extras[2] = lrow.light.area_height;
                s.extras[3] = lrow.light.cos_inner_cone;
                ubo.count++;
            }
            (void)device.upload_buffer(lights_ubo, 0,
                std::span<const std::byte>(
                    reinterpret_cast<const std::byte*>(&ubo), sizeof(ubo)));
            counters.set("lights_active", ubo.count);
        }

        prim_material.apply(cmd);
        prim_inst.bind(cmd, 0);  // Faz 1.6 CSM + Faz 1.9 light UBO

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
            // Shadow-catcher + grid-helper combo (gaps #20 + #21).
            // Floor body colour kept subtle so the plane reads more
            // like an editor helper than a scene mesh — shadows
            // (much darker, see planar-shadow tint below) and grid
            // lines (much brighter) both stand out against it. FS
            // also fades the floor with camera distance for a
            // pseudo-infinite-grid feel pending the real procedural-
            // grid helper in v1.6 editor.
            fp.tint[0] = 0.15F; fp.tint[1] = 0.16F; fp.tint[2] = 0.18F; fp.tint[3] = 2.0F;
            fp.sun_dir[0] = sun_dir.x; fp.sun_dir[1] = sun_dir.y;
            fp.sun_dir[2] = sun_dir.z; fp.sun_dir[3] = sun_str;
            fp.sun_color[0] = sun_col.x; fp.sun_color[1] = sun_col.y;
            fp.sun_color[2] = sun_col.z; fp.sun_color[3] = ambient_w;
            fp.fx_params[0] = static_cast<float>(tonemap_op);
            fp.fx_params[1] = 0.0F;
            // Floor opts out of GTAO crease darkening — its normal is
            // flat so dFdx/dFdy returns zero, but bloom on bright grid
            // lines is a nice subtle highlight.
            fp.fx_params[2] = 0.0F;
            fp.fx_params[3] = fx_bloom_strength;
            fp.fx_params2[0] = fx_smaa_strength;
            fp.fx_params2[1] = fx_motion_blur;
            fp.fx_params2[2] = fx_taa_amount;
            fp.fx_params2[3] = fx_dof_strength;
            fp.fx_params3[0] = fx_fog_density;
            fp.fx_params3[1] = fx_aerial_perspective;
            fp.fx_params3[2] = fx_clouds_coverage;
            fp.fx_params3[3] = fx_light_shafts;
            fp.camera_pos[0] = cam.eye.x; fp.camera_pos[1] = cam.eye.y;
            fp.camera_pos[2] = cam.eye.z; fp.camera_pos[3] = 0.0F;
            fp.fx_params4[0] = fp.fx_params4[1] = fp.fx_params4[2] = 0.0F;
            fp.fx_params4[3] = static_cast<float>(fx_view_mode);
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
            pp.fx_params[0] = static_cast<float>(tonemap_op);
            // fx_params.y = 1.0 routes the FS through the baseColor
            // texture path (binding 4). Only kGltf entities are
            // actually textured today — primitives stay on the
            // vertex-coloured albedo path.
            pp.fx_params[1] = (ent.kind == PrimitiveKind::kGltf && has_gltf_texture)
                            ? 1.0F : 0.0F;
            pp.fx_params[2] = fx_gtao_strength;
            pp.fx_params[3] = fx_bloom_strength;
            pp.fx_params2[0] = fx_smaa_strength;
            pp.fx_params2[1] = fx_motion_blur;
            pp.fx_params2[2] = fx_taa_amount;
            pp.fx_params2[3] = fx_dof_strength;
            pp.fx_params3[0] = fx_fog_density;
            pp.fx_params3[1] = fx_aerial_perspective;
            pp.fx_params3[2] = fx_clouds_coverage;
            pp.fx_params3[3] = fx_light_shafts;
            pp.camera_pos[0] = cam.eye.x; pp.camera_pos[1] = cam.eye.y;
            pp.camera_pos[2] = cam.eye.z; pp.camera_pos[3] = 0.0F;
            pp.fx_params4[0] = fx_clearcoat_strength;
            pp.fx_params4[1] = fx_sheen_strength;
            pp.fx_params4[2] = fx_sss_strength;
            pp.fx_params4[3] = static_cast<float>(fx_view_mode);
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
            sp.fx_params[0] = sp.fx_params[1] = sp.fx_params[2] = sp.fx_params[3] = 0.0F;
            sp.fx_params2[0] = sp.fx_params2[1] = sp.fx_params2[2] = sp.fx_params2[3] = 0.0F;
            sp.fx_params3[0] = sp.fx_params3[1] = sp.fx_params3[2] = sp.fx_params3[3] = 0.0F;
            sp.camera_pos[0] = sp.camera_pos[1] = sp.camera_pos[2] = sp.camera_pos[3] = 0.0F;
            sp.fx_params4[0] = sp.fx_params4[1] = sp.fx_params4[2] = sp.fx_params4[3] = 0.0F;

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
                ImGui::DockBuilderDockWindow("Outliner",  dock_left);
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
                // Rotation (Euler XYZ in degrees). Converts to/from
                // quaternion every frame so the underlying Transform
                // stays canonical.
                ImGui::SeparatorText("Rotation (deg)");
                {
                    static cd::math::Quatf pre {};
                    const cd::math::Quatf& q = lt->value.rotation;
                    // Quat to Euler XYZ (radians) — small approximation
                    // works for inspector readout, gimbal-locked at
                    // pitch == 90 (rare for editor poses).
                    const float sinp = 2.0F * (q.w * q.x + q.y * q.z);
                    const float cosp = 1.0F - 2.0F * (q.x * q.x + q.y * q.y);
                    const float pitch = std::atan2(sinp, cosp);
                    float t2 = 2.0F * (q.w * q.y - q.z * q.x);
                    t2 = std::clamp(t2, -1.0F, 1.0F);
                    const float yaw = std::asin(t2);
                    const float siny = 2.0F * (q.w * q.z + q.x * q.y);
                    const float cosy = 1.0F - 2.0F * (q.y * q.y + q.z * q.z);
                    const float roll = std::atan2(siny, cosy);
                    constexpr float kRad2Deg = 57.2957795F;
                    float eul[3] { pitch * kRad2Deg, yaw * kRad2Deg, roll * kRad2Deg };
                    bool changed = ImGui::DragFloat3("##rot", eul, 1.0F, -180.0F, 180.0F, "%.1f");
                    if (ImGui::IsItemActivated()) pre = lt->value.rotation;
                    if (changed)
                    {
                        constexpr float kDeg2Rad = 0.01745329F;
                        // Rebuild quaternion from Euler XYZ (intrinsic).
                        const float cx = std::cos(eul[0] * kDeg2Rad * 0.5F);
                        const float sx = std::sin(eul[0] * kDeg2Rad * 0.5F);
                        const float cy = std::cos(eul[1] * kDeg2Rad * 0.5F);
                        const float sy = std::sin(eul[1] * kDeg2Rad * 0.5F);
                        const float cz = std::cos(eul[2] * kDeg2Rad * 0.5F);
                        const float sz = std::sin(eul[2] * kDeg2Rad * 0.5F);
                        lt->value.rotation = {
                            sx*cy*cz - cx*sy*sz,
                            cx*sy*cz + sx*cy*sz,
                            cx*cy*sz - sx*sy*cz,
                            cx*cy*cz + sx*sy*sz };
                    }
                    if (ImGui::IsItemDeactivatedAfterEdit())
                    {
                        // Compute delta-rotation = current * inverse(pre)
                        cd::math::Quatf cur = lt->value.rotation;
                        cd::math::Quatf inv_pre {
                            -pre.x, -pre.y, -pre.z, pre.w };
                        cd::math::Quatf delta {
                            cur.w*inv_pre.x + cur.x*inv_pre.w + cur.y*inv_pre.z - cur.z*inv_pre.y,
                            cur.w*inv_pre.y - cur.x*inv_pre.z + cur.y*inv_pre.w + cur.z*inv_pre.x,
                            cur.w*inv_pre.z + cur.x*inv_pre.y - cur.y*inv_pre.x + cur.z*inv_pre.w,
                            cur.w*inv_pre.w - cur.x*inv_pre.x - cur.y*inv_pre.y - cur.z*inv_pre.z };
                        const float mag = std::abs(delta.x) + std::abs(delta.y) +
                                          std::abs(delta.z) + std::abs(1.0F - delta.w);
                        if (mag > 1e-4F)
                        {
                            lt->value.rotation = pre;
                            history.push(std::make_unique<cd::editor::RotateCommand>(
                                scene, ent.handle, delta));
                            log_push("drag: Rotate " + ent.name);
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
                // Material tint (DragFloat3 RGB). No undo entry yet —
                // ComponentEditCommand lands with the v1.7 ECS work.
                ImGui::SeparatorText("Tint");
                {
                    float rgb[3] { ent.tint.x, ent.tint.y, ent.tint.z };
                    if (ImGui::ColorEdit3("##tint", rgb,
                                          ImGuiColorEditFlags_NoInputs))
                    {
                        ent.tint = { rgb[0], rgb[1], rgb[2] };
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

        // ---- R-Showcase panel: unified R1-R8 feature toggles ----
        // Single panel listing every realism-roadmap feature with
        // an in-place checkbox/slider so the user can experience the
        // engine's full capability surface from one place.
        ImGui::Begin("R-Showcase");
        ImGui::TextDisabled("CHROMODYNAMIC realism roadmap (live)");
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.4F, 0.9F, 0.4F, 1), "R1  HDR cubemap IBL");
        ImGui::SameLine(); ImGui::TextDisabled("(spec 6mip + diff 16 + brdf 64x64)");
        ImGui::TextColored(ImVec4(0.4F, 0.9F, 0.4F, 1), "R2  Material textures");
        ImGui::SameLine(); ImGui::TextDisabled("(albedo + normal + MR + AO)");
        if (ImGui::CollapsingHeader("R2-Debug  View modes (see each map)"))
        {
            const char* labels[] = { "Final", "Albedo", "World normal",
                                      "MR (G=rough,B=metal)", "AO",
                                      "Perturbed normal", "UVs" };
            for (int i = 0; i < 7; ++i)
            {
                if (ImGui::RadioButton(labels[i], fx_view_mode == i))
                    fx_view_mode = i;
            }
        }
        // Sun direction controller — drives the directional light + IBL
        // gate. Each axis [-1,1]; normalised before push fill.
        if (ImGui::CollapsingHeader("Sun direction"))
        {
            if (!lights.empty())
            {
                auto& sun = lights[0].light;
                bool d_changed = false;
                d_changed |= ImGui::SliderFloat("dir.x", &sun.direction.x, -1.0F, 1.0F);
                d_changed |= ImGui::SliderFloat("dir.y", &sun.direction.y, -1.0F, 1.0F);
                d_changed |= ImGui::SliderFloat("dir.z", &sun.direction.z, -1.0F, 1.0F);
                if (d_changed)
                {
                    const float dl = std::sqrt(sun.direction.x*sun.direction.x +
                                                sun.direction.y*sun.direction.y +
                                                sun.direction.z*sun.direction.z);
                    if (dl > 1e-4F)
                    {
                        sun.direction.x /= dl;
                        sun.direction.y /= dl;
                        sun.direction.z /= dl;
                    }
                }
                ImGui::SliderFloat("intensity (lx)", &sun.intensity, 0.0F, 200000.0F);
                if (ImGui::Button("Reset sun"))
                {
                    sun.direction = { -0.3F, -0.9F, -0.2F };
                    sun.intensity = 100000.0F;
                }
            }
        }
        if (ImGui::CollapsingHeader("R6  Advanced BRDFs"))
        {
            ImGui::SliderFloat("Clearcoat",  &fx_clearcoat_strength, 0.0F, 1.0F);
            ImGui::SliderFloat("Sheen",      &fx_sheen_strength,     0.0F, 1.0F);
            ImGui::SliderFloat("SSS (Burley)", &fx_sss_strength,     0.0F, 1.0F);
        }
        if (ImGui::CollapsingHeader("R7  Camera composition"))
        {
            ImGui::SliderFloat("Vignette",       &fx_vignette_strength, 0.0F, 1.0F);
            ImGui::SliderFloat("ChromAberration",&fx_chromab_strength,  0.0F, 1.0F);
            ImGui::SliderFloat("Film grain",     &fx_film_grain,        0.0F, 1.0F);
        }
        if (ImGui::CollapsingHeader("R4-FX  Inline scene post-fx (legacy)"))
        {
            ImGui::TextDisabled("inline fakes — composite owns the real versions");
            ImGui::SliderFloat("GTAO inline",  &fx_gtao_strength,        0.0F, 1.0F);
            ImGui::SliderFloat("Bloom inline", &fx_bloom_strength,       0.0F, 1.0F);
            ImGui::SliderFloat("SMAA inline",  &fx_smaa_strength,        0.0F, 1.0F);
            ImGui::SliderFloat("Height fog",   &fx_fog_density,          0.0F, 1.0F);
            ImGui::SliderFloat("Aerial persp", &fx_aerial_perspective,   0.0F, 1.0F);
        }
        if (ImGui::CollapsingHeader("R3  Composite post-fx (live)",
                                    ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::TextDisabled("single composite pass — AO/DOF/shafts/bloom/atmo");
            ImGui::SliderFloat("Exposure",           &fx_exposure,         0.1F, 10.0F);
            ImGui::SliderFloat("Saturation boost",   &fx_saturation_boost, 0.5F, 2.5F);
            ImGui::SliderFloat("Bloom strength",     &fx_bloom_post,       0.0F, 0.30F);
            ImGui::SliderFloat("AO strength",        &fx_ao_strength,      0.0F, 1.0F);
            ImGui::SliderFloat("DOF strength",       &fx_dof_strength,     0.0F, 1.0F);
            ImGui::SliderFloat("Light shafts",       &fx_shafts_strength,  0.0F, 1.5F);
            ImGui::SliderFloat("SSR strength",       &fx_ssr_strength,     0.0F, 1.0F);
            ImGui::SliderFloat("Motion blur",        &fx_motion_blur,      0.0F, 1.0F);
            ImGui::SliderFloat("TAA amount",         &fx_taa_amount,       0.0F, 0.97F);
            ImGui::TextDisabled("TAA: camera-velocity reprojection + 3x3 neighbourhood clamp");
        }
        if (ImGui::CollapsingHeader("R3  Frame-graph + advanced post-fx"))
        {
            ImGui::TextDisabled("composite-inline live: AO/SSR/TAA/motion blur/DOF/shafts");
        }
        if (ImGui::CollapsingHeader("R4  GI (ReSTIR / DDGI / NRC)"))
        {
            ImGui::TextDisabled("queued v1.7 (needs RT compute pipe)");
        }
        if (ImGui::CollapsingHeader("R5  Volumetrics"))
        {
            ImGui::TextDisabled("clouds + light shafts queued v1.7");
        }
        if (ImGui::CollapsingHeader("R8  HDR10 display output"))
        {
            ImGui::Checkbox("HDR10 request (queued swapchain rework)",
                            &fx_hdr10_request);
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

        // ---- Outliner (gap #18 cd::world_container preview) ----
        // Read-only view of the World > Project > Level > Layer tree.
        // Entity grouping under layers is the editor-v1.6 follow-up;
        // today this panel exists to surface the model + let the
        // user inspect names/bounds/postfx overrides.
        ImGui::Begin("Outliner");
        if (ImGui::TreeNodeEx(cd_world.name().data(),
                              ImGuiTreeNodeFlags_DefaultOpen))
        {
            auto* proj = cd_world.project();
            if (proj == nullptr)
            {
                ImGui::TextDisabled("(no project)");
            }
            else
            {
                std::string proj_lbl { proj->name() };
                if (ImGui::TreeNodeEx((proj_lbl + "##proj").c_str(),
                                      ImGuiTreeNodeFlags_DefaultOpen))
                {
                    for (std::size_t li = 0; li < proj->level_count(); ++li)
                    {
                        auto* lvl = proj->level(li);
                        if (lvl == nullptr) continue;
                        std::string lvl_lbl { lvl->name() };
                        const auto& b = lvl->bounds();
                        if (ImGui::TreeNodeEx(
                                (lvl_lbl + "##l" + std::to_string(li)).c_str(),
                                ImGuiTreeNodeFlags_DefaultOpen))
                        {
                            ImGui::TextDisabled("bounds  [%.1f, %.1f, %.1f] -> [%.1f, %.1f, %.1f]",
                                static_cast<double>(b.min.x),
                                static_cast<double>(b.min.y),
                                static_cast<double>(b.min.z),
                                static_cast<double>(b.max.x),
                                static_cast<double>(b.max.y),
                                static_cast<double>(b.max.z));
                            for (std::size_t yi = 0; yi < lvl->layer_count(); ++yi)
                            {
                                auto* ly = lvl->layer(yi);
                                if (ly == nullptr) continue;
                                std::string ly_lbl { ly->name() };
                                const bool active = (yi == lvl->active_layer());
                                if (active)
                                    ImGui::PushStyleColor(ImGuiCol_Text,
                                        ImVec4(1.0F, 0.85F, 0.0F, 1.0F));
                                ImGui::Bullet();
                                ImGui::Text("%s%s%s%s",
                                    ly_lbl.c_str(),
                                    active        ? " (active)" : "",
                                    ly->locked()  ? " [locked]" : "",
                                    !ly->visible() ? " [hidden]" : "");
                                if (active) ImGui::PopStyleColor();
                            }
                            ImGui::TreePop();
                        }
                    }
                    ImGui::TreePop();
                }
            }
            ImGui::TreePop();
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

            // CCT + intensity sliders. CCT-driven palette is the default,
            // but a raw RGB picker is available when the user wants an
            // arbitrary tint. Setting RGB sets kelvin to 0 so the per-
            // frame CCT->RGB rebake won't overwrite the manual choice.
            ImGui::SliderFloat("CCT (K)", &row.kelvin, 0.0F, 15000.0F, "%.0f K");
            {
                float rgb[3] {
                    row.light.color.x,
                    row.light.color.y,
                    row.light.color.z };
                if (ImGui::ColorEdit3("colour (RGB)", rgb,
                                      ImGuiColorEditFlags_NoInputs |
                                      ImGuiColorEditFlags_Float))
                {
                    row.light.color = { rgb[0], rgb[1], rgb[2] };
                    row.kelvin      = 0.0F;
                }
            }
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
                        // Derive tangent + bitangent from L.direction
                        // exactly the way the FS does — so when the user
                        // rotates the area light's direction via the
                        // Inspector or gizmo, the visual rectangle
                        // rotates with it. Closes 'area donunce gorseli
                        // donmuyor' bug.
                        cd::math::Vec3f ln = L.direction;
                        const float lnl = std::sqrt(ln.x*ln.x + ln.y*ln.y + ln.z*ln.z);
                        if (lnl > 1e-5F) { ln.x/=lnl; ln.y/=lnl; ln.z/=lnl; }
                        else             { ln = { 0.0F, 0.0F, -1.0F }; }
                        const cd::math::Vec3f up_ref =
                            (std::abs(ln.y) > 0.95F)
                            ? cd::math::Vec3f { 1.0F, 0.0F, 0.0F }
                            : cd::math::Vec3f { 0.0F, 1.0F, 0.0F };
                        cd::math::Vec3f t {
                            up_ref.y*ln.z - up_ref.z*ln.y,
                            up_ref.z*ln.x - up_ref.x*ln.z,
                            up_ref.x*ln.y - up_ref.y*ln.x };
                        const float tl = std::sqrt(t.x*t.x + t.y*t.y + t.z*t.z);
                        if (tl > 1e-5F) { t.x/=tl; t.y/=tl; t.z/=tl; }
                        cd::math::Vec3f b {
                            ln.y*t.z - ln.z*t.y,
                            ln.z*t.x - ln.x*t.z,
                            ln.x*t.y - ln.y*t.x };
                        // Project 4 corners.
                        const float hw = L.area_width * 0.5F, hh = L.area_height * 0.5F;
                        cd::math::Vec3f c0 {
                            L.position.x - t.x * hw - b.x * hh,
                            L.position.y - t.y * hw - b.y * hh,
                            L.position.z - t.z * hw - b.z * hh };
                        cd::math::Vec3f c1 {
                            L.position.x + t.x * hw - b.x * hh,
                            L.position.y + t.y * hw - b.y * hh,
                            L.position.z + t.z * hw - b.z * hh };
                        cd::math::Vec3f c2 {
                            L.position.x + t.x * hw + b.x * hh,
                            L.position.y + t.y * hw + b.y * hh,
                            L.position.z + t.z * hw + b.z * hh };
                        cd::math::Vec3f c3 {
                            L.position.x - t.x * hw + b.x * hh,
                            L.position.y - t.y * hw + b.y * hh,
                            L.position.z - t.z * hw + b.z * hh };
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
                    // Ray-plane projection of a screen pixel onto the
                    // active axis. Returns the signed distance along
                    // the axis from `world_start` to the hit point,
                    // or std::optional() if the plane is too parallel
                    // to the camera ray (caller falls back to the
                    // screen-space dot method below). The plane is
                    // the one containing the axis with normal
                    // = normalize(cross(axis, cross(view, axis))) —
                    // the most camera-facing orientation. Closes the
                    // "gizmo ileri-geri yapinca objeler isinlaniyor"
                    // teleport bug.
                    auto ray_axis_offset = [&](cd::editor::GizmoAxis axis,
                                               ImVec2 mouse_pixel,
                                               cd::math::Vec3f world_start)
                        -> std::optional<float>
                    {
                        const float vw = static_cast<float>(window.width());
                        const float vh = static_cast<float>(window.height());
                        if (vw < 1 || vh < 1) return std::nullopt;
                        // Camera basis (same path as pick).
                        cd::math::Vec3f fwd {
                            cam.target.x - cam.eye.x,
                            cam.target.y - cam.eye.y,
                            cam.target.z - cam.eye.z };
                        const float fl = std::sqrt(fwd.x*fwd.x + fwd.y*fwd.y + fwd.z*fwd.z);
                        if (fl < 1e-5F) return std::nullopt;
                        fwd.x/=fl; fwd.y/=fl; fwd.z/=fl;
                        cd::math::Vec3f wup { 0, 1, 0 };
                        cd::math::Vec3f rgt {
                            fwd.y*wup.z - fwd.z*wup.y,
                            fwd.z*wup.x - fwd.x*wup.z,
                            fwd.x*wup.y - fwd.y*wup.x };
                        const float rl = std::sqrt(rgt.x*rgt.x + rgt.y*rgt.y + rgt.z*rgt.z);
                        if (rl < 1e-5F) return std::nullopt;
                        rgt.x/=rl; rgt.y/=rl; rgt.z/=rl;
                        cd::math::Vec3f up_v {
                            rgt.y*fwd.z - rgt.z*fwd.y,
                            rgt.z*fwd.x - rgt.x*fwd.z,
                            rgt.x*fwd.y - rgt.y*fwd.x };
                        const float ndc_x = (2.0F * mouse_pixel.x / vw) - 1.0F;
                        const float ndc_y = 1.0F - (2.0F * mouse_pixel.y / vh);
                        const float tan_half = std::tan(cam.fov_y * 0.5F);
                        const float sx = (vw / vh) * tan_half;
                        const float sy = tan_half;
                        cd::math::Vec3f rdir {
                            fwd.x + rgt.x * ndc_x * sx + up_v.x * ndc_y * sy,
                            fwd.y + rgt.y * ndc_x * sx + up_v.y * ndc_y * sy,
                            fwd.z + rgt.z * ndc_x * sx + up_v.z * ndc_y * sy };
                        const float rdl = std::sqrt(rdir.x*rdir.x + rdir.y*rdir.y + rdir.z*rdir.z);
                        if (rdl < 1e-5F) return std::nullopt;
                        rdir.x/=rdl; rdir.y/=rdl; rdir.z/=rdl;
                        // Axis unit vector + plane normal.
                        cd::math::Vec3f a { 0, 0, 0 };
                        if (axis == cd::editor::GizmoAxis::kX) a = { 1, 0, 0 };
                        else if (axis == cd::editor::GizmoAxis::kY) a = { 0, 1, 0 };
                        else if (axis == cd::editor::GizmoAxis::kZ) a = { 0, 0, 1 };
                        cd::math::Vec3f c1 {
                            fwd.y*a.z - fwd.z*a.y,
                            fwd.z*a.x - fwd.x*a.z,
                            fwd.x*a.y - fwd.y*a.x };
                        cd::math::Vec3f n {
                            a.y*c1.z - a.z*c1.y,
                            a.z*c1.x - a.x*c1.z,
                            a.x*c1.y - a.y*c1.x };
                        const float nl = std::sqrt(n.x*n.x + n.y*n.y + n.z*n.z);
                        if (nl < 1e-5F) return std::nullopt;
                        n.x/=nl; n.y/=nl; n.z/=nl;
                        const float denom = rdir.x*n.x + rdir.y*n.y + rdir.z*n.z;
                        if (std::fabs(denom) < 1e-4F) return std::nullopt;
                        const float t = ((world_start.x - cam.eye.x) * n.x +
                                         (world_start.y - cam.eye.y) * n.y +
                                         (world_start.z - cam.eye.z) * n.z) / denom;
                        if (t < 0.0F) return std::nullopt;
                        const cd::math::Vec3f hit {
                            cam.eye.x + rdir.x * t,
                            cam.eye.y + rdir.y * t,
                            cam.eye.z + rdir.z * t };
                        return (hit.x - world_start.x) * a.x +
                               (hit.y - world_start.y) * a.y +
                               (hit.z - world_start.z) * a.z;
                    };

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
                        // Capture light start state for gaps #16/#17:
                        // R-mode rotates light.direction; S-mode scales
                        // light.range / area_width / area_height.
                        if (selected_kind == SelKind::kLight &&
                            selected >= 0 &&
                            selected < static_cast<int>(lights.size()))
                        {
                            const auto& L = lights[static_cast<std::size_t>(selected)].light;
                            light_drag_dir_start    = L.direction;
                            light_drag_range_start  = L.range;
                            light_drag_area_w_start = L.area_width;
                            light_drag_area_h_start = L.area_height;
                        }
                        // Capture the initial ray-plane axis offset
                        // so subsequent moves give delta = current -
                        // initial (no jump at click).
                        if (auto off = ray_axis_offset(best, mp, *target_pos);
                            off.has_value())
                        {
                            gizmo_drag_initial_offset = *off;
                            gizmo_drag_use_ray_plane  = true;
                        }
                        else
                        {
                            gizmo_drag_use_ray_plane = false;
                        }
                    }
                    if (gizmo.is_dragging())
                    {
                        ImVec2 axis_screen_end = p_x;
                        if (gizmo.active_axis() == cd::editor::GizmoAxis::kY) axis_screen_end = p_y;
                        else if (gizmo.active_axis() == cd::editor::GizmoAxis::kZ) axis_screen_end = p_z;
                        const float ax_dx = axis_screen_end.x - p_org.x;
                        const float ax_dy = axis_screen_end.y - p_org.y;
                        const float ax_len_px = std::sqrt(ax_dx*ax_dx + ax_dy*ax_dy);
                        // Two paths: ray-plane (preferred, robust) vs
                        // screen-space dot (fallback for rotate/scale
                        // which use angular / exponential math).
                        float delta_world = 0.0F;
                        if (gizmo_drag_use_ray_plane &&
                            gizmo_mode == GizmoMode::kTranslate)
                        {
                            if (auto off = ray_axis_offset(gizmo.active_axis(),
                                                           mp, gizmo_drag_world_start);
                                off.has_value())
                            {
                                delta_world = *off - gizmo_drag_initial_offset;
                            }
                        }
                        if (ax_len_px > 1.0F)
                        {
                            // Screen-space path (rotate/scale, or
                            // ray-plane fallback). delta_world stays 0
                            // for translate when ray-plane worked.
                            const float nx = ax_dx / ax_len_px, ny = ax_dy / ax_len_px;
                            const float mouse_dx = mp.x - gizmo_drag_anchor.x;
                            const float mouse_dy = mp.y - gizmo_drag_anchor.y;
                            const float dot_px = mouse_dx * nx + mouse_dy * ny;
                            const float world_per_px = kAxisLen / ax_len_px;
                            if (!gizmo_drag_use_ray_plane ||
                                gizmo_mode != GizmoMode::kTranslate)
                            {
                                delta_world = dot_px * world_per_px;
                            }
                        }
                        if (ax_len_px > 1.0F || gizmo_drag_use_ray_plane)
                        {
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
                                    const float factor = std::exp(delta_world * 0.5F);
                                    if (lt != nullptr)
                                    {
                                        cd::math::Vec3f cur = gizmo_drag_scale_start;
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
                                    }
                                    else if (selected_kind == SelKind::kLight &&
                                             selected >= 0 &&
                                             selected < static_cast<int>(lights.size()))
                                    {
                                        // gap #17: scale-mode gizmo on a
                                        // light edits its area-of-effect.
                                        // Point/Spot: range. Rect-area:
                                        // X=width, Y=height. Disk: width
                                        // (= radius in our convention).
                                        auto& L = lights[static_cast<std::size_t>(selected)].light;
                                        const auto axis = gizmo.active_axis();
                                        if (L.type == cd::light::LightType::kPoint ||
                                            L.type == cd::light::LightType::kSpot)
                                        {
                                            float r = light_drag_range_start * factor;
                                            if (r < 0.1F) r = 0.1F;
                                            if (r > 200.0F) r = 200.0F;
                                            L.range = r;
                                        }
                                        else if (L.type == cd::light::LightType::kRectArea)
                                        {
                                            float w = light_drag_area_w_start;
                                            float h = light_drag_area_h_start;
                                            if (axis == cd::editor::GizmoAxis::kX ||
                                                axis == cd::editor::GizmoAxis::kZ) w *= factor;
                                            if (axis == cd::editor::GizmoAxis::kY ||
                                                axis == cd::editor::GizmoAxis::kZ) h *= factor;
                                            L.area_width  = std::clamp(w, 0.05F, 50.0F);
                                            L.area_height = std::clamp(h, 0.05F, 50.0F);
                                        }
                                        else if (L.type == cd::light::LightType::kDiskArea)
                                        {
                                            float w = light_drag_area_w_start * factor;
                                            L.area_width  = std::clamp(w, 0.05F, 50.0F);
                                            L.area_height = L.area_width;  // radius
                                        }
                                    }
                                    break;
                                }
                                case GizmoMode::kRotate:
                                {
                                    // Compute the angle the mouse has swept around the
                                    // gizmo center since drag start (atan2 difference).
                                    const float anchor_dx = gizmo_drag_anchor.x - p_org.x;
                                    const float anchor_dy = gizmo_drag_anchor.y - p_org.y;
                                    const float cur_dx    = mp.x - p_org.x;
                                    const float cur_dy    = mp.y - p_org.y;
                                    if (std::sqrt(anchor_dx*anchor_dx + anchor_dy*anchor_dy) < 5.0F)
                                        break;  // too close to center, ignore
                                    const float a_anchor = std::atan2(anchor_dy, anchor_dx);
                                    const float a_now    = std::atan2(cur_dy,    cur_dx);
                                    float ang = a_now - a_anchor;
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
                                    if (lt != nullptr)
                                    {
                                        const auto& a = q;
                                        const auto& b = gizmo_drag_rot_start;
                                        lt->value.rotation = cd::math::Quatf {
                                            a.w*b.x + a.x*b.w + a.y*b.z - a.z*b.y,
                                            a.w*b.y - a.x*b.z + a.y*b.w + a.z*b.x,
                                            a.w*b.z + a.x*b.y - a.y*b.x + a.z*b.w,
                                            a.w*b.w - a.x*b.x - a.y*b.y - a.z*b.z };
                                    }
                                    else if (selected_kind == SelKind::kLight &&
                                             selected >= 0 &&
                                             selected < static_cast<int>(lights.size()))
                                    {
                                        // gap #16: rotate-mode gizmo
                                        // rotates a light's direction.
                                        // Apply q to light_drag_dir_start
                                        // (which was captured at click)
                                        // — pure vector rotation v' =
                                        // q * v * q^-1.
                                        const auto v = light_drag_dir_start;
                                        // q*(0,v) = (-q.xyz . v, q.w*v + q.xyz × v)
                                        const cd::math::Vec3f t {
                                            q.w * v.x + q.y * v.z - q.z * v.y,
                                            q.w * v.y + q.z * v.x - q.x * v.z,
                                            q.w * v.z + q.x * v.y - q.y * v.x };
                                        const float tw = -(q.x * v.x + q.y * v.y + q.z * v.z);
                                        // (result) = (q*v) * q^-1, scalar-out
                                        // ignored, vec-out = result.
                                        const cd::math::Vec3f rotated {
                                            tw * -q.x + t.x * q.w + t.y * -q.z - t.z * -q.y,
                                            tw * -q.y - t.x * -q.z + t.y * q.w + t.z * -q.x,
                                            tw * -q.z + t.x * -q.y - t.y * -q.x + t.z * q.w };
                                        const float L = std::sqrt(rotated.x*rotated.x +
                                                                  rotated.y*rotated.y +
                                                                  rotated.z*rotated.z);
                                        if (L > 1e-5F)
                                        {
                                            lights[static_cast<std::size_t>(selected)].light.direction =
                                                { rotated.x / L, rotated.y / L, rotated.z / L };
                                        }
                                    }
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

        // R3: end the HDR scene pass, transition HDR -> ShaderResource,
        // begin the composite render pass on the swapchain, draw the
        // fullscreen-triangle composite material (which samples HDR
        // and applies tonemap + saturation + gamma), then let ImGui
        // draw on top.
        cmd.end_render_pass();
        {
            // Five barriers: HDR + 3 G-Buffer targets -> ShaderResource
            // (read by bloom prefilter + composite); scene depth ->
            // ShaderResource (composite-inline GTAO + SSR).
            std::array<cd::rhi::TextureBarrier, 5> hb {
                cd::rhi::TextureBarrier {
                    .texture = hdr_target.image,
                    .from    = cd::rhi::ResourceState::kColorAttachment,
                    .to      = cd::rhi::ResourceState::kShaderResource,
                    .range   = { 0, 1, 0, 1 } },
                cd::rhi::TextureBarrier {
                    .texture = gbuf_normal.image,
                    .from    = cd::rhi::ResourceState::kColorAttachment,
                    .to      = cd::rhi::ResourceState::kShaderResource,
                    .range   = { 0, 1, 0, 1 } },
                cd::rhi::TextureBarrier {
                    .texture = gbuf_albedo.image,
                    .from    = cd::rhi::ResourceState::kColorAttachment,
                    .to      = cd::rhi::ResourceState::kShaderResource,
                    .range   = { 0, 1, 0, 1 } },
                cd::rhi::TextureBarrier {
                    .texture = gbuf_mr.image,
                    .from    = cd::rhi::ResourceState::kColorAttachment,
                    .to      = cd::rhi::ResourceState::kShaderResource,
                    .range   = { 0, 1, 0, 1 } },
                cd::rhi::TextureBarrier {
                    .texture = depth.image,
                    .from    = cd::rhi::ResourceState::kDepthWrite,
                    .to      = cd::rhi::ResourceState::kShaderResource,
                    .range   = { 0, 1, 0, 1 } } };
            cmd.barrier({}, hb);
        }

        // R3 — Bloom chain. 7 fullscreen-triangle passes against the
        // dedicated bloom mip chain (each pass owns one render target,
        // writes its full extent, and ends as kShaderResource so the
        // next pass can sample it). All 7 share the composite VS.
        auto run_bloom_pass = [&](cd::material::Material& mat,
                                  cd::material::MaterialInstance& inst,
                                  ColorTarget& dst,
                                  cd::rhi::LoadOp load_op,
                                  std::span<const std::byte> push_bytes,
                                  bool first_frame) {
            std::array<cd::rhi::TextureBarrier, 1> tb {
                cd::rhi::TextureBarrier {
                    .texture = dst.image,
                    .from    = first_frame ? cd::rhi::ResourceState::kUndefined
                                           : cd::rhi::ResourceState::kShaderResource,
                    .to      = cd::rhi::ResourceState::kColorAttachment,
                    .range   = { 0, 1, 0, 1 } } };
            cmd.barrier({}, tb);

            std::array<cd::rhi::ColorAttachmentInfo, 1> ca {
                cd::rhi::ColorAttachmentInfo {
                    .view = dst.view,
                    .load_op = load_op,
                    .store_op = cd::rhi::StoreOp::kStore,
                    .clear_color = { .f32 = { 0.0F, 0.0F, 0.0F, 1.0F } } } };
            cd::rhi::RenderPassBeginInfo rp {};
            rp.render_area = cd::rhi::Rect2D { {0,0}, dst.extent };
            rp.color_attachments = ca;
            rp.depth_stencil = nullptr;
            cmd.begin_render_pass(rp);
            cmd.set_viewport(cd::rhi::Viewport {
                0.0F, 0.0F,
                static_cast<float>(dst.extent.width),
                static_cast<float>(dst.extent.height),
                0.0F, 1.0F });
            cmd.set_scissor(cd::rhi::Rect2D { {0,0}, dst.extent });
            mat.apply(cmd);
            inst.bind(cmd, 0);
            if (!push_bytes.empty())
            {
                cmd.push_constants(mat.pipeline_layout(),
                                   cd::rhi::ShaderStage::kFragment,
                                   0,
                                   static_cast<std::uint32_t>(push_bytes.size()),
                                   push_bytes.data());
            }
            cmd.draw(3, 1, 0, 0);
            cmd.end_render_pass();

            std::array<cd::rhi::TextureBarrier, 1> tb2 {
                cd::rhi::TextureBarrier {
                    .texture = dst.image,
                    .from    = cd::rhi::ResourceState::kColorAttachment,
                    .to      = cd::rhi::ResourceState::kShaderResource,
                    .range   = { 0, 1, 0, 1 } } };
            cmd.barrier({}, tb2);
        };

        const bool bloom_first_frame = (frame_idx == 0);
        // 1) Prefilter: HDR -> mip0 (soft-knee threshold).
        {
            BloomPrefilterPush bpp {};
            bpp.params[0] = 1.10F;  // threshold (linear HDR units)
            bpp.params[1] = 0.50F;  // knee
            bpp.params[2] = 0.0F;
            bpp.params[3] = 0.0F;
            std::span<const std::byte> bytes {
                reinterpret_cast<const std::byte*>(&bpp), sizeof(bpp) };
            run_bloom_pass(bloom_prefilter_material, bloom_prefilter_inst,
                           bloom_chain.mips[0],
                           cd::rhi::LoadOp::kClear, bytes,
                           bloom_first_frame);
        }
        // 2) Downsample chain: mip0 -> 1, 1 -> 2, 2 -> 3.
        for (std::uint32_t i = 0; i < 3; ++i)
        {
            run_bloom_pass(bloom_downsample_material, bloom_down_insts[i],
                           bloom_chain.mips[i + 1],
                           cd::rhi::LoadOp::kClear, {},
                           bloom_first_frame);
        }
        // 3) Upsample chain: mip3 -> 2, 2 -> 1, 1 -> 0 (additive blend).
        //    Load op must be Load to preserve the prior pass's output we're
        //    adding onto. radius 1.0 / intensity 1.0 (full contribution).
        for (std::uint32_t i = 0; i < 3; ++i)
        {
            const std::uint32_t dst_index = 3U - 1U - i;  // 2, 1, 0
            BloomUpsamplePush bup {};
            bup.params[0] = 1.0F;   // radius (px scale)
            bup.params[1] = 1.0F;   // intensity per level
            bup.params[2] = 0.0F;
            bup.params[3] = 0.0F;
            std::span<const std::byte> bytes {
                reinterpret_cast<const std::byte*>(&bup), sizeof(bup) };
            run_bloom_pass(bloom_upsample_material, bloom_up_insts[i],
                           bloom_chain.mips[dst_index],
                           cd::rhi::LoadOp::kLoad, bytes,
                           bloom_first_frame);
        }

        // TAA ping-pong selection. composite_insts[read_idx] has its
        // binding=4 wired to history_targets[read_idx]; we render into
        // history_targets[write_idx] (= the OTHER one) as the 2nd
        // color attachment so next frame can read it.
        const std::uint32_t read_idx  = frame_idx & 1U;
        const std::uint32_t write_idx = 1U - read_idx;

        // Barrier the two history targets: read side → ShaderResource,
        // write side → ColorAttachment.
        {
            std::array<cd::rhi::TextureBarrier, 2> hb {
                cd::rhi::TextureBarrier {
                    .texture = history_targets[read_idx].image,
                    .from    = history_states[read_idx],
                    .to      = cd::rhi::ResourceState::kShaderResource,
                    .range   = { 0, 1, 0, 1 } },
                cd::rhi::TextureBarrier {
                    .texture = history_targets[write_idx].image,
                    .from    = history_states[write_idx],
                    .to      = cd::rhi::ResourceState::kColorAttachment,
                    .range   = { 0, 1, 0, 1 } } };
            cmd.barrier({}, hb);
            history_states[read_idx]  = cd::rhi::ResourceState::kShaderResource;
            history_states[write_idx] = cd::rhi::ResourceState::kColorAttachment;
        }

        std::array<cd::rhi::ColorAttachmentInfo, 2> swap_attach {
            cd::rhi::ColorAttachmentInfo { .view = frame.swapchain_image_view,
                                          .load_op = cd::rhi::LoadOp::kClear,
                                          .store_op = cd::rhi::StoreOp::kStore,
                                          .clear_color = { .f32 = { 0.0F, 0.0F, 0.0F, 1.0F } } },
            cd::rhi::ColorAttachmentInfo { .view = history_targets[write_idx].view,
                                          .load_op = cd::rhi::LoadOp::kClear,
                                          .store_op = cd::rhi::StoreOp::kStore,
                                          .clear_color = { .f32 = { 0.0F, 0.0F, 0.0F, 1.0F } } } };
        cd::rhi::RenderPassBeginInfo swap_rp {};
        swap_rp.render_area = cd::rhi::Rect2D { {0,0}, frame.extent };
        swap_rp.color_attachments = swap_attach;
        swap_rp.depth_stencil = nullptr;
        cmd.begin_render_pass(swap_rp);
        cmd.set_viewport(cd::rhi::Viewport {
            0.0F, 0.0F,
            static_cast<float>(frame.extent.width),
            static_cast<float>(frame.extent.height),
            0.0F, 1.0F });
        cmd.set_scissor(cd::rhi::Rect2D { {0,0}, frame.extent });
        composite_material.apply(cmd);
        composite_insts[read_idx].bind(cmd, 0);
        CompositePush cp {};
        cp.fx[0] = static_cast<float>(tonemap_op);
        cp.fx[1] = fx_exposure;
        cp.fx[2] = fx_saturation_boost;
        cp.fx[3] = fx_bloom_post;
        cp.ao[0] = fx_ao_strength;
        cp.ao[1] = 4.0F;   // ao_radius_px — 4 px ring radius
        cp.ao[2] = cam.near_z;
        cp.ao[3] = cam.far_z;
        // DOF — wired from the existing UI slider. Focus on cam.target
        // (length(eye - target)), default 4 m range, 8 px max blur.
        const float focus_dist = cd::math::length(cd::math::Vec3f {
            cam.eye.x - cam.target.x,
            cam.eye.y - cam.target.y,
            cam.eye.z - cam.target.z });
        cp.dof[0] = fx_dof_strength;
        cp.dof[1] = focus_dist;
        cp.dof[2] = 4.0F;   // focus range (m) — pixels within ±range stay sharp
        cp.dof[3] = 8.0F;   // max blur radius (px)
        // Light shafts — project the first enabled directional light's
        // sun position to screen-space UV (sun lives at infinity in
        // direction -L). If sun is behind camera (fwd_dot ≤ 0) we
        // signal disabled via negative strength.
        cp.shafts[0] = 0.5F;
        cp.shafts[1] = 0.5F;
        cp.shafts[2] = -1.0F;  // disabled until a directional light + visible sun
        cp.shafts[3] = 1.0F;
        cp.sun_col[0] = 0.0F; cp.sun_col[1] = 0.0F; cp.sun_col[2] = 0.0F; cp.sun_col[3] = 0.0F;
        for (const auto& lrow : lights)
        {
            if (!lrow.enabled) continue;
            if (lrow.light.type != cd::light::LightType::kDirectional) continue;
            const cd::math::Vec3f to_sun {
                -lrow.light.direction.x,
                -lrow.light.direction.y,
                -lrow.light.direction.z };
            // Compute camera basis (forward/right/up). Same construction
            // as the sky/PBR push setup right above.
            const cd::math::Vec3f cam_fwd_n {
                cam.target.x - cam.eye.x,
                cam.target.y - cam.eye.y,
                cam.target.z - cam.eye.z };
            const float cam_fwd_len = std::sqrt(
                cam_fwd_n.x * cam_fwd_n.x +
                cam_fwd_n.y * cam_fwd_n.y +
                cam_fwd_n.z * cam_fwd_n.z);
            if (cam_fwd_len < 1e-6F) break;
            const cd::math::Vec3f f {
                cam_fwd_n.x / cam_fwd_len,
                cam_fwd_n.y / cam_fwd_len,
                cam_fwd_n.z / cam_fwd_len };
            const cd::math::Vec3f shaft_up_axis { 0.0F, 1.0F, 0.0F };
            const cd::math::Vec3f r_raw {
                f.y * shaft_up_axis.z - f.z * shaft_up_axis.y,
                f.z * shaft_up_axis.x - f.x * shaft_up_axis.z,
                f.x * shaft_up_axis.y - f.y * shaft_up_axis.x };
            const float r_len = std::sqrt(r_raw.x * r_raw.x +
                                          r_raw.y * r_raw.y +
                                          r_raw.z * r_raw.z);
            if (r_len < 1e-6F) break;
            const cd::math::Vec3f r {
                r_raw.x / r_len, r_raw.y / r_len, r_raw.z / r_len };
            const cd::math::Vec3f u {
                r.y * f.z - r.z * f.y,
                r.z * f.x - r.x * f.z,
                r.x * f.y - r.y * f.x };
            const float fwd_dot = to_sun.x * f.x + to_sun.y * f.y + to_sun.z * f.z;
            if (fwd_dot <= 0.0F) break;  // sun behind camera
            const float r_dot = to_sun.x * r.x + to_sun.y * r.y + to_sun.z * r.z;
            const float u_dot = to_sun.x * u.x + to_sun.y * u.y + to_sun.z * u.z;
            const float aspect_l = static_cast<float>(frame.extent.width) /
                                   static_cast<float>(frame.extent.height);
            const float half_h_l = std::tan(cam.fov_y * 0.5F);
            const float half_w_l = half_h_l * aspect_l;
            const float sun_ndc_x = (r_dot / fwd_dot) / half_w_l;
            const float sun_ndc_y = (u_dot / fwd_dot) / half_h_l;
            cp.shafts[0] = 0.5F + 0.5F * sun_ndc_x;
            cp.shafts[1] = 0.5F - 0.5F * sun_ndc_y;
            // Fade strength near screen edges so shafts don't pop on
            // sun exit. Linear taper outside [-1, 1] NDC.
            float edge_fade = std::min(1.0F,
                std::max(0.0F, 1.0F - std::max(std::abs(sun_ndc_x),
                                               std::abs(sun_ndc_y)) - 0.0F));
            edge_fade = std::clamp(edge_fade, 0.0F, 1.0F);
            cp.shafts[2] = fx_shafts_strength * edge_fade;
            cp.shafts[3] = 3.5F;               // decay (per UV distance)
            cp.sun_col[0] = lrow.light.color.x;
            cp.sun_col[1] = lrow.light.color.y;
            cp.sun_col[2] = lrow.light.color.z;
            cp.sun_col[3] = 0.0F;
            break;
        }
        // Atmospheric fog (uniform exp-haze) + aerial perspective (sky
        // horizon tint with distance). Reuses the existing UI sliders
        // so the composite is now the *one* home for these effects.
        cp.atmo[0] = fx_fog_density;
        cp.atmo[1] = fx_aerial_perspective;
        cp.atmo[2] = fx_vignette_strength;
        cp.atmo[3] = fx_film_grain;
        cp.lens[0] = fx_chromab_strength;
        cp.lens[1] = 0.0F;
        cp.lens[2] = 0.0F;
        cp.lens[3] = 0.0F;
        // G-Buffer-aware ops: pack camera basis so the composite FS can
        // reconstruct world-space positions per pixel for SSR + normal-
        // aware AO. Match the same basis the sky shader uses (forward
        // = (target-eye)/|...|, right = forward × +Y, up = right ×
        // forward) so SSR rays project consistently.
        {
            const cd::math::Vec3f fwd_raw {
                cam.target.x - cam.eye.x,
                cam.target.y - cam.eye.y,
                cam.target.z - cam.eye.z };
            const float ssr_fl = std::sqrt(fwd_raw.x*fwd_raw.x +
                                           fwd_raw.y*fwd_raw.y +
                                           fwd_raw.z*fwd_raw.z);
            const cd::math::Vec3f fwd = (ssr_fl > 1e-6F)
                ? cd::math::Vec3f { fwd_raw.x / ssr_fl, fwd_raw.y / ssr_fl, fwd_raw.z / ssr_fl }
                : cd::math::Vec3f { 0.0F, 0.0F, -1.0F };
            constexpr cd::math::Vec3f cam_world_up { 0.0F, 1.0F, 0.0F };
            const cd::math::Vec3f r_raw {
                fwd.y * cam_world_up.z - fwd.z * cam_world_up.y,
                fwd.z * cam_world_up.x - fwd.x * cam_world_up.z,
                fwd.x * cam_world_up.y - fwd.y * cam_world_up.x };
            const float ssr_rl = std::sqrt(r_raw.x*r_raw.x +
                                           r_raw.y*r_raw.y +
                                           r_raw.z*r_raw.z);
            const cd::math::Vec3f right = (ssr_rl > 1e-6F)
                ? cd::math::Vec3f { r_raw.x / ssr_rl, r_raw.y / ssr_rl, r_raw.z / ssr_rl }
                : cd::math::Vec3f { 1.0F, 0.0F, 0.0F };
            const cd::math::Vec3f up_cam {
                right.y * fwd.z - right.z * fwd.y,
                right.z * fwd.x - right.x * fwd.z,
                right.x * fwd.y - right.y * fwd.x };
            const float aspect_l = static_cast<float>(frame.extent.width) /
                                   static_cast<float>(frame.extent.height);
            const float half_h_l = std::tan(cam.fov_y * 0.5F);
            const float half_w_l = half_h_l * aspect_l;
            cp.cam_right[0] = right.x; cp.cam_right[1] = right.y;
            cp.cam_right[2] = right.z; cp.cam_right[3] = half_w_l;
            cp.cam_up[0]    = up_cam.x; cp.cam_up[1]    = up_cam.y;
            cp.cam_up[2]    = up_cam.z; cp.cam_up[3]    = half_h_l;
            cp.cam_fwd[0]   = fwd.x;   cp.cam_fwd[1]   = fwd.y;
            // TAA alpha — first frame must blend 0 (history undefined).
            cp.cam_fwd[2]   = fwd.z;
            cp.cam_fwd[3]   = (frame_idx > 0) ? fx_taa_amount : 0.0F;
            cp.cam_pos[0]   = cam.eye.x; cp.cam_pos[1] = cam.eye.y;
            cp.cam_pos[2]   = cam.eye.z; cp.cam_pos[3] = 0.0F;
        }
        // SSR — wired from the existing UI slider; defaults to 0 (off).
        cp.ssr[0] = fx_ssr_strength;
        cp.ssr[1] = 25.0F;   // max distance (m)
        cp.ssr[2] = 24.0F;   // max steps
        cp.ssr[3] = 1.5F;    // edge-fade aggressiveness

        // Camera-velocity motion blur: pack the prev-frame basis. On
        // the very first frame, mirror current basis (zero velocity).
        {
            const auto& pb = prev_cam_basis;
            const bool first = !pb.valid;
            const cd::math::Vec3f pr  = first ? cd::math::Vec3f { cp.cam_right[0], cp.cam_right[1], cp.cam_right[2] } : pb.right;
            const cd::math::Vec3f pu  = first ? cd::math::Vec3f { cp.cam_up[0],    cp.cam_up[1],    cp.cam_up[2] }    : pb.up;
            const cd::math::Vec3f pf  = first ? cd::math::Vec3f { cp.cam_fwd[0],   cp.cam_fwd[1],   cp.cam_fwd[2] }   : pb.fwd;
            const cd::math::Vec3f pp  = first ? cd::math::Vec3f { cp.cam_pos[0],   cp.cam_pos[1],   cp.cam_pos[2] }   : pb.pos;
            const float phw = first ? cp.cam_right[3] : pb.half_w;
            const float phh = first ? cp.cam_up[3]    : pb.half_h;
            cp.prev_cam_right[0] = pr.x; cp.prev_cam_right[1] = pr.y;
            cp.prev_cam_right[2] = pr.z; cp.prev_cam_right[3] = phw;
            cp.prev_cam_up[0]    = pu.x; cp.prev_cam_up[1]    = pu.y;
            cp.prev_cam_up[2]    = pu.z; cp.prev_cam_up[3]    = phh;
            cp.prev_cam_fwd[0]   = pf.x; cp.prev_cam_fwd[1]   = pf.y;
            cp.prev_cam_fwd[2]   = pf.z; cp.prev_cam_fwd[3]   = fx_motion_blur;
            cp.prev_cam_pos[0]   = pp.x; cp.prev_cam_pos[1]   = pp.y;
            cp.prev_cam_pos[2]   = pp.z; cp.prev_cam_pos[3]   = 8.0F;  // sample count
        }

        cmd.push_constants(composite_material.pipeline_layout(),
                           cd::rhi::ShaderStage::kFragment,
                           0, sizeof(cp), &cp);
        cmd.draw(3, 1, 0, 0);

        // Snapshot current camera basis for next frame's velocity
        // reprojection. Done AFTER the push so the next frame can
        // reproject "where was this pixel one frame ago?".
        prev_cam_basis.right = { cp.cam_right[0], cp.cam_right[1], cp.cam_right[2] };
        prev_cam_basis.up    = { cp.cam_up[0],    cp.cam_up[1],    cp.cam_up[2] };
        prev_cam_basis.fwd   = { cp.cam_fwd[0],   cp.cam_fwd[1],   cp.cam_fwd[2] };
        prev_cam_basis.pos   = { cp.cam_pos[0],   cp.cam_pos[1],   cp.cam_pos[2] };
        prev_cam_basis.half_w = cp.cam_right[3];
        prev_cam_basis.half_h = cp.cam_up[3];
        prev_cam_basis.valid  = true;

        // ---- ImGui pass (on swapchain, after composite) ----
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
    destroy_mesh(device, knot_mesh);
    destroy_mesh(device, floor_mesh);
    destroy_mesh(device, pbr_sphere);
    depth.destroy(device);
    hdr_target.destroy(device);
    gbuf_normal.destroy(device);
    gbuf_albedo.destroy(device);
    gbuf_mr.destroy(device);
    for (auto& h : history_targets) h.destroy(device);
    bloom_chain.destroy(device);
    // Faz 1.6 CSM resources.
    shadow_target.destroy(device);
    device.destroy_sampler(shadow_sampler);
    device.destroy_buffer(shadow_ubo);
    device.destroy_buffer(lights_ubo);
    if (albedo_tex.view.is_valid())  device.destroy_texture_view(albedo_tex.view);
    if (albedo_tex.image.is_valid()) device.destroy_texture(albedo_tex.image);
    device.destroy_sampler(albedo_sampler);
    device.destroy_sampler(ibl_sampler);  // bug-hunt: was leaked
    // R1 IBL textures + views.
    if (gpu_spec_cube.view.is_valid())  device.destroy_texture_view(gpu_spec_cube.view);
    if (gpu_spec_cube.image.is_valid()) device.destroy_texture(gpu_spec_cube.image);
    if (gpu_diff_cube.view.is_valid())  device.destroy_texture_view(gpu_diff_cube.view);
    if (gpu_diff_cube.image.is_valid()) device.destroy_texture(gpu_diff_cube.image);
    if (gpu_brdf_lut.view.is_valid())   device.destroy_texture_view(gpu_brdf_lut.view);
    if (gpu_brdf_lut.image.is_valid())  device.destroy_texture(gpu_brdf_lut.image);
    // R2 textures.
    if (normal_tex.view.is_valid())  device.destroy_texture_view(normal_tex.view);
    if (normal_tex.image.is_valid()) device.destroy_texture(normal_tex.image);
    if (mr_tex.view.is_valid())  device.destroy_texture_view(mr_tex.view);
    if (mr_tex.image.is_valid()) device.destroy_texture(mr_tex.image);
    // Faz 1.7 RT resources — wait_idle so any in-flight cmd buffers
    // that referenced these structures are guaranteed done, then
    // tear down the TLAS queue + every BLAS.
    device.wait_idle();
    if (current_tlas.is_valid()) device.destroy_acceleration_structure(current_tlas);
    while (!tlas_destroy_queue.empty()) {
        device.destroy_acceleration_structure(tlas_destroy_queue.front().h);
        tlas_destroy_queue.pop_front();
    }
    for (auto h : { blas_cube, blas_sphere, blas_cone, blas_cyl, blas_torus, blas_floor, blas_gltf })
        if (h.is_valid()) device.destroy_acceleration_structure(h);
    if (gltf_mesh.vb.is_valid()) destroy_mesh(device, gltf_mesh);
    std::printf("hello_engine: clean exit (%u frames).\n", frame_idx);
    return 0;
}
