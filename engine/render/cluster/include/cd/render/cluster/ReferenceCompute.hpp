// =============================================================================
// CHROMODYNAMIC — cd/render/cluster/ReferenceCompute.hpp
// Phase 8 / Wave 88 — CPU reference simulator for cluster_assign.comp.
//
// Mirrors the GLSL compute shader's per-thread algorithm exactly, so
// the GPU pipeline (Wave 89) can be parity-tested against this
// reference without a GPU. Per-cluster:
//   PHASE 0: count lights that overlap this cluster.
//   PHASE 1: write the overlapping light indices at the cluster's
//             pre-computed slot in light_indices.
//
// CPU prefix-sum sits between the two passes (the GPU pipeline runs
// it on the host between two compute dispatches; here we just call
// it in `run()`).
//
// Output buffer shape matches ClusterGrid (cluster_offsets +
// light_indices), so a parity test can compare directly.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/render/cluster/ClusterGrid.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

namespace cd::render::cluster
{

/// Output buffers in the same shape the GLSL compute shader produces.
/// Caller (typically a parity test) inspects these to verify GPU
/// output matches.
struct ReferenceOutput
{
    std::vector<std::uint32_t> cluster_counts;    ///< size = X·Y·Z
    std::vector<std::uint32_t> cluster_offsets;   ///< size = X·Y·Z + 1
    std::vector<std::uint32_t> light_indices;     ///< size = total
};

namespace ref_detail
{

[[nodiscard]] inline std::uint32_t depth_to_cluster_z(
    float depth, float near_plane, float far_plane, std::uint32_t cells_z) noexcept
{
    const float clamped = std::clamp(depth, near_plane, far_plane);
    const float t = std::log(clamped / near_plane) / std::log(far_plane / near_plane);
    const auto z = static_cast<std::uint32_t>(
        std::floor(t * static_cast<float>(cells_z)));
    return std::min(z, cells_z - 1U);
}

[[nodiscard]] inline std::uint32_t
angle_to_cluster_axis(float angle, float tan_half, std::uint32_t cells) noexcept
{
    const float t = std::clamp(std::tan(angle) / tan_half, -1.0F, 1.0F);
    const float u = (t + 1.0F) * 0.5F * static_cast<float>(cells);
    const auto v = static_cast<std::uint32_t>(std::clamp(
        std::floor(u), 0.0F, static_cast<float>(cells - 1)));
    return v;
}

[[nodiscard]] inline bool light_overlaps_cluster(
    const LightSphere& light, const ClusterConfig& cfg,
    std::uint32_t cx, std::uint32_t cy, std::uint32_t cz)
{
    const float depth_centre = -light.view_pos.z;
    const float depth_min = std::max(cfg.near_plane, depth_centre - light.radius);
    const float depth_max = std::min(cfg.far_plane, depth_centre + light.radius);
    if (depth_max < cfg.near_plane || depth_min > cfg.far_plane)
        return false;
    const auto cz_min = depth_to_cluster_z(depth_min, cfg.near_plane, cfg.far_plane, cfg.cells_z);
    const auto cz_max = depth_to_cluster_z(depth_max, cfg.near_plane, cfg.far_plane, cfg.cells_z);
    if (cz < cz_min || cz > cz_max)
        return false;

    const float depth_for_angle = std::max(depth_min, 1.0e-3F);
    const float ang_half = std::atan2(light.radius, depth_for_angle);
    const float centre_x = std::atan2(light.view_pos.x, depth_for_angle);
    const float centre_y = std::atan2(light.view_pos.y, depth_for_angle);

    const float half_fov_y = cfg.fov_y_rad * 0.5F;
    const float tan_half_y = std::tan(half_fov_y);
    const float tan_half_x = cfg.aspect * tan_half_y;

    const auto cx_min = angle_to_cluster_axis(centre_x - ang_half, tan_half_x, cfg.cells_x);
    const auto cx_max = angle_to_cluster_axis(centre_x + ang_half, tan_half_x, cfg.cells_x);
    const auto cy_min = angle_to_cluster_axis(centre_y - ang_half, tan_half_y, cfg.cells_y);
    const auto cy_max = angle_to_cluster_axis(centre_y + ang_half, tan_half_y, cfg.cells_y);

    return cx >= cx_min && cx <= cx_max
        && cy >= cy_min && cy <= cy_max;
}

}  // namespace ref_detail

/// Run the two-pass cluster assignment exactly the way the GLSL
/// compute shader does. Iteration order matches GPU semantics:
/// per-cluster outer, per-light inner. Within a cluster, light
/// indices appear in ascending light-index order — same as the
/// shader's `for (i = 0; i < light_count; ++i)` loop.
[[nodiscard]] inline ReferenceOutput
run_reference_compute(const ClusterConfig& cfg, std::span<const LightSphere> lights)
{
    const std::size_t cluster_count = static_cast<std::size_t>(cfg.cells_x)
                                    * cfg.cells_y * cfg.cells_z;

    ReferenceOutput out;
    out.cluster_counts.assign(cluster_count, 0U);
    out.cluster_offsets.assign(cluster_count + 1, 0U);

    // PASS 1: count.
    for (std::uint32_t cz = 0; cz < cfg.cells_z; ++cz)
        for (std::uint32_t cy = 0; cy < cfg.cells_y; ++cy)
            for (std::uint32_t cx = 0; cx < cfg.cells_x; ++cx)
            {
                const std::size_t cid =
                    (static_cast<std::size_t>(cz) * cfg.cells_y + cy) * cfg.cells_x + cx;
                std::uint32_t count = 0;
                for (const auto& light : lights)
                    if (ref_detail::light_overlaps_cluster(light, cfg, cx, cy, cz))
                        ++count;
                out.cluster_counts[cid] = count;
            }

    // CPU prefix-sum.
    for (std::size_t i = 0; i < cluster_count; ++i)
        out.cluster_offsets[i + 1] = out.cluster_offsets[i] + out.cluster_counts[i];
    out.light_indices.assign(out.cluster_offsets.back(), 0U);

    // PASS 2: write.
    for (std::uint32_t cz = 0; cz < cfg.cells_z; ++cz)
        for (std::uint32_t cy = 0; cy < cfg.cells_y; ++cy)
            for (std::uint32_t cx = 0; cx < cfg.cells_x; ++cx)
            {
                const std::size_t cid =
                    (static_cast<std::size_t>(cz) * cfg.cells_y + cy) * cfg.cells_x + cx;
                std::uint32_t slot = out.cluster_offsets[cid];
                for (std::uint32_t i = 0; i < lights.size(); ++i)
                    if (ref_detail::light_overlaps_cluster(lights[i], cfg, cx, cy, cz))
                    {
                        out.light_indices[slot++] = i;
                    }
            }
    return out;
}

}  // namespace cd::render::cluster
