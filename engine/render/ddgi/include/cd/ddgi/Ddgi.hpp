// =============================================================================
// CHROMODYNAMIC — cd/ddgi/Ddgi.hpp
// phase526 — Dynamic Diffuse Global Illumination (Majercik et al. 2019).
//
// 3-D grid of irradiance probes; each probe stores irradiance + depth in
// octahedral-encoded atlases (Cigolle et al. 2014). Per frame the engine
// traces N rays per probe via cd_tlas (RT) to update the atlases with a
// low-discrepancy temporal blend; per shading point the FS samples the 8
// nearest probes with Chebyshev-gated trilinear interpolation.
//
// CPU-only math lives here so every system (ECS, editor, tests) can query
// probe positions and octahedral mappings without a GPU device. GPU dispatch
// (trace + blend) is wired in phase527+ via cd::rhi.
//
// References:
//   Majercik, Marrs, Spjut, McGuire — "Dynamic Diffuse Global Illumination
//   with Ray-Traced Irradiance Fields", JCGT 8:2, 2019.
//   Cigolle et al. — "A Survey of Efficient Representations for Independent
//   Unit Vectors", JCGT 3:2, 2014.
// =============================================================================
#pragma once

#include <cd/math/Vector.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace cd::ddgi
{

// ---------------------------------------------------------------------------
// ProbeGrid — spatial layout of the probe lattice
// ---------------------------------------------------------------------------

/// Axis-aligned 3-D grid of irradiance probes.
/// Default 8×4×8 = 256 probes (Majercik 2019 §4 recommended starter config).
struct ProbeGrid
{
    cd::math::Vec3f  origin  { 0.0F, 0.0F, 0.0F };  ///< World-space origin of probe [0,0,0].
    cd::math::Vec3f  spacing { 1.0F, 1.0F, 1.0F };  ///< Distance between adjacent probes (metres).
    std::uint32_t    probes_x { 8 };
    std::uint32_t    probes_y { 4 };
    std::uint32_t    probes_z { 8 };

    /// Total number of probes.
    [[nodiscard]] constexpr std::uint32_t probe_count() const noexcept
    {
        return probes_x * probes_y * probes_z;
    }

    /// Flat index for grid coordinate (px, py, pz).
    /// Order: x-major (px + probes_x * (py + probes_y * pz)).
    [[nodiscard]] constexpr std::uint32_t
    flat_index(std::uint32_t px,
               std::uint32_t py,
               std::uint32_t pz) const noexcept
    {
        return px + probes_x * (py + probes_y * pz);
    }

    /// World-space centre of probe at integer grid coord (px, py, pz).
    [[nodiscard]] cd::math::Vec3f
    probe_world_pos(std::uint32_t px,
                    std::uint32_t py,
                    std::uint32_t pz) const noexcept
    {
        return {
            origin.x + static_cast<float>(px) * spacing.x,
            origin.y + static_cast<float>(py) * spacing.y,
            origin.z + static_cast<float>(pz) * spacing.z };
    }

    /// Returns the flat indices and trilinear weights of the 8 probes
    /// surrounding world-space point `p`.
    ///
    /// Corner order (xyz lo/hi loops: dz outer, dy, dx inner):
    ///   idx 0 = (x0,y0,z0), 1 = (x1,y0,z0), 2 = (x0,y1,z0), 3 = (x1,y1,z0),
    ///   idx 4 = (x0,y0,z1), 5 = (x1,y0,z1), 6 = (x0,y1,z1), 7 = (x1,y1,z1).
    ///
    /// Probes that fall outside the grid bounds receive weight 0 and index 0.
    [[nodiscard]] std::array<std::uint32_t, 8>
    probe_index_from_world(cd::math::Vec3f p,
                           std::array<float, 8>& weights_out) const noexcept
    {
        const float gx = (p.x - origin.x) / spacing.x;
        const float gy = (p.y - origin.y) / spacing.y;
        const float gz = (p.z - origin.z) / spacing.z;

        const auto x0 = static_cast<std::int32_t>(std::floor(gx));
        const auto y0 = static_cast<std::int32_t>(std::floor(gy));
        const auto z0 = static_cast<std::int32_t>(std::floor(gz));

        const float fx = gx - static_cast<float>(x0);
        const float fy = gy - static_cast<float>(y0);
        const float fz = gz - static_cast<float>(z0);

        std::array<std::uint32_t, 8> indices {};
        int idx = 0;
        for (int dz = 0; dz <= 1; ++dz)
        for (int dy = 0; dy <= 1; ++dy)
        for (int dx = 0; dx <= 1; ++dx)
        {
            const float wx = (dx == 0) ? (1.0F - fx) : fx;
            const float wy = (dy == 0) ? (1.0F - fy) : fy;
            const float wz = (dz == 0) ? (1.0F - fz) : fz;

            const std::int32_t cx = x0 + dx;
            const std::int32_t cy = y0 + dy;
            const std::int32_t cz = z0 + dz;

            const bool in_bounds =
                cx >= 0 && cy >= 0 && cz >= 0 &&
                static_cast<std::uint32_t>(cx) < probes_x &&
                static_cast<std::uint32_t>(cy) < probes_y &&
                static_cast<std::uint32_t>(cz) < probes_z;

            const std::size_t i = static_cast<std::size_t>(idx);
            weights_out[i] = in_bounds ? (wx * wy * wz) : 0.0F;
            indices[i]     = in_bounds
                ? flat_index(static_cast<std::uint32_t>(cx),
                             static_cast<std::uint32_t>(cy),
                             static_cast<std::uint32_t>(cz))
                : 0U;
            ++idx;
        }
        return indices;
    }
};

// ---------------------------------------------------------------------------
// Backwards-compatible free function (keeps existing callsites green)
// ---------------------------------------------------------------------------

/// World position of probe at integer grid coordinate (px, py, pz).
[[nodiscard]] inline cd::math::Vec3f
probe_world_pos(const ProbeGrid& g,
                std::uint32_t px,
                std::uint32_t py,
                std::uint32_t pz) noexcept
{
    return g.probe_world_pos(px, py, pz);
}

/// Alias kept for callers using the old GridConfig-named overload.
using GridConfig = ProbeGrid;

// ---------------------------------------------------------------------------
// ProbeAtlas — descriptor of the GPU atlas textures
// ---------------------------------------------------------------------------

/// Descriptor of the two octahedral-encoded GPU atlas textures.
/// The irradiance atlas stores rgba16f; the visibility atlas stores rg16f
/// (mean depth, mean depth²) for Chebyshev visibility gating.
///
/// Layout (Majercik 2019 §4):
///   width  = probes_x * probes_z * probe_face_size
///   height = probes_y * probe_face_size
struct ProbeAtlas
{
    std::uint32_t probe_face_size     { 8 };   ///< Texels per face (power-of-two; 8 or 16).
    std::uint32_t irradiance_width    { 0 };   ///< Computed from grid on init.
    std::uint32_t irradiance_height   { 0 };
    std::uint32_t visibility_width    { 0 };   ///< Visibility atlas dims (same layout).
    std::uint32_t visibility_height   { 0 };

    /// Populate atlas dimensions from a ProbeGrid.
    void init_from_grid(const ProbeGrid& g) noexcept
    {
        irradiance_width   = g.probes_x * g.probes_z * probe_face_size;
        irradiance_height  = g.probes_y * probe_face_size;
        visibility_width   = irradiance_width;
        visibility_height  = irradiance_height;
    }

    /// UV of the texel-centre for direction `d` inside probe `flat_index`.
    /// `d` must be normalised.  Returns UV in [0,1]² in the full atlas.
    [[nodiscard]] cd::math::Vec2f
    probe_uv(const ProbeGrid& g, std::uint32_t flat_idx, cd::math::Vec3f d) const noexcept
    {
        // --- octahedral encode ---
        const float l1 = std::abs(d.x) + std::abs(d.y) + std::abs(d.z);
        float ox = d.x / l1;
        float oy = d.y / l1;
        if (d.z < 0.0F)
        {
            const float tx = ox;
            const float ty = oy;
            ox = (1.0F - std::abs(ty)) * (tx >= 0.0F ? 1.0F : -1.0F);
            oy = (1.0F - std::abs(tx)) * (ty >= 0.0F ? 1.0F : -1.0F);
        }
        // local face UV [0,1]
        const float fu = ox * 0.5F + 0.5F;
        const float fv = oy * 0.5F + 0.5F;

        // probe grid coord from flat index
        const std::uint32_t pz  = flat_idx / (g.probes_x * g.probes_y);
        const std::uint32_t rem = flat_idx % (g.probes_x * g.probes_y);
        const std::uint32_t py  = rem / g.probes_x;
        const std::uint32_t px  = rem % g.probes_x;

        const float tile_x = static_cast<float>(px + pz * g.probes_x) + fu;
        const float tile_y = static_cast<float>(py) + fv;

        const float atlas_w = static_cast<float>(irradiance_width);
        const float atlas_h = static_cast<float>(irradiance_height);
        // Guard against zero-size atlas (uninitialised).
        if (atlas_w < 1.0F || atlas_h < 1.0F)
            return { fu, fv };

        return {
            (tile_x * static_cast<float>(probe_face_size)) / atlas_w,
            (tile_y * static_cast<float>(probe_face_size)) / atlas_h };
    }
};

// ---------------------------------------------------------------------------
// TraceSettings — per-frame ray trace parameters
// ---------------------------------------------------------------------------

struct TraceSettings
{
    /// Rays per probe per frame. Majercik 2019 recommends 64–256.
    std::uint32_t rays_per_probe { 64 };
    /// Temporal hysteresis (EMA alpha): 0 = instant, 1 = never update.
    /// 0.97 ≈ convergence in ~33 frames at 60 fps.
    float         hysteresis     { 0.97F };
    /// Maximum RT trace distance (metres).
    float         max_distance   { 20.0F };
    /// Backface-hit fraction threshold for probe relocation (phase529+).
    float         backface_threshold { 0.25F };
};

// ---------------------------------------------------------------------------
// IrradianceField — aggregate (grid + atlas + settings + fallback)
// ---------------------------------------------------------------------------

/// Top-level descriptor of one DDGI irradiance field.
/// Owns the spatial grid config, atlas layout, trace settings, and the
/// sky-colour fallback returned when a probe accumulates zero RT hits.
struct IrradianceField
{
    ProbeGrid    grid     {};
    ProbeAtlas   atlas    {};
    TraceSettings settings {};
    /// Fallback irradiance for probes that see only sky (zero-hit frame).
    cd::math::Vec3f sky_color { 0.2F, 0.25F, 0.4F };
    /// Constant ambient fallback for an empty/uninitialised field.
    cd::math::Vec3f default_ambient { 0.05F, 0.05F, 0.05F };

    /// Initialise the atlas dimensions to match `grid`.
    void init() noexcept { atlas.init_from_grid(grid); }

    /// Returns sky_color when the grid has valid probes, default_ambient when empty.
    [[nodiscard]] cd::math::Vec3f
    ambient_fallback(bool any_probes_hit) const noexcept
    {
        if (grid.probe_count() == 0)
            return default_ambient;
        return any_probes_hit ? cd::math::Vec3f{0.0F,0.0F,0.0F} : sky_color;
    }
};

// ---------------------------------------------------------------------------
// Octahedral encode / decode (Cigolle et al. 2014)
// ---------------------------------------------------------------------------

/// Encode unit-sphere direction `n` (normalised) to UV in [0,1]².
[[nodiscard]] inline cd::math::Vec2f
octahedral_encode(cd::math::Vec3f n) noexcept
{
    const float l1 = std::abs(n.x) + std::abs(n.y) + std::abs(n.z);
    float ox = n.x / l1;
    float oy = n.y / l1;
    if (n.z < 0.0F)
    {
        const float tx = ox;
        const float ty = oy;
        ox = (1.0F - std::abs(ty)) * (tx >= 0.0F ? 1.0F : -1.0F);
        oy = (1.0F - std::abs(tx)) * (ty >= 0.0F ? 1.0F : -1.0F);
    }
    return { ox * 0.5F + 0.5F, oy * 0.5F + 0.5F };
}

/// Decode UV in [0,1]² back to a unit-sphere direction.
[[nodiscard]] inline cd::math::Vec3f
octahedral_decode(cd::math::Vec2f uv) noexcept
{
    float ox = uv.x * 2.0F - 1.0F;
    float oy = uv.y * 2.0F - 1.0F;
    cd::math::Vec3f n { ox, oy, 1.0F - std::abs(ox) - std::abs(oy) };
    if (n.z < 0.0F)
    {
        const float tx = n.x;
        const float ty = n.y;
        n.x = (1.0F - std::abs(ty)) * (tx >= 0.0F ? 1.0F : -1.0F);
        n.y = (1.0F - std::abs(tx)) * (ty >= 0.0F ? 1.0F : -1.0F);
    }
    const float len = std::sqrt(n.x*n.x + n.y*n.y + n.z*n.z);
    return { n.x/len, n.y/len, n.z/len };
}

// ---------------------------------------------------------------------------
// Backwards-compatible free function (trilinear weights via ProbeGrid)
// ---------------------------------------------------------------------------

/// Trilinear weights + corner coordinates for the 8 nearest probes at `p`.
/// Out-of-bounds corners receive weight 0 and are clamped to grid edge.
inline void
trilinear_probe_weights(const ProbeGrid& g,
                        cd::math::Vec3f p,
                        std::array<float, 8>& weights,
                        std::array<std::array<std::uint32_t, 3>, 8>& corners) noexcept
{
    const float gx = (p.x - g.origin.x) / g.spacing.x;
    const float gy = (p.y - g.origin.y) / g.spacing.y;
    const float gz = (p.z - g.origin.z) / g.spacing.z;
    const auto x0 = static_cast<std::int32_t>(std::floor(gx));
    const auto y0 = static_cast<std::int32_t>(std::floor(gy));
    const auto z0 = static_cast<std::int32_t>(std::floor(gz));
    const float fx = gx - static_cast<float>(x0);
    const float fy = gy - static_cast<float>(y0);
    const float fz = gz - static_cast<float>(z0);
    int idx = 0;
    for (int dz = 0; dz <= 1; ++dz)
    for (int dy = 0; dy <= 1; ++dy)
    for (int dx = 0; dx <= 1; ++dx)
    {
        const float wx = (dx == 0) ? (1.0F - fx) : fx;
        const float wy = (dy == 0) ? (1.0F - fy) : fy;
        const float wz = (dz == 0) ? (1.0F - fz) : fz;
        const std::int32_t cx = x0 + dx;
        const std::int32_t cy = y0 + dy;
        const std::int32_t cz = z0 + dz;
        const bool in_bounds =
            cx >= 0 && cy >= 0 && cz >= 0 &&
            static_cast<std::uint32_t>(cx) < g.probes_x &&
            static_cast<std::uint32_t>(cy) < g.probes_y &&
            static_cast<std::uint32_t>(cz) < g.probes_z;
        const std::size_t i = static_cast<std::size_t>(idx);
        weights[i] = in_bounds ? (wx * wy * wz) : 0.0F;
        corners[i] = {
            static_cast<std::uint32_t>(std::max(0, cx)),
            static_cast<std::uint32_t>(std::max(0, cy)),
            static_cast<std::uint32_t>(std::max(0, cz)) };
        ++idx;
    }
}

// ===========================================================================
// GLSL compute / fragment shader sources
// ===========================================================================

/// Ray-trace from each probe via cd_tlas.
/// One workgroup per probe; each thread handles one ray direction (Fibonacci
/// low-discrepancy sequence, rotated by frame_index to accumulate uniformly).
constexpr std::string_view kDdgiTraceCS = R"glsl(
#version 460
#extension GL_EXT_ray_query : require

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform accelerationStructureEXT cd_tlas;
// rgba16f: per-ray hit radiance. Width = rays_per_probe, height = probe_count.
layout(set = 0, binding = 1, rgba16f) uniform image2D ray_radiance;
// rg16f:   per-ray hit direction (packed octahedral) and distance.
layout(set = 0, binding = 2, rg16f)   uniform image2D ray_dir_dist;

layout(push_constant) uniform PC {
    vec3  grid_origin;   float max_distance;
    vec3  grid_spacing;  float hysteresis;
    uvec4 probes_dim;    // xyz = count, w = rays_per_probe
    uint  probe_face_size;
    uint  frame_index;
    uint  _pad0;
    uint  _pad1;
    vec3  sky_color;     float _pad2;
} pc;

// Fibonacci hemisphere direction (low-discrepancy, Majercik §4).
vec3 fibonacci_dir(uint i, uint n, uint seed) {
    float phi  = 2.399963229728653 * float((i + seed) % n);
    float cos_t = 1.0 - 2.0 * float(i) / float(n);
    float sin_t = sqrt(max(0.0, 1.0 - cos_t*cos_t));
    return vec3(cos(phi)*sin_t, sin(phi)*sin_t, cos_t);
}

// Octahedral encode for packing hit direction into rg16f.
vec2 oct_encode(vec3 n) {
    float l = abs(n.x) + abs(n.y) + abs(n.z);
    vec2  p = n.xy / l;
    if (n.z < 0.0) p = (1.0 - abs(p.yx)) * sign(p);
    return p * 0.5 + 0.5;
}

void main() {
    uint ray_idx   = gl_GlobalInvocationID.x + gl_GlobalInvocationID.y * 8u;
    uint probe_idx = gl_GlobalInvocationID.z;
    if (ray_idx   >= pc.probes_dim.w)                             return;
    if (probe_idx >= pc.probes_dim.x * pc.probes_dim.y * pc.probes_dim.z) return;

    // Reconstruct world-space probe position from flat index.
    uint pz  = probe_idx / (pc.probes_dim.x * pc.probes_dim.y);
    uint rem = probe_idx % (pc.probes_dim.x * pc.probes_dim.y);
    uint py  = rem / pc.probes_dim.x;
    uint px  = rem % pc.probes_dim.x;
    vec3 probe_pos = pc.grid_origin
        + vec3(float(px), float(py), float(pz)) * pc.grid_spacing;

    vec3 dir = fibonacci_dir(ray_idx, pc.probes_dim.w, pc.frame_index * 1013u);

    rayQueryEXT rq;
    rayQueryInitializeEXT(rq, cd_tlas,
        gl_RayFlagsOpaqueEXT, 0xFF,
        probe_pos, 1e-3, dir, pc.max_distance);
    while (rayQueryProceedEXT(rq)) {}

    vec4 radiance = vec4(pc.sky_color, -1.0); // negative dist = miss
    float dist    = -1.0;
    if (rayQueryGetIntersectionTypeEXT(rq, true) ==
        gl_RayQueryCommittedIntersectionTriangleEXT)
    {
        dist = rayQueryGetIntersectionTEXT(rq, true);
        // Placeholder: real shading via material/light UBO goes here (phase527).
        radiance = vec4(0.1, 0.1, 0.1, dist);
    }

    ivec2 px_out = ivec2(int(ray_idx), int(probe_idx));
    imageStore(ray_radiance, px_out, radiance);
    imageStore(ray_dir_dist, px_out, vec4(oct_encode(dir), dist, 0.0));
}
)glsl";

/// Blend traced ray radiance into the irradiance octahedral atlas.
/// One thread per probe face texel; accumulates with EMA hysteresis.
constexpr std::string_view kDdgiBlendIrradianceCS = R"glsl(
#version 460

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

// rgba16f irradiance atlas.
layout(set = 0, binding = 0, rgba16f) uniform image2D irradiance_atlas;
// rgba16f per-ray hit radiance (from trace pass).
layout(set = 0, binding = 1, rgba16f) uniform image2D ray_radiance;
// rg16f packed direction + distance.
layout(set = 0, binding = 2, rg16f)   uniform image2D ray_dir_dist;

layout(push_constant) uniform PC {
    vec3  grid_origin;   float hysteresis;
    vec3  grid_spacing;  float _pad0;
    uvec4 probes_dim;
    uint  probe_face_size;
    uint  frame_index;
    uint  _pad1;
    uint  _pad2;
} pc;

// Octahedral decode.
vec3 oct_decode(vec2 e) {
    vec2  p = e * 2.0 - 1.0;
    vec3  n = vec3(p, 1.0 - abs(p.x) - abs(p.y));
    if (n.z < 0.0) n.xy = (1.0 - abs(n.yx)) * sign(n.xy);
    return normalize(n);
}

void main() {
    // Each thread maps to one atlas texel (probe_face texel in [0, face_size)).
    uint atlas_x    = gl_GlobalInvocationID.x;
    uint atlas_y    = gl_GlobalInvocationID.y;
    uint probe_idx  = gl_GlobalInvocationID.z;

    uint probes_per_row = pc.probes_dim.x * pc.probes_dim.z;
    uint total_probes   = probes_per_row * pc.probes_dim.y;
    if (probe_idx >= total_probes) return;

    // Local face texel UV -> octahedral direction.
    float fs = float(pc.probe_face_size);
    vec2  local_uv = (vec2(float(atlas_x), float(atlas_y)) + 0.5) / fs;
    vec2  oct_p    = local_uv * 2.0 - 1.0;
    vec3  face_dir = oct_decode(local_uv);

    // Accumulate weighted radiance across all rays (cosine-weighted).
    vec3  accum  = vec3(0.0);
    float weight = 0.0;
    uint  rays_n = pc.probes_dim.w;
    for (uint r = 0; r < rays_n; ++r) {
        ivec2  ray_px   = ivec2(int(r), int(probe_idx));
        vec4   rad_info = imageLoad(ray_radiance, ray_px);
        vec4   dir_info = imageLoad(ray_dir_dist, ray_px);
        vec3   ray_dir  = oct_decode(dir_info.xy);
        float  cos_w    = max(0.0, dot(face_dir, ray_dir));
        accum  += rad_info.rgb * cos_w;
        weight += cos_w;
    }
    vec3 new_val = (weight > 1e-6) ? (accum / weight) : vec3(0.0);

    // Atlas texel address.
    uint pz  = probe_idx / (pc.probes_dim.x * pc.probes_dim.y);
    uint rem = probe_idx % (pc.probes_dim.x * pc.probes_dim.y);
    uint py  = rem / pc.probes_dim.x;
    uint px  = rem % pc.probes_dim.x;
    ivec2 atlas_base = ivec2(
        int((px + pz * pc.probes_dim.x) * pc.probe_face_size),
        int(py * pc.probe_face_size));
    ivec2 atlas_coord = atlas_base + ivec2(int(atlas_x), int(atlas_y));

    // EMA blend (temporal hysteresis, Majercik §4).
    vec4 prev = (pc.frame_index > 0u)
        ? imageLoad(irradiance_atlas, atlas_coord)
        : vec4(0.0);
    vec4 blended = vec4(mix(new_val, prev.rgb, pc.hysteresis), 1.0);
    imageStore(irradiance_atlas, atlas_coord, blended);
}
)glsl";

/// Blend Chebyshev visibility (mean depth, depth²) into the visibility atlas.
/// Used by the sample FS to gate indirect irradiance and prevent light leaks.
constexpr std::string_view kDdgiBlendVisibilityCS = R"glsl(
#version 460

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

// rg16f: (mean_depth, mean_depth²) per face texel.
layout(set = 0, binding = 0, rg16f) uniform image2D visibility_atlas;
// rgba16f per-ray hit radiance (w = hit distance, negative = miss).
layout(set = 0, binding = 1, rgba16f) uniform image2D ray_radiance;
// rg16f packed direction + distance.
layout(set = 0, binding = 2, rg16f)   uniform image2D ray_dir_dist;

layout(push_constant) uniform PC {
    vec3  grid_origin;   float hysteresis;
    vec3  grid_spacing;  float max_distance;
    uvec4 probes_dim;
    uint  probe_face_size;
    uint  frame_index;
    uint  _pad0;
    uint  _pad1;
} pc;

vec3 oct_decode(vec2 e) {
    vec2 p = e * 2.0 - 1.0;
    vec3 n = vec3(p, 1.0 - abs(p.x) - abs(p.y));
    if (n.z < 0.0) n.xy = (1.0 - abs(n.yx)) * sign(n.xy);
    return normalize(n);
}

void main() {
    uint atlas_x   = gl_GlobalInvocationID.x;
    uint atlas_y   = gl_GlobalInvocationID.y;
    uint probe_idx = gl_GlobalInvocationID.z;

    uint total = pc.probes_dim.x * pc.probes_dim.y * pc.probes_dim.z;
    if (probe_idx >= total) return;

    float fs      = float(pc.probe_face_size);
    vec2  local_uv = (vec2(float(atlas_x), float(atlas_y)) + 0.5) / fs;
    vec3  face_dir = oct_decode(local_uv);

    float sum_d  = 0.0;
    float sum_d2 = 0.0;
    float weight = 0.0;
    uint  rays_n = pc.probes_dim.w;
    for (uint r = 0; r < rays_n; ++r) {
        ivec2 ray_px  = ivec2(int(r), int(probe_idx));
        vec4  dir_dd  = imageLoad(ray_dir_dist, ray_px);
        float dist    = dir_dd.z;
        if (dist < 0.0) continue;   // miss — sky
        vec3  ray_dir = oct_decode(dir_dd.xy);
        float cos_w   = max(0.0, dot(face_dir, ray_dir));
        sum_d  += dist * cos_w;
        sum_d2 += dist * dist * cos_w;
        weight += cos_w;
    }
    vec2 new_vis = (weight > 1e-6)
        ? vec2(sum_d / weight, sum_d2 / weight)
        : vec2(pc.max_distance, pc.max_distance * pc.max_distance);

    // Atlas address (same layout as irradiance atlas).
    uint pz  = probe_idx / (pc.probes_dim.x * pc.probes_dim.y);
    uint rem = probe_idx % (pc.probes_dim.x * pc.probes_dim.y);
    uint py  = rem / pc.probes_dim.x;
    uint px  = rem % pc.probes_dim.x;
    ivec2 atlas_base  = ivec2(
        int((px + pz * pc.probes_dim.x) * pc.probe_face_size),
        int(py * pc.probe_face_size));
    ivec2 atlas_coord = atlas_base + ivec2(int(atlas_x), int(atlas_y));

    vec2 prev   = (pc.frame_index > 0u)
        ? imageLoad(visibility_atlas, atlas_coord).rg
        : vec2(0.0);
    vec2 blended = mix(new_vis, prev, pc.hysteresis);
    imageStore(visibility_atlas, atlas_coord, vec4(blended, 0.0, 0.0));
}
)glsl";

/// Per-fragment indirect irradiance sample (composite / deferred lighting pass).
/// Reads from irradiance + visibility atlases; applies Chebyshev gating to
/// prevent indirect light leaking through occluders.
constexpr std::string_view kDdgiSampleFS = R"glsl(
#version 460

// Irradiance atlas (rgba16f, full atlas).
layout(set = 1, binding = 0) uniform sampler2D irradiance_atlas;
// Visibility atlas (rg16f, mean depth + depth²).
layout(set = 1, binding = 1) uniform sampler2D visibility_atlas;

layout(push_constant) uniform PC {
    vec3  grid_origin;   float _pad0;
    vec3  grid_spacing;  float _pad1;
    uvec4 probes_dim;
    uint  probe_face_size;
    uint  _pad2;
    uint  _pad3;
    uint  _pad4;
    vec3  sky_color;     float _pad5;
} pc;

// Fragment inputs (from G-Buffer / forward pass).
layout(location = 0) in  vec3 v_world_pos;
layout(location = 1) in  vec3 v_normal;

// Output: indirect irradiance contribution (premultiplied by albedo outside).
layout(location = 0) out vec4 out_indirect;

// Octahedral decode.
vec3 oct_decode(vec2 e) {
    vec2 p = e * 2.0 - 1.0;
    vec3 n = vec3(p, 1.0 - abs(p.x) - abs(p.y));
    if (n.z < 0.0) n.xy = (1.0 - abs(n.yx)) * sign(n.xy);
    return normalize(n);
}

// Octahedral encode.
vec2 oct_encode(vec3 n) {
    float l = abs(n.x) + abs(n.y) + abs(n.z);
    vec2  p = n.xy / l;
    if (n.z < 0.0) p = (1.0 - abs(p.yx)) * sign(p);
    return p * 0.5 + 0.5;
}

// Chebyshev upper-bound weight for visibility gating (McGuire et al. 2017).
float chebyshev_weight(float mean, float mean_sq, float dist) {
    if (dist <= mean) return 1.0;
    float variance = mean_sq - mean * mean;
    float d        = dist - mean;
    return variance / (variance + d * d);
}

// Sample irradiance atlas UV for a probe (flat_idx) and normal n.
vec2 probe_irr_uv(uint flat_idx, vec3 n) {
    uint pz  = flat_idx / (pc.probes_dim.x * pc.probes_dim.y);
    uint rem = flat_idx % (pc.probes_dim.x * pc.probes_dim.y);
    uint py  = rem / pc.probes_dim.x;
    uint px  = rem % pc.probes_dim.x;
    float fs = float(pc.probe_face_size);
    vec2  face_uv = oct_encode(n);
    float tile_x  = float(px + pz * pc.probes_dim.x) + face_uv.x;
    float tile_y  = float(py) + face_uv.y;
    float atlas_w = float(pc.probes_dim.x * pc.probes_dim.z) * fs;
    float atlas_h = float(pc.probes_dim.y) * fs;
    return vec2(tile_x * fs / atlas_w, tile_y * fs / atlas_h);
}

void main() {
    vec3 p = v_world_pos;
    vec3 n = normalize(v_normal);

    // Grid-space fractional position.
    vec3  gp = (p - pc.grid_origin) / pc.grid_spacing;
    ivec3 b0 = ivec3(floor(gp));
    vec3  f  = gp - vec3(b0);

    vec3  irr_accum = vec3(0.0);
    float w_total   = 0.0;

    // Trilinear loop over 8 nearest probes.
    for (int dz = 0; dz <= 1; ++dz)
    for (int dy = 0; dy <= 1; ++dy)
    for (int dx = 0; dx <= 1; ++dx) {
        ivec3 c = b0 + ivec3(dx, dy, dz);
        // Clamp to grid bounds.
        c = clamp(c, ivec3(0),
                  ivec3(int(pc.probes_dim.x)-1,
                        int(pc.probes_dim.y)-1,
                        int(pc.probes_dim.z)-1));
        uint flat_idx = uint(c.x) + pc.probes_dim.x *
            (uint(c.y) + pc.probes_dim.y * uint(c.z));

        // Trilinear weight.
        float wx = (dx == 0) ? (1.0 - f.x) : f.x;
        float wy = (dy == 0) ? (1.0 - f.y) : f.y;
        float wz = (dz == 0) ? (1.0 - f.z) : f.z;
        float trilinear = wx * wy * wz;

        // Probe world pos.
        vec3 probe_pos = pc.grid_origin
            + vec3(float(c.x), float(c.y), float(c.z)) * pc.grid_spacing;
        vec3  to_probe = probe_pos - p;
        float dist     = length(to_probe);

        // Chebyshev visibility gate.
        vec2 vis_uv  = probe_irr_uv(flat_idx, -normalize(to_probe));
        vec2 vis     = texture(visibility_atlas, vis_uv).rg;
        float cheb_w = chebyshev_weight(vis.x, vis.y, dist);

        // Backface weight (prevent probes behind the surface normal).
        float backface = max(0.0, dot(normalize(to_probe), n));
        float w = trilinear * max(0.001, backface * cheb_w);

        // Sample irradiance in normal direction.
        vec2  irr_uv = probe_irr_uv(flat_idx, n);
        vec3  irr    = texture(irradiance_atlas, irr_uv).rgb;

        irr_accum += irr * w;
        w_total   += w;
    }

    vec3 indirect = (w_total > 1e-6) ? (irr_accum / w_total) : pc.sky_color;
    out_indirect  = vec4(indirect, 1.0);
}
)glsl";

/// Sprint-3 (phase570) — compute-shader counterpart of kDdgiSampleFS. Reads
/// world-position + world-normal G-buffer images, samples the probe atlases
/// for the 8 nearest probes, applies trilinear + Chebyshev visibility +
/// backface gating, and writes per-pixel indirect irradiance into an output
/// storage image (RGBA16F). One thread per output pixel; workgroup 8×8×1.
///
/// All images are bound as storage images (read or write) to keep the
/// descriptor-set / barrier graph uniform across the trace/blend/sample
/// pipeline trio. A pure FS variant lives at kDdgiSampleFS for callers that
/// prefer hooking into a graphics framebuffer pass; the math is identical.
constexpr std::string_view kDdgiSampleCS = R"glsl(
#version 460

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

// Output: per-pixel indirect irradiance.
layout(set = 0, binding = 0, rgba16f) uniform image2D out_indirect;
// G-Buffer: world position (xyz) — w unused.
layout(set = 0, binding = 1, rgba16f) uniform image2D world_pos_image;
// G-Buffer: world normal (xyz, normalised) — w unused.
layout(set = 0, binding = 2, rgba16f) uniform image2D world_normal_image;
// Probe atlases.
layout(set = 0, binding = 3, rgba16f) uniform image2D irradiance_atlas;
layout(set = 0, binding = 4, rg16f)   uniform image2D visibility_atlas;

layout(push_constant) uniform PC {
    vec3  grid_origin;   float _pad0;
    vec3  grid_spacing;  float _pad1;
    uvec4 probes_dim;
    uint  probe_face_size;
    uint  output_width;
    uint  output_height;
    uint  _pad3;
    vec3  sky_color;     float _pad4;
} pc;

vec2 oct_encode(vec3 n) {
    float l = abs(n.x) + abs(n.y) + abs(n.z);
    vec2  p = n.xy / l;
    if (n.z < 0.0) p = (1.0 - abs(p.yx)) * sign(p);
    return p * 0.5 + 0.5;
}

// Chebyshev upper-bound weight for visibility gating (McGuire et al. 2017).
float chebyshev_weight(float mean, float mean_sq, float dist) {
    if (dist <= mean) return 1.0;
    float variance = max(0.0, mean_sq - mean * mean);
    float d        = dist - mean;
    return variance / (variance + d * d);
}

// Per-probe atlas texel coordinate for direction `n`.
ivec2 probe_atlas_coord(uint flat_idx, vec3 n) {
    uint pz  = flat_idx / (pc.probes_dim.x * pc.probes_dim.y);
    uint rem = flat_idx % (pc.probes_dim.x * pc.probes_dim.y);
    uint py  = rem / pc.probes_dim.x;
    uint px  = rem % pc.probes_dim.x;
    vec2 face_uv = oct_encode(n);
    uint fs = pc.probe_face_size;
    ivec2 base = ivec2(int((px + pz * pc.probes_dim.x) * fs),
                       int(py * fs));
    ivec2 local = ivec2(clamp(int(face_uv.x * float(fs)), 0, int(fs) - 1),
                        clamp(int(face_uv.y * float(fs)), 0, int(fs) - 1));
    return base + local;
}

void main() {
    uvec2 pix = gl_GlobalInvocationID.xy;
    if (pix.x >= pc.output_width || pix.y >= pc.output_height) return;

    ivec2 ipix = ivec2(pix);
    vec3 wp = imageLoad(world_pos_image,    ipix).xyz;
    vec3 n_raw = imageLoad(world_normal_image, ipix).xyz;
    float nl = length(n_raw);
    vec3 n  = (nl > 1e-6) ? (n_raw / nl) : vec3(0.0, 1.0, 0.0);

    // Grid-space fractional position.
    vec3  gp = (wp - pc.grid_origin) / pc.grid_spacing;
    ivec3 b0 = ivec3(floor(gp));
    vec3  f  = gp - vec3(b0);

    vec3  irr_accum = vec3(0.0);
    float w_total   = 0.0;

    for (int dz = 0; dz <= 1; ++dz)
    for (int dy = 0; dy <= 1; ++dy)
    for (int dx = 0; dx <= 1; ++dx) {
        ivec3 c = b0 + ivec3(dx, dy, dz);
        c = clamp(c, ivec3(0),
                  ivec3(int(pc.probes_dim.x) - 1,
                        int(pc.probes_dim.y) - 1,
                        int(pc.probes_dim.z) - 1));
        uint flat_idx = uint(c.x) + pc.probes_dim.x *
            (uint(c.y) + pc.probes_dim.y * uint(c.z));

        float wx = (dx == 0) ? (1.0 - f.x) : f.x;
        float wy = (dy == 0) ? (1.0 - f.y) : f.y;
        float wz = (dz == 0) ? (1.0 - f.z) : f.z;
        float trilinear = wx * wy * wz;

        vec3 probe_pos = pc.grid_origin
            + vec3(float(c.x), float(c.y), float(c.z)) * pc.grid_spacing;
        vec3  to_probe = probe_pos - wp;
        float dist     = length(to_probe);
        vec3  to_probe_n = (dist > 1e-6) ? (to_probe / dist) : vec3(0.0, 1.0, 0.0);

        // Visibility (Chebyshev) — sample atlas at -to_probe direction.
        ivec2 vis_coord = probe_atlas_coord(flat_idx, -to_probe_n);
        vec2  vis       = imageLoad(visibility_atlas, vis_coord).rg;
        float cheb_w    = chebyshev_weight(vis.x, vis.y, dist);

        // Backface weight (prevent probes behind the surface normal).
        float backface = max(0.0, dot(to_probe_n, n));
        float w = trilinear * max(0.001, backface * cheb_w);

        // Sample irradiance in normal direction.
        ivec2 irr_coord = probe_atlas_coord(flat_idx, n);
        vec3  irr       = imageLoad(irradiance_atlas, irr_coord).rgb;

        irr_accum += irr * w;
        w_total   += w;
    }

    vec3 indirect = (w_total > 1e-6) ? (irr_accum / w_total) : pc.sky_color;
    imageStore(out_indirect, ipix, vec4(indirect, 1.0));
}
)glsl";

// Alias kept for backwards compatibility (was kProbeUpdateCS in early skeleton).
constexpr std::string_view kProbeUpdateCS = kDdgiTraceCS;

}  // namespace cd::ddgi
