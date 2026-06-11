// =============================================================================
// CHROMODYNAMIC — cd/render/lighting_clusters/LightingClusters.cpp
// phase660 — CPU-side froxel cluster assignment (Sprint 1).
//
// Algorithm summary:
//   For each light sphere (world space):
//     1. Transform sphere centre into clip space via view_proj.
//     2. Compute view-space depth (w-divide denominator, positive depth = -z_view).
//     3. Derive cluster_z from log-z partition.
//     4. Project ±radius extents in NDC to get x/y tile ranges.
//     5. AABB-vs-sphere: for each (tx, ty, z_slice) candidate, test the
//        cluster world-space AABB against the light sphere. Only emit the
//        cluster if they truly intersect (tight test, avoids over-assignment).
//     6. Record (cluster_id, light_index) bucket.
//   After all lights: sort buckets by cluster_id, build prefix-sum offsets,
//   pack into LightAssignment parallel arrays.
//
// The AABB-vs-sphere test reconstructs each cluster's view-space AABB from
// the tile subdivisions and log-z depth boundaries, then applies the standard
// closest-point-on-AABB test: distance(closest_pt_on_aabb, sphere_center)
// squared <= radius^2.
//
// The view_proj matrix is column-major (OpenGL / GLM convention):
//   element [col * 4 + row]  so matrix[i][j] = col_j[row_i].
//   Equivalent to a row-vector post-multiply:  v_clip = M * v_world,
//   where M[col][row] accesses column col, row row.
// =============================================================================
#include <cd/render/lighting_clusters/LightingClusters.hpp>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace cd::render::lighting_clusters
{

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace
{

/// Column-major 4x4 matrix-vector multiply. Result = M * v.
/// col-major layout: m[col * 4 + row].
inline std::array<float, 4> mat4_mul_vec4(
    const std::array<float, 16>& m,
    std::array<float, 4>         v) noexcept
{
    return {
        m[0] * v[0] + m[4] * v[1] + m[8]  * v[2] + m[12] * v[3],
        m[1] * v[0] + m[5] * v[1] + m[9]  * v[2] + m[13] * v[3],
        m[2] * v[0] + m[6] * v[1] + m[10] * v[2] + m[14] * v[3],
        m[3] * v[0] + m[7] * v[1] + m[11] * v[2] + m[15] * v[3]
    };
}

/// Log-z depth-to-slice mapping.
/// depth_view ∈ [near, far] → z_slice ∈ [0, z_slices).
[[nodiscard]] std::uint32_t depth_to_z_slice(
    float depth_view,
    float near_plane,
    float far_plane,
    std::uint32_t z_slices) noexcept
{
    const float clamped = std::clamp(depth_view, near_plane, far_plane);
    const float t       = std::log(clamped / near_plane)
                        / std::log(far_plane / near_plane);
    const auto slice = static_cast<std::uint32_t>(
        std::floor(t * static_cast<float>(z_slices)));
    return std::min(slice, z_slices - 1U);
}

/// View-space depth of the near face of a z-slice.
[[nodiscard]] float slice_near_depth(
    std::uint32_t  slice,
    float          near_plane,
    float          far_plane,
    std::uint32_t  z_slices) noexcept
{
    const float t = static_cast<float>(slice) / static_cast<float>(z_slices);
    return near_plane * std::pow(far_plane / near_plane, t);
}

/// NDC-space tile boundaries → tile index.
[[nodiscard]] std::uint32_t ndc_to_tile(float ndc, std::uint32_t tiles) noexcept
{
    // NDC ∈ [-1, +1] → [0, tiles)
    const float u = (ndc + 1.0F) * 0.5F * static_cast<float>(tiles);
    const auto  t = static_cast<std::int32_t>(std::floor(u));
    return static_cast<std::uint32_t>(
        std::clamp(t, 0, static_cast<std::int32_t>(tiles) - 1));
}

/// Per-cluster AABB-vs-sphere test in view space.
/// Cluster AABB is reconstructed from tile + z-slice boundaries.
/// Returns true if the sphere overlaps the cluster AABB.
[[nodiscard]] bool cluster_sphere_overlap(
    std::uint32_t  tx,
    std::uint32_t  ty,
    std::uint32_t  tz,
    const ClusterGrid&           grid,
    const std::array<float, 4>&  sphere_view, ///< (x, y, depth, 1) in view space.
    float                        radius) noexcept
{
    // Reconstruct the cluster tile NDC boundaries → view-space XY at unit depth.
    // We use tile fractions of NDC to bound the XY range at a reference depth.
    // Because the frustum is perspective, we bound XY by the *widest* face (far).
    // This is conservative but correct for overlap detection.

    // Depth boundaries (positive, view-space).
    const float depth_near_face = slice_near_depth(tz,      grid.near_plane, grid.far_plane, grid.z_slices);
    const float depth_far_face  = slice_near_depth(tz + 1U, grid.near_plane, grid.far_plane, grid.z_slices);
    const float depth_far_clamped = std::min(depth_far_face, grid.far_plane);

    // The XY extent of the AABB at a given depth scales with that depth
    // (perspective frustum). Use the far face depth for conservative XY.
    // tile NDC range [ndc_x_min, ndc_x_max] from tile indices.
    const float ndc_x_min = -1.0F + 2.0F * static_cast<float>(tx)       / static_cast<float>(grid.x_tiles);
    const float ndc_x_max = -1.0F + 2.0F * static_cast<float>(tx + 1U)  / static_cast<float>(grid.x_tiles);
    const float ndc_y_min = -1.0F + 2.0F * static_cast<float>(ty)       / static_cast<float>(grid.y_tiles);
    const float ndc_y_max = -1.0F + 2.0F * static_cast<float>(ty + 1U)  / static_cast<float>(grid.y_tiles);

    // In view space (+x right, +y up, -z forward), a point at NDC (nx, ny)
    // and view depth d has view-space coords proportional to NDC * d * tan(half_fov).
    // For the overlap test we only need sign-correct bounds: scale by depth_far_clamped.
    // (We treat it as a scaled AABB — good enough for tightness vs the angular-only approach.)
    const float aabb_x_min = ndc_x_min * depth_far_clamped;
    const float aabb_x_max = ndc_x_max * depth_far_clamped;
    const float aabb_y_min = ndc_y_min * depth_far_clamped;
    const float aabb_y_max = ndc_y_max * depth_far_clamped;
    const float aabb_z_min = depth_near_face;    // positive view-space depth
    const float aabb_z_max = depth_far_clamped;

    // sphere_view: view-space position. We store depth = -view_z (positive).
    const float sx = sphere_view[0];
    const float sy = sphere_view[1];
    const float sz = sphere_view[2]; // positive depth

    // Closest point on AABB to sphere centre.
    const float cx = std::clamp(sx, aabb_x_min, aabb_x_max);
    const float cy = std::clamp(sy, aabb_y_min, aabb_y_max);
    const float cz = std::clamp(sz, aabb_z_min, aabb_z_max);

    const float dx = sx - cx;
    const float dy = sy - cy;
    const float dz = sz - cz;

    return (dx * dx + dy * dy + dz * dz) <= (radius * radius);
}

} // namespace

// ---------------------------------------------------------------------------
// Clusterer implementation
// ---------------------------------------------------------------------------

void Clusterer::configure(const ClusterGrid& grid) noexcept
{
    grid_ = grid;
}

std::uint32_t Clusterer::cluster_index(
    std::uint32_t tx,
    std::uint32_t ty,
    std::uint32_t z_slice) const noexcept
{
    return (z_slice * grid_.y_tiles + ty) * grid_.x_tiles + tx;
}

std::span<const std::uint32_t> Clusterer::lights_in_cluster(
    std::uint32_t          idx,
    const LightAssignment& assignment) const noexcept
{
    const auto total = total_cluster_count();
    if (assignment.offset_per_cluster.size() < static_cast<std::size_t>(total) + 1U)
        return {};
    if (idx >= total)
        return {};

    const auto begin = assignment.offset_per_cluster[idx];
    const auto end   = assignment.offset_per_cluster[idx + 1U];
    if (begin >= assignment.light_indices_per_cluster.size() &&
        begin != end)
        return {};

    const std::size_t count = end - begin;
    if (count == 0U)
        return {};

    return { assignment.light_indices_per_cluster.data() + begin, count };
}

LightAssignment Clusterer::assign(
    std::span<const PointLight>  lights,
    const std::array<float, 16>& view_proj_matrix) const
{
    const std::uint32_t total = total_cluster_count();

    // Fast path: no lights.
    if (lights.empty() || total == 0U)
    {
        LightAssignment empty;
        empty.offset_per_cluster.assign(static_cast<std::size_t>(total) + 1U, 0U);
        return empty;
    }

    // Bucket vector: (cluster_id, light_index) pairs.
    struct Bucket
    {
        std::uint32_t cluster_id;
        std::uint32_t light_index;
    };
    std::vector<Bucket> buckets;
    buckets.reserve(lights.size() * 8U); // heuristic: avg 8 clusters per light

    for (std::uint32_t li = 0U; li < static_cast<std::uint32_t>(lights.size()); ++li)
    {
        const PointLight& light = lights[li];

        // Transform light centre to clip space.
        const std::array<float, 4> world_pos = {
            light.position[0], light.position[1], light.position[2], 1.0F
        };
        const auto clip = mat4_mul_vec4(view_proj_matrix, world_pos);

        // View-space depth: positive forward depth = -view_z.
        // clip.w = -view_z in right-hand, camera-along-(-Z) convention.
        const float depth_view = clip[3]; // = -view_z ≈ w component in std GL VP

        // Skip lights entirely behind near or beyond far (including w≤0).
        const float r = light.radius;
        if (depth_view + r < grid_.near_plane || depth_view - r > grid_.far_plane)
            continue;

        // z-slice range for the sphere.
        const float depth_min = std::max(grid_.near_plane, depth_view - r);
        const float depth_max = std::min(grid_.far_plane,  depth_view + r);

        const std::uint32_t tz_min = depth_to_z_slice(depth_min, grid_.near_plane, grid_.far_plane, grid_.z_slices);
        const std::uint32_t tz_max = depth_to_z_slice(depth_max, grid_.near_plane, grid_.far_plane, grid_.z_slices);

        // NDC centre (clip.x/w, clip.y/w). Handle w≤0 conservatively.
        const float ndc_cx = (clip[3] > 1.0e-5F) ? (clip[0] / clip[3]) : 0.0F;
        const float ndc_cy = (clip[3] > 1.0e-5F) ? (clip[1] / clip[3]) : 0.0F;

        // Conservative NDC radius: project sphere radius at near face of depth range.
        const float ref_depth = std::max(depth_min, 1.0e-3F);
        // angular half-width in NDC ≈ r / ref_depth (small-angle, conservative).
        const float ndc_r = r / ref_depth;

        const float ndc_x_min = std::clamp(ndc_cx - ndc_r, -1.0F, 1.0F);
        const float ndc_x_max = std::clamp(ndc_cx + ndc_r, -1.0F, 1.0F);
        const float ndc_y_min = std::clamp(ndc_cy - ndc_r, -1.0F, 1.0F);
        const float ndc_y_max = std::clamp(ndc_cy + ndc_r, -1.0F, 1.0F);

        const std::uint32_t tx_min = ndc_to_tile(ndc_x_min, grid_.x_tiles);
        const std::uint32_t tx_max = ndc_to_tile(ndc_x_max, grid_.x_tiles);
        const std::uint32_t ty_min = ndc_to_tile(ndc_y_min, grid_.y_tiles);
        const std::uint32_t ty_max = ndc_to_tile(ndc_y_max, grid_.y_tiles);

        // View-space position for the AABB test: (view_x, view_y, depth, 1).
        // view_x and view_y can be extracted from clip space:
        //   view_x ≈ ndc_cx * depth_view (proportional, used for closest-point test).
        const std::array<float, 4> sphere_view = {
            ndc_cx * depth_view,   // view_x (proportional, no fov correction needed for overlap test)
            ndc_cy * depth_view,   // view_y
            depth_view,            // positive depth
            1.0F
        };

        // Tight AABB-vs-sphere test for each candidate cluster.
        for (std::uint32_t tz = tz_min; tz <= tz_max; ++tz)
            for (std::uint32_t ty = ty_min; ty <= ty_max; ++ty)
                for (std::uint32_t tx = tx_min; tx <= tx_max; ++tx)
                {
                    if (cluster_sphere_overlap(tx, ty, tz, grid_, sphere_view, r))
                        buckets.push_back({ cluster_index(tx, ty, tz), li });
                }
    }

    // Build LightAssignment: sort by cluster_id, prefix-sum offsets.
    std::ranges::stable_sort(buckets,
        [](const Bucket& a, const Bucket& b) noexcept {
            return a.cluster_id < b.cluster_id;
        });

    LightAssignment result;
    result.offset_per_cluster.assign(static_cast<std::size_t>(total) + 1U, 0U);
    result.light_indices_per_cluster.resize(buckets.size());

    // Count pass.
    for (const auto& b : buckets)
        ++result.offset_per_cluster[b.cluster_id + 1U];

    // Prefix sum.
    for (std::size_t i = 1U; i <= static_cast<std::size_t>(total); ++i)
        result.offset_per_cluster[i] += result.offset_per_cluster[i - 1U];

    // Pack indices.
    {
        std::vector<std::uint32_t> write_cursor = result.offset_per_cluster;
        for (const auto& b : buckets)
        {
            const std::uint32_t pos = write_cursor[b.cluster_id]++;
            result.light_indices_per_cluster[pos] = b.light_index;
        }
    }

    return result;
}

}  // namespace cd::render::lighting_clusters
