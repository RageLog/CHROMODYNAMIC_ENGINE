// =============================================================================
// CHROMODYNAMIC — cd/render/cluster/ClusterGrid.hpp
// Phase 7 / Sprint 9 / Waves 84-85 — clustered forward+ CPU baseline.
//
// 3D X × Y × log(Z) cluster grid over a view-space frustum. Lights
// (sphere {view_pos, radius}) are assigned to every cluster whose
// AABB they overlap. PBR / forward shader fetches the per-cluster
// light list and shades against only the listed lights.
//
// Conventions:
//   * Right-handed view space, camera looking down -Z, +Y up.
//   * view_depth = -view_pos.z (positive, in metres).
//   * NDC = (-1, +1) per axis post-perspective-divide.
//
// Storage shape (designed for direct GPU port):
//   * cluster_offsets_ : uint32 [X·Y·Z + 1] — prefix-sum of light list
//                        lengths. Cluster k's light range is
//                        light_indices_[offsets_[k] .. offsets_[k+1]).
//   * light_indices_   : uint32 [total]    — packed per-cluster light
//                        ID streams.
//
// Build is two-pass:
//   1. assign_light() pushes (cluster_id, light_index) into a temp
//      bucket vector. O(lights × cluster-coverage-per-light).
//   2. finalize() sorts the buckets by cluster_id then packs into the
//      offsets / indices arrays. Required before lights_in_cluster()
//      returns anything; subsequent assign_light() calls invalidate
//      the packed view (will need another finalize()).
//
// Header-only; cd::core + cd::math only.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Vector.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace cd::render::cluster
{

// -----------------------------------------------------------------------------
// Canonical Froxel / Clustered-Shading Math Primitives (Wave 1.2 Deduplication)
// Shared by cd::render::cluster, cd::light, and cd::render::lighting_clusters.
// -----------------------------------------------------------------------------

/// Row-major 3D froxel linear index: ((z * cells_y + y) * cells_x + x).
[[nodiscard]] constexpr std::uint32_t linear_cluster_index(
    std::uint32_t x,
    std::uint32_t y,
    std::uint32_t z,
    std::uint32_t cells_x,
    std::uint32_t cells_y) noexcept
{
    return (z * cells_y + y) * cells_x + x;
}

/// Logarithmic Z partition: maps view-space depth in [near_plane, far_plane]
/// to slice index in [0, slices_z).
[[nodiscard]] inline std::uint32_t depth_to_log_z_slice(
    float         depth,
    float         near_plane,
    float         far_plane,
    std::uint32_t slices_z) noexcept
{
    if (slices_z == 0U)
    {
        return 0U;
    }
    const float clamped = std::clamp(depth, near_plane, far_plane);
    if (clamped >= far_plane)
    {
        return slices_z - 1U;
    }
    const float t = std::log(clamped / near_plane)
                  / std::log(far_plane / near_plane);
    const auto z = static_cast<std::uint32_t>(
        std::floor(t * static_cast<float>(slices_z)));
    return std::min(z, slices_z - 1U);
}

/// View-space depth of the near boundary of logarithmic slice `slice`.
[[nodiscard]] inline float log_z_slice_near_depth(
    std::uint32_t slice,
    float         near_plane,
    float         far_plane,
    std::uint32_t slices_z) noexcept
{
    const float t = static_cast<float>(slice) / static_cast<float>(slices_z);
    return near_plane * std::pow(far_plane / near_plane, t);
}

/// Near and far view-space depths [z_near, z_far] of logarithmic slice `slice`.
[[nodiscard]] inline std::pair<float, float> log_z_slice_range(
    std::uint32_t slice,
    float         near_plane,
    float         far_plane,
    std::uint32_t slices_z) noexcept
{
    return {
        log_z_slice_near_depth(slice,      near_plane, far_plane, slices_z),
        log_z_slice_near_depth(slice + 1U, near_plane, far_plane, slices_z),
    };
}

/// Map normalized device coordinate (NDC in [-1, +1]) to tile index in [0, tiles).
[[nodiscard]] inline std::uint32_t ndc_to_tile_index(
    float         ndc,
    std::uint32_t tiles) noexcept
{
    if (tiles == 0U)
    {
        return 0U;
    }
    const float u = (ndc + 1.0F) * 0.5F * static_cast<float>(tiles);
    const auto  t = static_cast<std::int32_t>(std::floor(u));
    return static_cast<std::uint32_t>(
        std::clamp(t, 0, static_cast<std::int32_t>(tiles) - 1));
}

struct ClusterConfig
{
    std::uint32_t cells_x { 16 };
    std::uint32_t cells_y { 9 };
    std::uint32_t cells_z { 24 };
    /// Vertical field of view in radians.
    float fov_y_rad { 1.0472F };  // ≈ 60°
    /// width / height of the projection plane.
    float aspect { 16.0F / 9.0F };
    /// Near / far in metres along the view-space -Z axis (positive).
    float near_plane { 0.1F };
    float far_plane { 1000.0F };
};

struct LightSphere
{
    cd::math::Vec3f view_pos { 0.0F, 0.0F, 0.0F };
    float radius { 1.0F };
};

class ClusterGrid
{
public:
    explicit ClusterGrid(const ClusterConfig& cfg) noexcept : cfg_ { cfg }
    {
        // Pre-size the offset array; finalize() fills it.
        cluster_offsets_.assign(total_cluster_count_() + 1U, 0U);
    }

    /// Reset all per-light assignments. Configuration is preserved.
    void clear() noexcept
    {
        buckets_.clear();
        light_indices_.clear();
        std::ranges::fill(cluster_offsets_, 0U);
        finalized_ = false;
    }

    /// Add the given light to every cluster whose extent overlaps the
    /// sphere. Multiple lights with the same `light_index` is legal
    /// but typically a caller bug — the cluster lookup will list the
    /// index multiple times.
    void assign_light(std::uint32_t light_index, const LightSphere& sphere)
    {
        // Compute cluster_z_min / _max via view-space depth ± radius.
        const float depth_centre = -sphere.view_pos.z;
        const float depth_min = std::max(cfg_.near_plane, depth_centre - sphere.radius);
        const float depth_max = std::min(cfg_.far_plane, depth_centre + sphere.radius);
        if (depth_max < cfg_.near_plane || depth_min > cfg_.far_plane)
            return;  // sphere entirely outside the frustum z-range
        const auto cz_min = depth_to_cluster_z_(depth_min);
        const auto cz_max = depth_to_cluster_z_(depth_max);

        // Angular extent of the sphere from the camera. tan_half_x/y
        // bound the horizontal / vertical projection NDC.
        // Conservative: use depth_min (nearest extent) for the widest
        // angular envelope.
        const float depth_for_angle = std::max(depth_min, 1.0e-3F);
        const float ang_half = std::atan2(sphere.radius, depth_for_angle);

        // Centre direction in view-space angles (atan2 of x / z and y / z).
        const float centre_angle_x = std::atan2(sphere.view_pos.x, depth_for_angle);
        const float centre_angle_y = std::atan2(sphere.view_pos.y, depth_for_angle);

        const float ang_x_min = centre_angle_x - ang_half;
        const float ang_x_max = centre_angle_x + ang_half;
        const float ang_y_min = centre_angle_y - ang_half;
        const float ang_y_max = centre_angle_y + ang_half;

        // Cluster X span — angle → NDC via tan(half_fov_x) = aspect * tan(half_fov_y).
        const float half_fov_y = cfg_.fov_y_rad * 0.5F;
        const float tan_half_y = std::tan(half_fov_y);
        const float tan_half_x = cfg_.aspect * tan_half_y;
        const auto cx_min = angle_to_cluster_x_(ang_x_min, tan_half_x);
        const auto cx_max = angle_to_cluster_x_(ang_x_max, tan_half_x);
        const auto cy_min = angle_to_cluster_y_(ang_y_min, tan_half_y);
        const auto cy_max = angle_to_cluster_y_(ang_y_max, tan_half_y);

        // Push (cluster_id, light_index) for every cluster in the box.
        for (std::uint32_t z = cz_min; z <= cz_max; ++z)
            for (std::uint32_t y = cy_min; y <= cy_max; ++y)
                for (std::uint32_t x = cx_min; x <= cx_max; ++x)
                {
                    const auto cid = cluster_index_(x, y, z);
                    buckets_.push_back({ cid, light_index });
                }
        finalized_ = false;
    }

    /// Sort + pack the per-cluster light lists. Must be called after
    /// all assign_light() calls and before lights_in_cluster() is
    /// expected to return non-empty results.
    void finalize()
    {
        std::ranges::sort(buckets_,
                          [](const Bucket& a, const Bucket& b) {
                              return a.cluster_id < b.cluster_id;
                          });
        // Build offsets via cumulative count.
        std::ranges::fill(cluster_offsets_, 0U);
        for (const auto& b : buckets_)
            ++cluster_offsets_[b.cluster_id + 1U];
        for (std::size_t i = 1; i < cluster_offsets_.size(); ++i)
            cluster_offsets_[i] += cluster_offsets_[i - 1];
        // Pack indices.
        light_indices_.assign(buckets_.size(), 0U);
        std::vector<std::uint32_t> write_cursor = cluster_offsets_;
        for (const auto& b : buckets_)
        {
            const auto pos = write_cursor[b.cluster_id]++;
            light_indices_[pos] = b.light_index;
        }
        finalized_ = true;
    }

    [[nodiscard]] std::span<const std::uint32_t>
    lights_in_cluster(std::uint32_t x, std::uint32_t y, std::uint32_t z) const noexcept
    {
        if (!finalized_)
            return {};
        if (x >= cfg_.cells_x || y >= cfg_.cells_y || z >= cfg_.cells_z)
            return {};
        const auto cid = cluster_index_(x, y, z);
        const auto begin = cluster_offsets_[cid];
        const auto end = cluster_offsets_[cid + 1];
        return { light_indices_.data() + begin, end - begin };
    }

    [[nodiscard]] const ClusterConfig& config() const noexcept { return cfg_; }
    [[nodiscard]] std::size_t total_cluster_count() const noexcept
    {
        return total_cluster_count_();
    }
    [[nodiscard]] std::size_t total_light_assignments() const noexcept
    {
        return light_indices_.size();
    }
    [[nodiscard]] bool finalized() const noexcept { return finalized_; }

private:
    struct Bucket
    {
        std::uint32_t cluster_id;
        std::uint32_t light_index;
    };

    [[nodiscard]] std::size_t total_cluster_count_() const noexcept
    {
        return static_cast<std::size_t>(cfg_.cells_x) * cfg_.cells_y * cfg_.cells_z;
    }

    [[nodiscard]] std::uint32_t cluster_index_(std::uint32_t x, std::uint32_t y,
                                                std::uint32_t z) const noexcept
    {
        return linear_cluster_index(x, y, z, cfg_.cells_x, cfg_.cells_y);
    }

    /// Log-z partition: depth ∈ [near, far] → cluster_z ∈ [0, cells_z).
    [[nodiscard]] std::uint32_t depth_to_cluster_z_(float depth) const noexcept
    {
        return depth_to_log_z_slice(depth, cfg_.near_plane, cfg_.far_plane, cfg_.cells_z);
    }

    /// Map a horizontal angle (radians) to a cluster_x in [0, cells_x).
    /// Angle is converted to NDC via NDC = tan(angle) / tan(half_fov),
    /// then to cluster via (NDC + 1) / 2 * cells.
    [[nodiscard]] std::uint32_t angle_to_cluster_x_(float angle, float tan_half) const noexcept
    {
        const float t = std::clamp(std::tan(angle) / tan_half, -1.0F, 1.0F);
        return ndc_to_tile_index(t, cfg_.cells_x);
    }

    [[nodiscard]] std::uint32_t angle_to_cluster_y_(float angle, float tan_half) const noexcept
    {
        const float t = std::clamp(std::tan(angle) / tan_half, -1.0F, 1.0F);
        return ndc_to_tile_index(t, cfg_.cells_y);
    }

    ClusterConfig cfg_;
    std::vector<Bucket> buckets_;
    std::vector<std::uint32_t> cluster_offsets_;
    std::vector<std::uint32_t> light_indices_;
    bool finalized_ { false };
};

}  // namespace cd::render::cluster
