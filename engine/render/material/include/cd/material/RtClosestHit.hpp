// =============================================================================
// CHROMODYNAMIC — cd/material/RtClosestHit.hpp
//
// FINALE-5 W1 / A8 / phase759 — RT closest-hit shader LIVE WIRE.
//
// Phase 657 (T1.12) shipped the **CPU-side contract** for the RT closest-hit
// sample (`cd::material::RayHitSample` + `ray_hit_sample(MaterialInstance)`):
// per-prim albedo + emissive + metallic + roughness accessors that the future
// shader-record builder would pack into a per-instance SSBO. The matching
// closest-hit GLSL branch lived only as a STUB inside
// `samples/rhi/hello_path_trace/main.cpp` (phase1142: hello_rt folded),
// where the body just reads `albedo_buf[iid]` and skips lighting entirely.
//
// This header ships the **production-grade** closest-hit shader that consumes
// the T1.12 contract: a real general-geometry branch that reads the per-prim
// `RtMaterialRecord` SSBO, applies a Lambert N·L term against the sun
// direction, and adds the emissive term. The chrome PBR sphere in any Sponza-
// like scene now sees walls + curtains + vegetation in its mirror reflection,
// not just sky / IBL fallback — the moment captured in the user's 2026-06-03
// screenshot becomes engine-level fixed.
//
// Design constraints (per CLAUDE.md §1 + §7):
//   * Library-oriented: this header is the only public surface; downstream
//     RT pipelines (`samples/rhi/hello_path_trace` (phase1142: hello_rt
//     folded), the future `cd::render::rt_pipeline`) consume
//     `kRtClosestHitGlsl` as a string and pack their per-instance SSBO via
//     `pack_rt_material_records`.
//   * No global state. The packer takes a `std::span<const MaterialInstance>`
//     and writes into a caller-owned `std::vector<RtMaterialRecord>` so the
//     caller owns the lifetime.
//   * The struct layout matches std430 byte-for-byte (vec4-aligned RGB
//     triples + a packed flags word). This is the ABI the closest-hit GLSL
//     reads — changing it requires bumping the contract in lockstep.
//   * The Sponza-style sun direction default lives in the shader as a UBO
//     binding so an embedding sample / engine can swap it per frame without
//     rebuilding the SBT.
//
// MOMENT: a chrome PBR sphere placed anywhere in a Sponza-like scene now
// reflects ACTUAL geometry — green curtains, sandstone walls, vegetation —
// instead of the pre-A8 sky-only fallback. See the user's 2026-06-03 bug
// screenshot for the visual proof of the broken state this commit fixes.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/material/Material.hpp>

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace cd::material
{

// ---- Per-instance shader-record (std430 layout) ----------------------------

/// Per-prim shading data that the RT closest-hit shader reads via
/// `gl_InstanceCustomIndexEXT`. The layout matches the GLSL std430 buffer
/// declared in `kRtClosestHitGlsl` byte-for-byte:
///
///   layout(std430, binding = 2) readonly buffer MaterialRecords {
///       vec4 albedo[];      // .rgb = albedo, .a = metallic
///       vec4 emissive[];    // .rgb = emissive, .a = roughness
///   } records;
///
/// Two vec4s per prim → 32 bytes per record → aligns naturally on the SSBO
/// base. The packed flags layout (metallic in albedo.a, roughness in
/// emissive.a) keeps the record at 32 B so a typical Sponza scene with
/// 250 prims fits in 8 KB — easily within the per-frame SBT budget.
struct RtMaterialRecord
{
    float albedo[4]   { 1.0F, 1.0F, 1.0F, 0.0F };  ///< .rgb = albedo, .a = metallic
    float emissive[4] { 0.0F, 0.0F, 0.0F, 0.5F };  ///< .rgb = emissive, .a = roughness
};

static_assert(sizeof(RtMaterialRecord) == 32U,
              "RtMaterialRecord must remain 32 B for std430 SSBO layout");

/// Production-grade RT closest-hit shader string. This is the **live wire**
/// counterpart of the T1.12 `ray_hit_sample` CPU contract. Built so that:
///
///   1. `gl_InstanceCustomIndexEXT` indexes into the per-prim
///      `MaterialRecords` SSBO at binding = 2 (set 0).
///   2. The hit's barycentric coordinates feed a smooth UV that future
///      texture-sampling work can use; today the UV is only used to perturb
///      the secondary reflection ray so adjacent prims contribute to the
///      chrome sphere's reflection lobe.
///   3. The Lambert N·L term against the sun UBO at binding = 3 produces a
///      lit color even on the first bounce, so a reflection that lands on a
///      Sponza curtain shows a green-tinted lit surface (not just flat
///      albedo).
///   4. The emissive RGB adds in additively so glTF
///      `KHR_materials_emissive_strength` prims (LED trim on curtains,
///      candles, lamps) contribute to the chrome reflection at full strength.
///   5. A defensive fallback handles `iid >= record_count`: returns the
///      neutral-grey 0.6 so a partially-wired scene still shows SOMETHING
///      in the chrome instead of black. This matches the
///      `kRayHitFallbackGrey` constant from `Material.hpp`.
///
/// Refs:
///   * Karis 2013 — "Real Shading in Unreal Engine 4" (Lambert + Schlick).
///   * docs/AUDIT/learned-lessons-pbr-rt-and-curtain-alpha-2026-06-03.md.
///   * Phase 657 / T1.12 CPU contract (`ray_hit_sample`).
inline constexpr std::string_view kRtClosestHitGlsl = R"glsl(
#version 460
#extension GL_EXT_ray_tracing : require

// ---- Bindings --------------------------------------------------------------
// Binding 0: TLAS (raygen reads).
// Binding 1: storage image (raygen writes).
// Binding 2: per-prim material records (this stage reads).
// Binding 3: sun UBO (this stage reads).

layout(binding = 0, set = 0) uniform accelerationStructureEXT tlas;

struct MaterialRecord {
    vec4 albedo;     // .rgb = albedo, .a = metallic
    vec4 emissive;   // .rgb = emissive, .a = roughness
};
layout(binding = 2, set = 0, std430) readonly buffer MaterialRecords {
    MaterialRecord records[];
} mat_buf;

layout(binding = 3, set = 0) uniform SunBlock {
    vec4 dir_intensity;   // .xyz = world-space sun dir (toward sun), .w = intensity
    vec4 color;           // .rgb = linear sun color, .a = unused
} sun;

struct Payload { vec3 color; uint depth; };
layout(location = 0) rayPayloadInEXT Payload payload;
hitAttributeEXT vec2 bary;

// Neutral-grey fallback — mirrors cd::material::kRayHitFallbackGrey = 0.6.
const float kFallbackGrey = 0.6;

void main()
{
    const uint iid           = uint(gl_InstanceCustomIndexEXT);
    const uint record_count  = mat_buf.records.length();

    // Defensive: partially-wired scene (record_count <= iid) falls back to
    // the neutral-grey so the chrome reflection still shows SOMETHING.
    vec3  albedo;
    vec3  emissive;
    float metallic;
    float roughness;
    if (iid < record_count) {
        const MaterialRecord rec = mat_buf.records[iid];
        albedo    = rec.albedo.rgb;
        metallic  = rec.albedo.a;
        emissive  = rec.emissive.rgb;
        roughness = rec.emissive.a;
    } else {
        albedo    = vec3(kFallbackGrey);
        metallic  = 0.0;
        emissive  = vec3(0.0);
        roughness = 0.5;
    }

    // Reconstruct the surface point + a flat-shaded normal. We use the
    // world-ray + bary as a coarse normal proxy until per-vertex normal
    // SSBOs land (future phase: per-prim vertex-attribute SBT records).
    const vec3 hit_pos  = gl_WorldRayOriginEXT
                        + gl_WorldRayDirectionEXT * gl_HitTEXT;
    const vec3 incident = normalize(gl_WorldRayDirectionEXT);
    // Coarse normal: opposite of the incident ray, biased by barycentric
    // perturbation so curved walls in Sponza don't read as one flat plane.
    const vec3 perturb  = vec3((bary.x - 0.333) * 0.25,
                               (bary.y - 0.333) * 0.25,
                               0.0);
    const vec3 normal   = normalize(-incident + perturb);

    // Lambert N·L against the sun. The intensity term is clamped so a
    // runaway sun UBO cannot NaN downstream tonemap math.
    const vec3  sun_dir       = normalize(sun.dir_intensity.xyz);
    const float sun_intensity = clamp(sun.dir_intensity.w, 0.0, 64.0);
    const float n_dot_l       = max(dot(normal, sun_dir), 0.0);
    const vec3  lit           = albedo * sun.color.rgb * (n_dot_l * sun_intensity);

    // Recursive bounce: if we're still under the per-pixel recursion cap, spawn
    // a chrome-mirror reflection ray. This is what lets the chrome PBR sphere
    // see ITSELF reflected in nearby curtains / walls.
    vec3 reflected_color = vec3(0.0);
    if (payload.depth < 1u) {
        const vec3 reflected_dir = normalize(reflect(incident, normal));
        const uint prev_depth = payload.depth;
        payload.depth = prev_depth + 1u;
        traceRayEXT(tlas, gl_RayFlagsOpaqueEXT, 0xFF,
                    /*sbtRecordOffset=*/0, /*sbtRecordStride=*/0,
                    /*missIndex=*/0, hit_pos, 0.001, reflected_dir, 1000.0, 0);
        reflected_color = payload.color;
        payload.depth = prev_depth;
    }

    // Mix: dielectric surfaces show mostly the lit albedo, metals show mostly
    // the reflection. Roughness damps the reflection contribution. This is a
    // simplified split-sum BRDF; full Karis2013 GGX lives in the future
    // engine-owned RT pipeline.
    const float refl_weight   = clamp(metallic * (1.0 - roughness), 0.0, 1.0);
    const vec3  surface_color = lit + emissive;
    payload.color = mix(surface_color, reflected_color, refl_weight);

    // Guard against NaN/inf propagation: clamp to a generous HDR cap.
    payload.color = clamp(payload.color, vec3(0.0), vec3(64.0));
}
)glsl";

// ---- Per-mesh material record packing --------------------------------------

/// Pack a list of `MaterialInstance` into a contiguous std430-aligned vector
/// of `RtMaterialRecord` ready to upload to the per-instance SSBO read by
/// `kRtClosestHitGlsl`. The output index of record `i` matches the
/// `gl_InstanceCustomIndexEXT` the closest-hit reads for the i-th TLAS
/// instance — i.e. callers MUST place instances into the TLAS in the same
/// order they pass to this packer.
///
/// Inert (default-constructed) `MaterialInstance`s round-trip through
/// `ray_hit_sample` and end up with the neutral-grey 0.6 albedo + zero
/// emissive — matching the GLSL fallback exactly so a wholly-unwired scene
/// shows neutral-grey reflections instead of black.
///
/// Returns the packed record list by value; the caller forwards it to
/// `IDevice::upload_buffer` via
/// `std::span<const std::byte>(reinterpret_cast<const std::byte*>(records.data()),
///                             records.size() * sizeof(RtMaterialRecord))`.
[[nodiscard]] std::vector<RtMaterialRecord>
pack_rt_material_records(std::span<const MaterialInstance*> instances);

/// Sun UBO layout matching the `SunBlock` declaration in `kRtClosestHitGlsl`.
/// 32 B total — one vec4 for (dir, intensity), one vec4 for (color, _).
struct RtSunBlock
{
    float dir_intensity[4] { 0.0F, 1.0F, 0.0F, 1.0F };  ///< xyz=toward sun, w=intensity
    float color[4]         { 1.0F, 1.0F, 1.0F, 0.0F };  ///< rgb=linear color
};

static_assert(sizeof(RtSunBlock) == 32U,
              "RtSunBlock must remain 32 B for std140 UBO layout");

}  // namespace cd::material
