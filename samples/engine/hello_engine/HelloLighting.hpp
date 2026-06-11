// =============================================================================
// CHROMODYNAMIC — samples/engine/hello_engine/HelloLighting.hpp
//
// Phase 290 / Marathon Run 7 sub-N1B: PrimPush + LightSlotGpu +
// LightUboGpu plain-old-data layouts and the single-slot packing helper
// for hello_engine's multi-light UBO upload.
//
// These types mirror the GLSL std140 layouts declared inside
// PrimShader_kPrimFS.inl (struct LightSlot, layout(set=0,binding=3)
// LightArray block, and the kPrimVS push_constant block). They are
// hello_engine-scoped because PrimPush carries the sample's fx_params
// debug knobs; cd::light is a backend-agnostic library and must not
// pull in those sample-specific dials.
//
// `pack_light_slot()` converts one cd::light::Light into a single
// LightSlotGpu row including the W8-Y luminous-power -> intensity
// formula. Used by main.cpp's per-frame UBO fill loop.
// =============================================================================
#pragma once

#include <cd/light/Light.hpp>
#include <cd/math/Matrix.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace cd::hello_engine
{

// ---- Push constant block for kPrimVS / kPrimFS ----------------------------
// 256 B (well under Vulkan's 128 B *minimum*, but our target HW exposes
// 256 B+ via the maxPushConstantsSize feature). Layout: 2 mat4 + 8 vec4.
// NOTE: mirrors the GPU push-constant layout; host code fully overwrites
// every field before upload — NSDMIs below are defensive zero-init only.
struct PrimPush
{
    cd::math::Mat4f mvp;
    cd::math::Mat4f model;
    float tint[4] {};
    float sun_dir[4] {};
    float sun_color[4] {};
    // FX params block 1 - x=tonemap_op (0=Nark, 1=Hill, 2=Hable, 3=AGX)
    //                     y=albedo_tex_flag (1=sample cd_albedo_tex)
    //                     z=gtao_strength (inline curvature darkening)
    //                     w=bloom_strength (post-tonemap halo boost)
    float fx_params[4] {};
    // FX params block 2 - x=smaa_strength (legacy inline FXAA blur)
    //                     y=motion_blur_amount (LIVE in composite - phase 215)
    //                     z=taa_amount (LIVE in composite - phase 216-217)
    //                     w=dof_strength (LIVE in composite - phase 207)
    float fx_params2[4] {};
    // FX params block 3 - atmospherics (LIVE in composite - phase 209)
    //                     x=fog_density (legacy inline; composite owns now)
    //                     y=atmosphere_strength (legacy inline; composite owns)
    //                     z=clouds_coverage (queued - needs 3D noise sampler)
    //                     w=light_shafts_strength (LIVE in composite - phase 208)
    float fx_params3[4] {};
    // Camera origin (needed for distance fog without breaking the model
    // matrix invariant). xyz=world camera, w=unused.
    float camera_pos[4] {};
    // R6 advanced BRDF strengths:
    //   x=clearcoat (Filament second Schlick lobe on top of base spec)
    //   y=sheen (Charlie velvet rim term)
    //   z=sss (Burley wrap-diffusion approximation)
    //   w=reserved
    float fx_params4[4] {};
};

static_assert(sizeof(PrimPush) == 256, "PrimPush layout drift");

// ---- Multi-light UBO -------------------------------------------------------
// Matches the std140 layout inside PrimShader_kPrimFS.inl. The shader-side
// `LightArray` block uses 3 separate scalar uints for the header padding
// (NOT `uint pad[3]` — see kPrimFS comment) to match this packed C++
// layout where pad is 12 contiguous bytes.

constexpr std::uint32_t kMaxLights = 8;

struct LightSlotGpu
{
    float pos_range[4];  // xyz=world position, w=range
    float dir_type[4];   // xyz=direction or right-basis, w=type as float
    float color_int[4];  // xyz=colour, w=intensity (scaled, ready for FS)
    float extras[4];     // x=cos_outer, y=area_w, z=area_h, w=cos_inner
    // W8-N: explicit tangent vec for area lights so the rect's local
    // X axis is not derived in the shader (Frisvad produced a smooth
    // basis but the gizmo's per-axis rotation couldn't independently
    // control the rect's twist around its normal). Layout: xyz=unit
    // tangent direction (world space), w=reserved.
    float tangent[4];
};

static_assert(sizeof(LightSlotGpu) == 80, "LightSlotGpu must be 80 B");

struct LightUboGpu
{
    std::uint32_t count;
    std::uint32_t pad[3];
    LightSlotGpu slots[kMaxLights];
};

static_assert(sizeof(LightUboGpu) == 16 + kMaxLights * 80, "LightUboGpu must be 656 B");

// ---- pack_light_slot -------------------------------------------------------
// Fill one LightSlotGpu slot from a cd::light::Light, applying the W8-Y
// luminous-power-to-radiance conversion and W8-N tangent bookkeeping.
// Returns true on a "kept" slot, false when the light should be skipped
// (directional lights drive the sun push directly; multi-light UBO is
// non-sun lights only). Callers are responsible for the enabled-flag
// gate; this helper only handles the type-driven payload.
[[nodiscard]] inline bool pack_light_slot(LightSlotGpu& s, const cd::light::Light& L) noexcept
{
    using LT = cd::light::LightType;
    const auto k = L.type;
    if (k == LT::kDirectional)
        return false;

    s.pos_range[0] = L.position.x;
    s.pos_range[1] = L.position.y;
    s.pos_range[2] = L.position.z;
    // Range: point/spot already have it; area lights derive a sensible
    // falloff from area extents.
    s.pos_range[3] = (k == LT::kPoint || k == LT::kSpot)
                         ? L.range
                         : (L.area_width + L.area_height) * 4.0F;

    s.dir_type[0] = L.direction.x;
    s.dir_type[1] = L.direction.y;
    s.dir_type[2] = L.direction.z;
    s.dir_type[3] = static_cast<float>(static_cast<int>(k));

    s.color_int[0] = L.color.x;
    s.color_int[1] = L.color.y;
    s.color_int[2] = L.color.z;

    // W8-Y luminous-power -> radiance conversion. See main.cpp's prior
    // comment block (phase 287 / W8-Y / W8-AG) for the derivation:
    //   Area:    ki = phi / (8*pi) * 0.20   (Lambertian rect emitter)
    //   Punctual: ki = phi / (8*pi)         (point baseline)
    //   Spot:    same * 2.5                 (cone inflation)
    constexpr float kInvPi = std::numbers::inv_pi_v<float>;
    float ki = 0.0F;
    if (k == LT::kRectArea || k == LT::kDiskArea)
    {
        // W8-AG: 0.20 area multiplier (W8-T baseline, reverted from W8-AB 1.5x).
        ki = L.intensity / (4.0F * std::numbers::pi_v<float>) / 2.0F * 0.20F;
    }
    else
    {
        // Punctual (point/spot). W8-B baseline: phi/(4 pi)/2 = phi * (1/(8 pi)).
        ki = L.intensity * kInvPi * 0.125F;
        if (k == LT::kSpot)
            ki *= 2.5F;
    }
    s.color_int[3] = ki;

    s.extras[0] = L.cos_outer_cone;
    s.extras[1] = L.area_width;
    s.extras[2] = L.area_height;
    s.extras[3] = L.cos_inner_cone;

    // W8-N: explicit area tangent (cd::light::rect_area normalises it
    // already; non-area lights write the bookkeeping +X vector).
    if (k == LT::kRectArea || k == LT::kDiskArea)
    {
        s.tangent[0] = L.area_tangent.x;
        s.tangent[1] = L.area_tangent.y;
        s.tangent[2] = L.area_tangent.z;
    }
    else
    {
        s.tangent[0] = 1.0F;
        s.tangent[1] = 0.0F;
        s.tangent[2] = 0.0F;
    }
    s.tangent[3] = 0.0F;
    return true;
}

}  // namespace cd::hello_engine
