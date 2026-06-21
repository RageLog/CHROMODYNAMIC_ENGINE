// =============================================================================
// CHROMODYNAMIC — cd/asset/scene_streamer/SpatialStreamingPolicy.cpp
// Phase 1268 — Spatial cell/tile streaming policy implementation.
// =============================================================================

#include <cd/asset/scene_streamer/SpatialStreamingPolicy.hpp>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <limits>

namespace cd::asset::scene_streamer
{

// ---------------------------------------------------------------------------
// CellGrid
// ---------------------------------------------------------------------------

CellGrid::CellGrid(const float cell_size) noexcept
    : cell_size_ { cell_size }
{
    assert(cell_size > 0.0F && "CellGrid: cell_size must be positive");
}

// Floor division that gives correct results for negative values.
// Example: floor_div(-0.5, 1.0) = -1  (NOT 0 as truncation would give).
std::int32_t CellGrid::floor_div(const float pos, const float size) noexcept
{
    const float ratio = pos / size;
    // Use std::floor then clamp to int32 range.
    const float floored = std::floor(ratio);
    constexpr auto kMin = static_cast<float>(std::numeric_limits<std::int32_t>::min());
    constexpr auto kMax = static_cast<float>(std::numeric_limits<std::int32_t>::max());
    if (floored <= kMin) { return std::numeric_limits<std::int32_t>::min(); }
    if (floored >= kMax) { return std::numeric_limits<std::int32_t>::max(); }
    return static_cast<std::int32_t>(floored);
}

CellCoord CellGrid::world_to_cell(const float wx, const float wy, const float wz) const noexcept
{
    return CellCoord {
        floor_div(wx, cell_size_),
        floor_div(wy, cell_size_),
        floor_div(wz, cell_size_)
    };
}

std::array<float, 3>
CellGrid::cell_to_world_centre(const CellCoord& c) const noexcept
{
    return {
        (static_cast<float>(c.x) + 0.5F) * cell_size_,
        (static_cast<float>(c.y) + 0.5F) * cell_size_,
        (static_cast<float>(c.z) + 0.5F) * cell_size_
    };
}

// ---------------------------------------------------------------------------
// SpatialStreamingPolicy — construction
// ---------------------------------------------------------------------------

SpatialStreamingPolicy::SpatialStreamingPolicy(
    SpatialStreamingConfig cfg,
    CellPathFn             cell_path_fn)
    : config_       { cfg }
    , cell_path_fn_ { std::move(cell_path_fn) }
    , grid_         { cfg.cell_size }
{
    // Clamp unload_radius to be at least load_radius (hysteresis invariant).
    config_.unload_radius_cells =
        std::max(config_.unload_radius_cells, config_.load_radius_cells);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

std::uint32_t
SpatialStreamingPolicy::chebyshev_dist(const CellCoord& a, const CellCoord& b) noexcept
{
    // |a.x - b.x| etc. computed via uint32 abs to avoid signed overflow.
    const auto dx = static_cast<std::uint32_t>(
        std::abs(static_cast<std::int64_t>(a.x) - static_cast<std::int64_t>(b.x)));
    const auto dy = static_cast<std::uint32_t>(
        std::abs(static_cast<std::int64_t>(a.y) - static_cast<std::int64_t>(b.y)));
    const auto dz = static_cast<std::uint32_t>(
        std::abs(static_cast<std::int64_t>(a.z) - static_cast<std::int64_t>(b.z)));
    return std::max({ dx, dy, dz });
}

std::uint8_t
SpatialStreamingPolicy::distance_to_priority(const std::uint32_t dist) const noexcept
{
    // Distance 0 → 255 (highest priority).
    // Distance load_radius_cells → 1 (lowest enqueue priority).
    // Distance > load_radius_cells → 0 (not enqueued; caller guards this).
    const std::uint32_t r = config_.load_radius_cells;
    if (dist == 0U) { return 255U; }
    if (r == 0U)    { return 255U; }
    // Linear: priority = 255 - floor(254 * dist / r), clamped to [1,254].
    const std::uint32_t scaled = (254U * dist) / r;
    const std::uint32_t raw    = (scaled >= 254U) ? 1U : (255U - scaled);
    return static_cast<std::uint8_t>(raw < 1U ? 1U : raw);
}

// ---------------------------------------------------------------------------
// update — core spatial streaming logic
// ---------------------------------------------------------------------------

void SpatialStreamingPolicy::update(
    const float     cam_x,
    const float     cam_y,
    const float     cam_z,
    SceneStreamer&  streamer)
{
    const CellCoord camera_cell = grid_.world_to_cell(cam_x, cam_y, cam_z);
    last_camera_cell_           = camera_cell;

    const auto lr = static_cast<std::int32_t>(config_.load_radius_cells);

    // ---- Step 1: collect all cells in the load cube + sort by distance ------
    // Chebyshev radius lr means all cells with |dx|,|dy|,|dz| <= lr.
    // (2*lr+1)^3 cells in 3D; radius 2 → 125, radius 3 → 343. Manageable.

    last_prefetch_order_.clear();
    const std::size_t span = 2U * static_cast<std::size_t>(lr) + 1U;
    last_prefetch_order_.reserve(span * span * span);

    for (std::int32_t dz = -lr; dz <= lr; ++dz)
    {
        for (std::int32_t dy = -lr; dy <= lr; ++dy)
        {
            for (std::int32_t dx = -lr; dx <= lr; ++dx)
            {
                last_prefetch_order_.push_back(CellCoord {
                    camera_cell.x + dx,
                    camera_cell.y + dy,
                    camera_cell.z + dz });
            }
        }
    }

    // Sort ascending by Chebyshev distance (closest first = highest priority
    // enqueued last so streamer picks it first from the max-heap).
    std::ranges::sort(last_prefetch_order_, [&](const CellCoord& a, const CellCoord& b) {
        return chebyshev_dist(a, camera_cell) < chebyshev_dist(b, camera_cell);
    });

    // ---- Step 2: enqueue cells in load radius that are kUnloaded -----------
    for (const CellCoord& c : last_prefetch_order_)
    {
        const std::uint32_t dist = chebyshev_dist(c, camera_cell);
        // Guard: the sorted list is already <= lr but double-check via dist.
        if (dist > config_.load_radius_cells) { break; }

        auto it = cells_.find(c);
        if (it != cells_.end())
        {
            // Already tracked — promote kRequested → kLoaded if streamer done.
            if (it->second == CellState::kRequested)
            {
                const std::string path = cell_path_fn_(c);
                if (!path.empty() && streamer.is_loaded(path))
                {
                    it->second = CellState::kLoaded;
                }
            }
            continue;
        }

        // Not tracked → try to request.
        const std::string path = cell_path_fn_(c);
        if (path.empty()) { continue; }

        streamer.enqueue(StreamRequest {
            path,
            distance_to_priority(dist) });

        cells_.emplace(c, CellState::kRequested);
    }

    // ---- Step 3: promote any remaining kRequested → kLoaded (cells already
    //     in the load radius that were requested in prior frames) --------------
    for (auto& [coord, state] : cells_)
    {
        if (state != CellState::kRequested) { continue; }
        const std::uint32_t dist = chebyshev_dist(coord, camera_cell);
        if (dist > config_.load_radius_cells) { continue; }
        const std::string path = cell_path_fn_(coord);
        if (!path.empty() && streamer.is_loaded(path))
        {
            state = CellState::kLoaded;
        }
    }

    // ---- Step 4: unload cells beyond unload_radius --------------------------
    // Collect keys to erase (cannot erase while iterating).
    std::vector<CellCoord> to_remove;
    for (const auto& [coord, state] : cells_)
    {
        const std::uint32_t dist = chebyshev_dist(coord, camera_cell);
        if (dist > config_.unload_radius_cells)
        {
            to_remove.push_back(coord);
        }
    }

    for (const CellCoord& coord : to_remove)
    {
        const auto it = cells_.find(coord);
        if (it == cells_.end()) { continue; }

        if (it->second == CellState::kRequested)
        {
            // Cancel in-flight request.
            const std::string path = cell_path_fn_(coord);
            if (!path.empty())
            {
                streamer.cancel(path);
            }
        }
        // kLoaded: ECS eviction is caller's job (SEALED — see ADR note).
        cells_.erase(it);
    }
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

CellState SpatialStreamingPolicy::cell_state(const CellCoord& c) const noexcept
{
    const auto it = cells_.find(c);
    if (it == cells_.cend()) { return CellState::kUnloaded; }
    return it->second;
}

std::size_t SpatialStreamingPolicy::requested_count() const noexcept
{
    std::size_t n = 0U;
    for (const auto& [coord, state] : cells_)
    {
        if (state == CellState::kRequested) { ++n; }
    }
    return n;
}

std::size_t SpatialStreamingPolicy::loaded_count() const noexcept
{
    std::size_t n = 0U;
    for (const auto& [coord, state] : cells_)
    {
        if (state == CellState::kLoaded) { ++n; }
    }
    return n;
}

std::size_t SpatialStreamingPolicy::tracked_count() const noexcept
{
    return cells_.size();
}

}  // namespace cd::asset::scene_streamer
