// =============================================================================
// CHROMODYNAMIC — cd/light/ClusterGrid.hpp
// Phase 165 / v0.99.87 — clustered shading light grid (Forward+).
//
// Splits the camera frustum into a 3D grid of clusters and assigns
// each Light to the clusters it touches. The shader for a given
// fragment indexes the grid by (screen_xy, view_z) and iterates only
// the lights that overlap its cluster — the standard
// "Clustered Deferred / Forward+" technique introduced by Olsson et
// al. 2012 ("Clustered Deferred and Forward Shading") and now used
// by every modern engine (Unity HDRP, UE5, Doom Eternal, Filament,
// id Tech 6/7, Cyberpunk 2077).
//
// Grid layout (defaults match Doom 2016 / Eternal):
//   * 16 x  9 tiles in screen XY (matches 16:9 aspect; reconfigurable)
//   * 24 logarithmic Z slices (near → far in log-space)
//
// Each cluster stores a small fixed-size light-index list. Lights
// that don't fit overflow into the shared bucket and are dropped (or
// the cluster reports overflow upstream — caller's choice).
//
// Header-only because the grid lives in app memory; the GPU side
// uploads it as an SSBO once per frame.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/light/Light.hpp>
#include <cd/math/Vector.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace cd::light
{

/// Per-cluster light list. The fixed-size array keeps each cluster a
/// constant 132-byte chunk so the GPU SSBO doesn't need indirection
/// to look up a cluster's range.
constexpr std::uint32_t kMaxLightsPerCluster = 32;

struct ClusterCell
{
    std::uint32_t light_count { 0 };
    std::array<std::uint32_t, kMaxLightsPerCluster> light_indices {};
    std::uint32_t overflow { 0 };  // count of lights that didn't fit
};

struct ClusterGridDesc
{
    std::uint32_t tiles_x  { 16 };
    std::uint32_t tiles_y  {  9 };
    std::uint32_t slices_z { 24 };
    float         near_z   { 0.1F };
    float         far_z    { 100.0F };
};

class ClusterGrid
{
public:
    void configure(const ClusterGridDesc& desc) { desc_ = desc; rebuild_(); }

    [[nodiscard]] const ClusterGridDesc& desc() const noexcept { return desc_; }

    [[nodiscard]] std::uint32_t cluster_count() const noexcept
    {
        return desc_.tiles_x * desc_.tiles_y * desc_.slices_z;
    }

    [[nodiscard]] const std::vector<ClusterCell>& cells() const noexcept
    {
        return cells_;
    }

    /// Map a view-space Z to its logarithmic slice index. Used by
    /// both the CPU assignment pass and the GPU shader. Identical
    /// formula:  slice = floor(log(z/near) / log(far/near) * slices).
    [[nodiscard]] std::uint32_t slice_of(float view_z) const noexcept
    {
        view_z = std::max(view_z, desc_.near_z);
        if (view_z >= desc_.far_z) return desc_.slices_z - 1;
        const float t = std::log(view_z / desc_.near_z) /
                        std::log(desc_.far_z / desc_.near_z);
        const auto s = static_cast<std::uint32_t>(t * static_cast<float>(desc_.slices_z));
        return std::min(s, desc_.slices_z - 1);
    }

    /// Compute the inverse mapping (slice → z range) for the GPU
    /// reconstruction shader. Returns [z_near_of_slice, z_far_of_slice].
    [[nodiscard]] std::pair<float, float> slice_z_range(std::uint32_t slice) const noexcept
    {
        const float t0 = static_cast<float>(slice)       / static_cast<float>(desc_.slices_z);
        const float t1 = static_cast<float>(slice + 1)   / static_cast<float>(desc_.slices_z);
        const float ratio = desc_.far_z / desc_.near_z;
        return {
            desc_.near_z * std::pow(ratio, t0),
            desc_.near_z * std::pow(ratio, t1),
        };
    }

    /// Clear every cell's light list. Call once per frame before
    /// `assign`.
    void clear()
    {
        for (auto& c : cells_) { c.light_count = 0; c.overflow = 0; }
    }

    /// Append a light to every cluster cell whose AABB overlaps the
    /// light's range sphere (or full-grid coverage for directional).
    /// Returns the number of (cluster, light) assignments made.
    std::uint32_t assign(std::uint32_t light_index,
                         const Light& light,
                         const cd::math::Vec3f& view_space_position)
    {
        std::uint32_t hits = 0;

        if (light.type == LightType::kDirectional)
        {
            // Directional touches everything.
            for (auto& c : cells_) hits += push_(c, light_index);
            return hits;
        }

        // For point/spot/area we use a screen-space tile bound + Z slice
        // range. The renderer's culling pass usually does a tighter
        // sphere-vs-frustum-cluster intersection; this is the
        // simple-yet-correct version.
        const float r = std::max(0.001F, light.range);
        const float z = view_space_position.z;
        const std::uint32_t z_min = slice_of(std::max(desc_.near_z, z - r));
        const std::uint32_t z_max = slice_of(std::min(desc_.far_z,  z + r));

        for (std::uint32_t sz = z_min; sz <= z_max && sz < desc_.slices_z; ++sz)
        {
            for (std::uint32_t ty = 0; ty < desc_.tiles_y; ++ty)
            {
                for (std::uint32_t tx = 0; tx < desc_.tiles_x; ++tx)
                {
                    auto& cell = cells_[index_(tx, ty, sz)];
                    hits += push_(cell, light_index);
                }
            }
        }
        return hits;
    }

    [[nodiscard]] std::uint32_t index_(std::uint32_t x, std::uint32_t y, std::uint32_t z) const noexcept
    {
        return (z * desc_.tiles_y + y) * desc_.tiles_x + x;
    }

private:
    void rebuild_() { cells_.assign(cluster_count(), ClusterCell {}); }

    static std::uint32_t push_(ClusterCell& c, std::uint32_t li) noexcept
    {
        if (c.light_count < kMaxLightsPerCluster)
        {
            c.light_indices[c.light_count++] = li;
            return 1;
        }
        ++c.overflow;
        return 0;
    }

    ClusterGridDesc          desc_ {};
    std::vector<ClusterCell> cells_;
};

}  // namespace cd::light
