// =============================================================================
// CHROMODYNAMIC — cd/asset/scene_streamer/SpatialStreamingPolicy.hpp
// Phase 1268 — Spatial cell/tile streaming policy for cd::asset::scene_streamer
//
// Converts a camera world position into streaming load/unload decisions over a
// uniform 3D cell grid. A cell is identified by a signed integer (cx,cy,cz)
// triple. Each cell carries one scene-chunk asset path; the policy issues
// enqueue() / cancel() calls against a SceneStreamer instance when cells enter
// or leave the load / unload radius.
//
// ---- Hysteresis ---
// Two radii prevent flickering when the camera sits on a cell boundary:
//   load_radius  — cells whose Chebyshev cell-distance <= load_radius_cells
//                  are requested (enqueue).
//   unload_radius — cells whose Chebyshev cell-distance > unload_radius_cells
//                  are released (cancel). Must be >= load_radius_cells.
//
// ---- Prefetch order ---
// Cells are sorted by Chebyshev distance before enqueue so that the closest
// cell (highest urgency) gets the highest priority value fed to StreamRequest.
//
// ---- Cell addressing ---
// World position → cell:  coord = floor(pos / cell_size)  (integer division)
// Cell centre → world:    centre = (coord + 0.5) * cell_size
//
// ---- Thread safety ---
// SpatialStreamingPolicy is NOT thread-safe. update() must be called from the
// owner thread — the same thread that calls SceneStreamer::tick().
//
// ---- Formal seal ---
// The asset path resolver callback (cell_path_fn) is caller-supplied. The
// actual on-disk I/O and ECS ingest happen inside SceneStreamer and the
// render-side consumer respectively; this class contains only CPU policy math.
// =============================================================================
#pragma once

#include <cd/asset/scene_streamer/SceneStreamer.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace cd::asset::scene_streamer
{

// ---- CellCoord ---------------------------------------------------------------

/// Signed 3D integer cell coordinate.
struct CellCoord
{
    std::int32_t x { 0 };
    std::int32_t y { 0 };
    std::int32_t z { 0 };

    [[nodiscard]] bool operator==(const CellCoord&) const noexcept = default;
};

}  // namespace cd::asset::scene_streamer

// ---- std::hash<CellCoord> specialisation ------------------------------------
namespace std
{
template <>
struct hash<cd::asset::scene_streamer::CellCoord>
{
    [[nodiscard]] std::size_t
    operator()(const cd::asset::scene_streamer::CellCoord& c) const noexcept
    {
        // FNV-1a style combine over three 32-bit ints.
        // Cast to uint32_t first to keep arithmetic unsigned.
        const auto ux = static_cast<std::uint32_t>(c.x);
        const auto uy = static_cast<std::uint32_t>(c.y);
        const auto uz = static_cast<std::uint32_t>(c.z);
        std::size_t h = 0x811C'9DC5ULL;
        h ^= static_cast<std::size_t>(ux);
        h *= 0x0100'0193ULL;
        h ^= static_cast<std::size_t>(uy);
        h *= 0x0100'0193ULL;
        h ^= static_cast<std::size_t>(uz);
        h *= 0x0100'0193ULL;
        return h;
    }
};
}  // namespace std

namespace cd::asset::scene_streamer
{

// ---- CellGrid ----------------------------------------------------------------

/// Converts between world positions (float, metres) and integer cell coords.
///
/// All three axes share the same `cell_size`. Negative positions floor
/// correctly because integer division towards zero is adjusted:
///   world_to_cell(-0.5, 1.0) → -1  (floor, not truncate)
class CellGrid
{
public:
    /// `cell_size` must be positive. Asserted at construction.
    explicit CellGrid(float cell_size) noexcept;

    /// World position → cell coordinate (floor division per axis).
    [[nodiscard]] CellCoord world_to_cell(float wx, float wy, float wz) const noexcept;

    /// Cell coordinate → world-space centre of that cell.
    [[nodiscard]] std::array<float, 3>
    cell_to_world_centre(const CellCoord& c) const noexcept;

    [[nodiscard]] float cell_size() const noexcept { return cell_size_; }

private:
    float cell_size_ { 1.0F };

    /// Floor-division for one axis (correct for negative values).
    [[nodiscard]] static std::int32_t
    floor_div(float pos, float size) noexcept;
};

// ---- SpatialStreamingConfig --------------------------------------------------

/// Configuration for SpatialStreamingPolicy.
///
///   cell_size          — side length of each cubic cell (world units, > 0).
///   load_radius_cells  — Chebyshev distance at which cells are loaded (>= 0).
///   unload_radius_cells — Chebyshev distance at which cells are released
///                         (>= load_radius_cells for hysteresis). 0 = no unload
///                         hysteresis (immediate release on leaving load radius).
struct SpatialStreamingConfig
{
    float         cell_size           { 100.0F };
    std::uint32_t load_radius_cells   {   2U };
    std::uint32_t unload_radius_cells {   3U };
};

// ---- CellState ---------------------------------------------------------------

/// Internal per-cell residency state.
enum class CellState : std::uint8_t
{
    kUnloaded  = 0,  ///< Not in memory, not requested.
    kRequested = 1,  ///< enqueue() issued; waiting for SceneStreamer.
    kLoaded    = 2,  ///< SceneStreamer reports is_loaded() == true.
};

// ---- SpatialStreamingPolicy --------------------------------------------------

/// CPU streaming policy that drives a SceneStreamer based on camera position.
///
/// Typical frame loop:
/// @code
///   policy.update(cam_x, cam_y, cam_z, scene_streamer);
///   scene_streamer.tick(dt);
/// @endcode
///
/// Callback `cell_path_fn` maps a CellCoord to the asset path for that cell's
/// scene chunk. If it returns an empty string the cell is skipped (no request).
///
/// Priority encoding: closer cells (smaller Chebyshev distance) receive a
/// higher StreamRequest::priority value (linear mapping, 1 → 255, 0 → stays at
/// load_radius_cells+1 mapped range). At Chebyshev distance 0 priority=255.
class SpatialStreamingPolicy
{
public:
    using CellPathFn = std::function<std::string(const CellCoord&)>;

    /// Construct with configuration + cell-to-path resolver.
    /// `cfg.unload_radius_cells` is clamped to be >= `cfg.load_radius_cells`.
    explicit SpatialStreamingPolicy(SpatialStreamingConfig cfg, CellPathFn cell_path_fn);

    // Non-copyable; non-movable.
    SpatialStreamingPolicy(const SpatialStreamingPolicy&)            = delete;
    SpatialStreamingPolicy& operator=(const SpatialStreamingPolicy&) = delete;
    SpatialStreamingPolicy(SpatialStreamingPolicy&&)                 = delete;
    SpatialStreamingPolicy& operator=(SpatialStreamingPolicy&&)      = delete;

    /// Drive the policy for one frame.
    ///
    /// 1. Maps (cam_x, cam_y, cam_z) to the camera cell.
    /// 2. Identifies all cells within load_radius_cells (Chebyshev).
    ///    — Cells that are kUnloaded → enqueued, state → kRequested.
    ///    — Cells that are kRequested → promote to kLoaded if streamer
    ///      reports is_loaded() (poll only; no blocking).
    /// 3. Identifies all resident cells beyond unload_radius_cells.
    ///    — kRequested cells → cancel() + state → kUnloaded.
    ///    — kLoaded cells    → state → kUnloaded (ECS eviction is
    ///      caller's job — SEALED, see ADR note in PROJECT_COMPLETION_STATUS).
    /// 4. Cells are sorted by ascending Chebyshev distance before enqueue
    ///    so that the streamer's priority queue serves closest first.
    void update(float cam_x, float cam_y, float cam_z, SceneStreamer& streamer);

    // ---- Queries -------------------------------------------------------------

    /// Cell-coord → residency state. Returns kUnloaded for unknown cells.
    [[nodiscard]] CellState cell_state(const CellCoord& c) const noexcept;

    /// Number of cells currently in kRequested state.
    [[nodiscard]] std::size_t requested_count() const noexcept;

    /// Number of cells currently in kLoaded state.
    [[nodiscard]] std::size_t loaded_count() const noexcept;

    /// Total number of cells tracked (kRequested + kLoaded).
    [[nodiscard]] std::size_t tracked_count() const noexcept;

    /// The grid helper (for tests / debug).
    [[nodiscard]] const CellGrid& grid() const noexcept { return grid_; }

    /// Configuration (immutable after construction).
    [[nodiscard]] const SpatialStreamingConfig& config() const noexcept
    {
        return config_;
    }

    /// Camera cell from the most recent update() call.
    /// Returns {0,0,0} before the first update().
    [[nodiscard]] CellCoord last_camera_cell() const noexcept
    {
        return last_camera_cell_;
    }

    /// Sorted prefetch order from the most recent update() (ascending
    /// Chebyshev distance → closest cell is element [0]).
    [[nodiscard]] const std::vector<CellCoord>&
    last_prefetch_order() const noexcept
    {
        return last_prefetch_order_;
    }

private:
    // ---- Helpers -------------------------------------------------------------

    /// Chebyshev distance between two cell coordinates (L∞ norm).
    [[nodiscard]] static std::uint32_t
    chebyshev_dist(const CellCoord& a, const CellCoord& b) noexcept;

    /// Encode a Chebyshev distance into a StreamRequest priority (closer =
    /// higher priority). Distance 0 → 255, distance load_radius → 1.
    [[nodiscard]] std::uint8_t
    distance_to_priority(std::uint32_t dist) const noexcept;

    // ---- State ---------------------------------------------------------------

    SpatialStreamingConfig config_;
    CellPathFn             cell_path_fn_;
    CellGrid               grid_;

    // Cell residency table. Only tracks kRequested / kLoaded cells.
    std::unordered_map<CellCoord, CellState> cells_;

    CellCoord             last_camera_cell_ {};
    std::vector<CellCoord> last_prefetch_order_;
};

}  // namespace cd::asset::scene_streamer
