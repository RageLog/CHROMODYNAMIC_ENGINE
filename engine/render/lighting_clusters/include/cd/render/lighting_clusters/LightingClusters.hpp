// =============================================================================
// CHROMODYNAMIC — cd/render/lighting_clusters/LightingClusters.hpp
// phase660 — Forward+ / clustered-forward CPU baseline.
//
// 3D froxel grid over a view-space frustum. Point/spot lights (represented
// as spheres) are assigned to every cluster whose AABB they overlap.
// A PBR forward shader fetches the per-cluster light list and shades
// against only those lights — typically ~5 of 200+ visible lights per pixel.
//
// Design references:
//   * Olsson & Assarsson, "Tiled Shading", JCGT 2011.
//   * Olsson, Billeter & Assarsson, "Clustered Deferred and Forward Shading", HPG 2012.
//   * Doom 2016 / id Tech 6 cluster forward lighting, GDC 2016.
//   * Detroit: Become Human cluster forward reference (Quantic Dream, 2018).
//
// Sprint 1: CPU-side cluster math + AABB-vs-sphere assignment.
// Sprint 2 plan: GPU compute path + light culling shader.
//
// Naming discipline: this library is distinct from cd::render::cluster
//   (ClusterGrid, wave-84 header-only). That library uses a view-space
//   angular/depth API. This library exposes a view-proj matrix API
//   (Clusterer::assign) matched to a GPU-ready froxel layout
//   (ClusterGrid x/y/z_slices + near/far, PointLight with world-space
//   position + radius, LightAssignment with parallel offset/index arrays).
//
// Conventions:
//   * view_proj_matrix: column-major float[16], maps world → clip space.
//   * Cluster index: linear = (z_slice * y_tiles + ty) * x_tiles + tx.
//   * LightAssignment::lights_in_cluster() requires the LightAssignment
//     returned by the same assign() call.
// =============================================================================
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace cd::render::lighting_clusters
{

// ---------------------------------------------------------------------------
// Data structures
// ---------------------------------------------------------------------------

/// 3-D froxel grid configuration.
struct ClusterGrid
{
    std::uint32_t x_tiles   { 16 };    ///< Horizontal tile count.
    std::uint32_t y_tiles   { 9  };    ///< Vertical tile count.
    std::uint32_t z_slices  { 24 };    ///< Depth-slice count (log-z partitioned).
    float         near_plane{ 0.1F };  ///< View-space near depth (metres, positive).
    float         far_plane { 1000.F };///< View-space far  depth (metres, positive).
};

/// Point light described in world space.
struct PointLight
{
    std::array<float, 3> position  { 0.F, 0.F, 0.F }; ///< World-space centre.
    float                radius    { 1.F };             ///< Influence sphere radius.
    std::array<float, 3> color     { 1.F, 1.F, 1.F }; ///< Linear RGB.
    float                intensity { 1.F };             ///< Luminous intensity (cd).
};

/// Result of a single assign() call.
/// Parallel arrays:  light_indices_per_cluster / offset_per_cluster.
/// For cluster k:  light indices = light_indices_per_cluster[
///                      offset_per_cluster[k] .. offset_per_cluster[k+1] ).
struct LightAssignment
{
    std::vector<std::uint32_t> light_indices_per_cluster; ///< Packed light index stream.
    std::vector<std::uint32_t> offset_per_cluster;        ///< prefix-sum offsets, size = total_clusters + 1.
};

// ---------------------------------------------------------------------------
// Clusterer
// ---------------------------------------------------------------------------

/// CPU-side clustered light assignment for Forward+ rendering.
///
/// Usage:
///   Clusterer clusterer;
///   clusterer.configure({ 16, 9, 24, 0.1F, 1000.F });
///   auto assignment = clusterer.assign(point_lights, view_proj);
///   auto lit = clusterer.lights_in_cluster(idx, assignment);
class Clusterer
{
public:
    Clusterer() noexcept = default;

    /// Replace the active grid configuration. Invalidates all prior assignments.
    void configure(const ClusterGrid& grid) noexcept;

    /// Assign each light to the clusters whose AABB overlaps the light sphere.
    ///
    /// @param lights         Span of PointLight in world space.
    /// @param view_proj      Column-major 4×4 matrix (world → clip).
    /// @return               LightAssignment with populated parallel arrays.
    [[nodiscard]] LightAssignment assign(
        std::span<const PointLight>      lights,
        const std::array<float, 16>&     view_proj_matrix) const;

    /// Linear cluster index for tile (tx, ty, z_slice).
    [[nodiscard]] std::uint32_t cluster_index(
        std::uint32_t tx,
        std::uint32_t ty,
        std::uint32_t z_slice) const noexcept;

    /// Slice of light indices for one cluster from an assignment.
    [[nodiscard]] std::span<const std::uint32_t> lights_in_cluster(
        std::uint32_t           idx,
        const LightAssignment&  assignment) const noexcept;

    [[nodiscard]] const ClusterGrid& grid() const noexcept { return grid_; }

    [[nodiscard]] std::uint32_t total_cluster_count() const noexcept
    {
        return grid_.x_tiles * grid_.y_tiles * grid_.z_slices;
    }

private:
    ClusterGrid grid_;
};

}  // namespace cd::render::lighting_clusters
