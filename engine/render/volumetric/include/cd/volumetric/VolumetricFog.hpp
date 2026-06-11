// =============================================================================
// CHROMODYNAMIC — cd/volumetric/VolumetricFog.hpp
// Phase 469 — Froxel-based volumetric fog (Wronski 2014).
//
// Three-pass froxel pipeline:
//   1) Inject   — per-froxel scattering + extinction contribution from
//                 every analytical light (sun + punctual + area lights).
//   2) Integrate — front-to-back marching across the depth slices of the
//                 3D froxel grid, producing an integrated 3D LUT whose
//                 RGB channels hold in-scattered radiance and whose A
//                 channel holds transmittance to the slice.
//   3) Composite — sampled in the screen-space full-screen pass. The
//                 froxel LUT is read with linear filtering at the
//                 scene-depth-derived slice, giving fog under aliasing-
//                 free volume bounds without per-pixel marching.
//
// Coordinate conventions:
//   * Froxel grid is 160 x 90 x 64 by default (Wronski 2014).
//   * X/Y tile the screen 1:1 (so XY maps to NDC then to camera-ray).
//   * Z is logarithmically distributed via a quadratic warp:
//        z_view(slice) = near + (far - near) * slice^2
//     placing more samples close to the camera where the eye is
//     sensitive to banding.
//
// All math header-only and free of GPU dependencies; the GPU side is
// fed the same constants via push-constants / UBO so CPU "truth" and
// GPU output stay byte-equivalent up to fp16 quantisation.
//
// References:
//   * Wronski, B. 2014 — "Volumetric Fog: Unified Compute Shader-Based
//     Solution to Atmospheric Scattering" (SIGGRAPH 2014 / Frostbite).
//   * Hillaire, S. 2015 — "Towards Unified and Physically-Based
//     Volumetric Lighting in Frostbite" (HPG 2015).
//   * Henyey, L. & Greenstein, J. 1941 — "Diffuse Radiation in the
//     Galaxy" (original phase function).
// =============================================================================
#pragma once

#include <cd/math/Vector.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <string_view>
#include <vector>

namespace cd::volumetric
{

inline constexpr float kVolFogPi { std::numbers::pi_v<float> };

// ---- Settings ---------------------------------------------------------------

/// Author-facing fog tweakables. All scalar coefficients are in 1/m
/// (extinction-domain units); colour is linear RGB pre-multiplied with
/// the user's authoring scale (e.g. exposure-normalised).
struct VolumetricFogSettings
{
    /// Base medium density in 1/m. Used as the linear scale for
    /// scattering and extinction unless `absorption` is non-zero.
    float density { 0.05F };

    /// Henyey-Greenstein asymmetry g in (-1, +1). g > 0 ⇒ forward
    /// scatter (water droplets / mist), g = 0 ⇒ isotropic.
    float anisotropy_g { 0.4F };

    /// Absorption coefficient in 1/m. Extinction σ_t = density +
    /// absorption; scattering σ_s = density. (For energy conservation
    /// σ_s ≤ σ_t — both `density` and `absorption` must be ≥ 0.)
    float absorption { 0.0F };

    /// In-scatter tint (linear RGB). Multiplies per-light radiance
    /// before the HG phase scale.
    cd::math::Vec3f albedo { 0.95F, 0.95F, 1.0F };

    /// Optional ambient ground-bounce in-scatter (rgb) added uniformly
    /// to every froxel. Lets cheap volumetric AO surrogate ride along
    /// without a second pass.
    cd::math::Vec3f ambient { 0.0F, 0.0F, 0.0F };
};

[[nodiscard]] inline float extinction_of(const VolumetricFogSettings& s) noexcept
{
    return std::max(0.0F, s.density + s.absorption);
}

[[nodiscard]] inline float scattering_of(const VolumetricFogSettings& s) noexcept
{
    return std::max(0.0F, s.density);
}

// ---- Grid layout ------------------------------------------------------------

/// Froxel grid description. Tile counts and view-Z range.
struct FroxelGridDesc
{
    std::uint32_t width  { 160 };   ///< Wronski 2014: 160 tiles @ 1080p.
    std::uint32_t height {  90 };
    std::uint32_t depth  {  64 };   ///< Slices along view-Z (logarithmic).
    float near_z { 0.1F };
    float far_z  { 64.0F };
};

[[nodiscard]] inline std::size_t
cell_count(const FroxelGridDesc& g) noexcept
{
    return static_cast<std::size_t>(g.width) *
           static_cast<std::size_t>(g.height) *
           static_cast<std::size_t>(g.depth);
}

// ---- Coordinate conversion --------------------------------------------------
//
// Slice <-> view-Z uses the Wronski quadratic warp. Froxel <-> world
// is a two-stage transform: (i) tile XY centre -> NDC -> camera-space
// ray direction; (ii) ray * view_z(slice) = view-space sample; the
// caller multiplies the view->world matrix in for world space. The
// helpers in this header stop at view-space so they can stay matrix-
// free; tests can chain in any user-supplied basis.

/// Wronski quadratic slice -> view-Z. `slice` in [0, 1].
[[nodiscard]] inline float
slice_to_view_z(float slice, const FroxelGridDesc& g) noexcept
{
    const float t = slice * slice;
    return g.near_z + (g.far_z - g.near_z) * t;
}

/// Inverse view-Z -> slice in [0, 1].
[[nodiscard]] inline float
view_z_to_slice(float view_z, const FroxelGridDesc& g) noexcept
{
    const float denom = std::max(g.far_z - g.near_z, 1.0e-6F);
    const float t = std::clamp((view_z - g.near_z) / denom, 0.0F, 1.0F);
    return std::sqrt(t);
}

/// Integer froxel index (centre) -> normalised UVW in [0, 1]^3.
[[nodiscard]] inline cd::math::Vec3f
froxel_to_uvw(std::uint32_t x, std::uint32_t y, std::uint32_t z,
              const FroxelGridDesc& g) noexcept
{
    return { (static_cast<float>(x) + 0.5F) / static_cast<float>(g.width),
             (static_cast<float>(y) + 0.5F) / static_cast<float>(g.height),
             (static_cast<float>(z) + 0.5F) / static_cast<float>(g.depth) };
}

/// Froxel centre -> view-space sample point. Assumes a symmetrical
/// perspective projection with horizontal half-FoV `tan_half_fov_x`
/// and aspect = width / height; output is camera-relative (camera at
/// the origin looking down -Z).
[[nodiscard]] inline cd::math::Vec3f
froxel_to_view(std::uint32_t x, std::uint32_t y, std::uint32_t z,
               const FroxelGridDesc& g,
               float tan_half_fov_x,
               float aspect) noexcept
{
    const auto uvw = froxel_to_uvw(x, y, z, g);
    const float ndc_x = uvw.x * 2.0F - 1.0F;       // [-1, +1]
    const float ndc_y = 1.0F - uvw.y * 2.0F;       // flip Y for screen
    const float view_z = slice_to_view_z(uvw.z, g);
    const float aspect_safe = std::max(aspect, 1.0e-4F);
    return { ndc_x * tan_half_fov_x * view_z,
             ndc_y * (tan_half_fov_x / aspect_safe) * view_z,
             -view_z };
}

/// View-space sample point -> floating-point froxel index. Returns
/// negative / out-of-range values for points outside the view frustum
/// — caller decides clamping behaviour (typical use clamps and skips
/// the cell).
[[nodiscard]] inline cd::math::Vec3f
view_to_froxel(const cd::math::Vec3f& view,
               const FroxelGridDesc& g,
               float tan_half_fov_x,
               float aspect) noexcept
{
    const float view_z = -view.z;
    if (view_z <= 0.0F)
        return { -1.0F, -1.0F, -1.0F };
    const float aspect_safe = std::max(aspect, 1.0e-4F);
    const float ndc_x = view.x / (tan_half_fov_x * view_z);
    const float ndc_y = view.y / ((tan_half_fov_x / aspect_safe) * view_z);
    const float u = (ndc_x + 1.0F) * 0.5F;
    const float v = (1.0F - ndc_y) * 0.5F;
    const float w = view_z_to_slice(view_z, g);
    return { u * static_cast<float>(g.width)  - 0.5F,
             v * static_cast<float>(g.height) - 0.5F,
             w * static_cast<float>(g.depth)  - 0.5F };
}

// ---- Phase + transmittance ---------------------------------------------------

/// Henyey-Greenstein phase function. `cos_theta` = dot(view, light)
/// where both vectors are unit and point AWAY from the surface (i.e.
/// camera ray and light ray). g ∈ (-1, +1).
[[nodiscard]] inline float
henyey_greenstein(float cos_theta, float g) noexcept
{
    const float g2 = g * g;
    const float denom = 1.0F + g2 - 2.0F * g * cos_theta;
    // Guard against the singularity at g→1 ∧ cos_theta→1.
    const float safe = std::max(denom, 1.0e-6F);
    return (1.0F - g2) / (4.0F * kVolFogPi * std::pow(safe, 1.5F));
}

/// Beer-Lambert transmittance e^(-σ_t · d).
[[nodiscard]] inline float
beer_lambert(float sigma_t, float distance) noexcept
{
    return std::exp(-std::max(0.0F, sigma_t * distance));
}

// ---- CPU froxel grid + front-to-back integration ----------------------------

/// CPU-side accumulation buffer matching the GPU rgba16f image3D.
/// Channel layout: RGB = in-scattered radiance (pre-multiplied with
/// the slice thickness), A = extinction σ_t at the cell.
struct FroxelGrid
{
    FroxelGridDesc desc {};
    std::vector<cd::math::Vec4f> cells;

    void resize()
    {
        cells.assign(cell_count(desc), { 0.0F, 0.0F, 0.0F, 0.0F });
    }

    [[nodiscard]] std::size_t
    index(std::uint32_t x, std::uint32_t y, std::uint32_t z) const noexcept
    {
        return (static_cast<std::size_t>(z) * desc.height + y) * desc.width + x;
    }

    [[nodiscard]] cd::math::Vec4f&
    at(std::uint32_t x, std::uint32_t y, std::uint32_t z) noexcept
    {
        return cells[index(x, y, z)];
    }

    [[nodiscard]] const cd::math::Vec4f&
    at(std::uint32_t x, std::uint32_t y, std::uint32_t z) const noexcept
    {
        return cells[index(x, y, z)];
    }
};

/// Slice thickness (view-space) for slice `z`.
[[nodiscard]] inline float
slice_thickness(std::uint32_t z, const FroxelGridDesc& g) noexcept
{
    const float inv = 1.0F / static_cast<float>(g.depth);
    const float s0 = static_cast<float>(z)     * inv;
    const float s1 = static_cast<float>(z + 1) * inv;
    return slice_to_view_z(s1, g) - slice_to_view_z(s0, g);
}

/// Front-to-back integrate a single XY tile of the froxel grid. The
/// destination span is written one Vec4 per slice — RGB = accumulated
/// in-scattering, A = transmittance from camera to that slice.
///
/// IMPORTANT: the inject step is expected to have pre-multiplied each
/// cell's RGB by the slice thickness (matches Wronski's GPU pipeline
/// and the inject GLSL in this header). The integrator therefore does
/// NOT multiply by `dt` again — only the extinction in channel A is
/// converted to per-slice transmittance via Beer-Lambert.
inline void
integrate_view_ray(const FroxelGrid& src,
                   std::uint32_t x,
                   std::uint32_t y,
                   std::vector<cd::math::Vec4f>& dst)
{
    dst.assign(src.desc.depth, { 0.0F, 0.0F, 0.0F, 1.0F });
    cd::math::Vec4f accum { 0.0F, 0.0F, 0.0F, 1.0F };
    for (std::uint32_t z = 0; z < src.desc.depth; ++z)
    {
        const auto& cell = src.at(x, y, z);
        const float dt = slice_thickness(z, src.desc);
        const float trans = beer_lambert(cell.w, dt);
        accum.x += cell.x * accum.w;   // RGB already premultiplied by dt at inject.
        accum.y += cell.y * accum.w;
        accum.z += cell.z * accum.w;
        accum.w *= trans;
        dst[z] = accum;
    }
}

/// CPU equivalent of the inject pass for a single froxel cell. Writes
/// `inscatter * dt` to RGB and σ_t to A — same packing the GPU shader
/// uses, so unit tests can drive the integrator with a realistic input.
inline cd::math::Vec4f
inject_cell(const VolumetricFogSettings& s,
            float sun_intensity,
            const cd::math::Vec3f& sun_color,
            const cd::math::Vec3f& view_dir,
            const cd::math::Vec3f& sun_dir,
            float dt) noexcept
{
    const float sigma_t = extinction_of(s);
    const float sigma_s = scattering_of(s);
    const float cos_theta = cd::math::dot(view_dir, sun_dir);
    const float phase = henyey_greenstein(cos_theta, s.anisotropy_g);
    cd::math::Vec3f rgb {
        (s.albedo.x * sun_color.x * sun_intensity * phase + s.ambient.x) * sigma_s,
        (s.albedo.y * sun_color.y * sun_intensity * phase + s.ambient.y) * sigma_s,
        (s.albedo.z * sun_color.z * sun_intensity * phase + s.ambient.z) * sigma_s,
    };
    return { rgb.x * dt, rgb.y * dt, rgb.z * dt, sigma_t };
}

// ---- GLSL compute kernels ---------------------------------------------------
//
// Three shaders fed via the same push-constant layout. Bindings:
//   set=0, b=0 (image3D rgba16f)   -- inject target / integrated source
//   set=0, b=1 (sampler3D)         -- inject source for integrate pass
//   set=0, b=2 (image3D rgba16f)   -- integrate target
//   set=0, b=3 (sampler2D depth)   -- composite source
//   set=0, b=4 (sampler3D)         -- composite LUT
//   set=0, b=5 (image2D rgba16f)   -- composite target

constexpr std::string_view kVolFogInjectCS = R"glsl(
#version 460
layout(local_size_x = 8, local_size_y = 8, local_size_z = 4) in;

layout(set = 0, binding = 0, rgba16f) uniform writeonly image3D uFroxel;

layout(push_constant) uniform PC {
    uvec4 size;          // x, y, z, _
    vec4  near_far;      // near_z, far_z, density, absorption
    vec4  sun_dir;       // xyz=dir-to-sun, w=intensity
    vec4  sun_color;     // rgb=color, a=anisotropy_g
    vec4  albedo;        // rgb=albedo, a=_unused
    vec4  ambient;       // rgb=ambient inscatter, a=_unused
    vec4  fov_aspect;    // tanHalfFovX, aspect, _, _
} pc;

float slice_to_z(float s) {
    return pc.near_far.x + (pc.near_far.y - pc.near_far.x) * s * s;
}

vec3 froxel_to_view(uvec3 cell) {
    vec3 uvw = (vec3(cell) + 0.5) / vec3(pc.size.xyz);
    float ndcX = uvw.x * 2.0 - 1.0;
    float ndcY = 1.0 - uvw.y * 2.0;
    float vz   = slice_to_z(uvw.z);
    float tanX = pc.fov_aspect.x;
    float tanY = pc.fov_aspect.x / max(pc.fov_aspect.y, 1e-4);
    return vec3(ndcX * tanX * vz, ndcY * tanY * vz, -vz);
}

float hg_phase(float cosTheta, float g) {
    float g2 = g * g;
    float d  = 1.0 + g2 - 2.0 * g * cosTheta;
    return (1.0 - g2) / (4.0 * 3.14159265 * pow(max(d, 1e-6), 1.5));
}

void main() {
    uvec3 cell = gl_GlobalInvocationID.xyz;
    if (any(greaterThanEqual(cell, pc.size.xyz))) return;

    vec3 view_pos = froxel_to_view(cell);
    vec3 view_dir = normalize(view_pos);

    float density    = pc.near_far.z;
    float absorption = pc.near_far.w;
    float sigma_t    = max(density + absorption, 0.0);
    float sigma_s    = max(density, 0.0);

    // HG phase along sun direction (camera-space, caller transforms
    // light dir to view space before push).
    float cosT = dot(view_dir, normalize(pc.sun_dir.xyz));
    float ph   = hg_phase(cosT, pc.sun_color.a);

    vec3 inscatter = pc.albedo.rgb * pc.sun_color.rgb * pc.sun_dir.w * ph * sigma_s;
    inscatter += pc.ambient.rgb * sigma_s;

    // Slice thickness for the RGB pre-multiply (matches CPU integrator).
    float s0 = float(cell.z)       / float(pc.size.z);
    float s1 = float(cell.z + 1u)  / float(pc.size.z);
    float dt = slice_to_z(s1) - slice_to_z(s0);

    imageStore(uFroxel, ivec3(cell), vec4(inscatter * dt, sigma_t));
}
)glsl";

constexpr std::string_view kVolFogIntegrateCS = R"glsl(
#version 460
layout(local_size_x = 8, local_size_y = 8) in;

layout(set = 0, binding = 1) uniform sampler3D uSrc;
layout(set = 0, binding = 2, rgba16f) uniform writeonly image3D uDst;

layout(push_constant) uniform PC {
    uvec4 size;
    vec4  near_far;
} pc;

void main() {
    uvec2 tile = gl_GlobalInvocationID.xy;
    if (any(greaterThanEqual(tile, pc.size.xy))) return;

    vec4 accum = vec4(0.0, 0.0, 0.0, 1.0);
    for (uint z = 0u; z < pc.size.z; ++z) {
        vec3 uvw = (vec3(tile.x, tile.y, z) + 0.5) / vec3(pc.size.xyz);
        vec4 cell = texture(uSrc, uvw);
        float s0 = float(z)       / float(pc.size.z);
        float s1 = float(z + 1u)  / float(pc.size.z);
        float dt = (pc.near_far.y - pc.near_far.x) * (s1 * s1 - s0 * s0);
        float trans = exp(-cell.a * dt);
        accum.rgb += cell.rgb * accum.a;     // RGB already premultiplied by dt
        accum.a   *= trans;
        imageStore(uDst, ivec3(tile.x, tile.y, int(z)), accum);
    }
}
)glsl";

constexpr std::string_view kVolFogCompositeCS = R"glsl(
#version 460
layout(local_size_x = 8, local_size_y = 8) in;

layout(set = 0, binding = 3) uniform sampler2D uDepth;       // linear depth (view-Z)
layout(set = 0, binding = 4) uniform sampler3D uIntegrated;  // integrated fog LUT
layout(set = 0, binding = 5, rgba16f) uniform image2D uScene;

layout(push_constant) uniform PC {
    uvec4 size;          // x, y, z, _
    vec4  near_far;      // near_z, far_z, _, _
    vec4  viewport;      // width, height, _, _
} pc;

float view_z_to_slice(float vz) {
    float t = clamp((vz - pc.near_far.x) /
                    max(pc.near_far.y - pc.near_far.x, 1e-6), 0.0, 1.0);
    return sqrt(t);
}

void main() {
    uvec2 px = gl_GlobalInvocationID.xy;
    if (px.x >= uint(pc.viewport.x) || px.y >= uint(pc.viewport.y)) return;

    vec2 uv = (vec2(px) + 0.5) / pc.viewport.xy;
    float view_z = texture(uDepth, uv).r;
    float w = view_z_to_slice(view_z);
    vec3 uvw = vec3(uv, w);
    vec4 fog = texture(uIntegrated, uvw);

    vec4 scene = imageLoad(uScene, ivec2(px));
    // Standard "scene * transmittance + in-scattered" composite.
    vec3 outRgb = scene.rgb * fog.a + fog.rgb;
    imageStore(uScene, ivec2(px), vec4(outRgb, scene.a));
}
)glsl";

}  // namespace cd::volumetric
