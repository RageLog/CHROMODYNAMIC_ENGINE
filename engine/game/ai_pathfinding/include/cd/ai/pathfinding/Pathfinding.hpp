// =============================================================================
// CHROMODYNAMIC — cd/ai/pathfinding/Pathfinding.hpp
// Phase 671 — cd::ai::pathfinding (Sprint-1: in-memory navmesh + A*)
//
// 2D/3D pathfinding on a triangulated nav-mesh using the A* algorithm.
//
// Design references:
//   * Millington & Funge, "Artificial Intelligence for Games", 2nd ed.
//     (CRC Press 2009) — Ch. 4 (pathfinding), §4.3 A*.
//   * Hart, Nilsson & Raphael, "A Formal Basis for the Heuristic
//     Determination of Minimum Cost Paths", IEEE TSSC 1968 — foundational
//     A* theorem with admissible/consistent heuristic requirements.
//   * Snook, "Simplified 3D Movement and Pathfinding Using Navigation
//     Meshes", Game Programming Gems 1 (2000) — triangle-centre graph +
//     funnel smoothing as used by Unreal / Unity NavMesh agents.
//
// Sprint-1 scope: in-memory NavMesh + A* traversal over triangle-centre
// graph. Triangulation pipeline (Constrained Delaunay / Recast) is a
// Sprint-2 concern and is intentionally out of scope here.
//
// Namespace: cd::ai::pathfinding
//
// Dependencies (CLAUDE.md §7): cd::core only — stdlib containers + math.
// No ECS, render, or physics headers are pulled in.
//
// Moment: A game designer drops a navmesh into the scene, NPCs walk around
// obstacles to reach the player using actual A* — no scripted move-to-point
// hacks.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace cd::ai::pathfinding
{

// =============================================================================
// Data types
// =============================================================================

/// A single triangle in the navigation mesh.
/// `vertex_indices[0..2]` index into `NavMesh::vertices`.
/// `mesh_id` lets callers partition the mesh into logical sub-regions (rooms,
/// floors, etc.) for culling or area-cost overrides without splitting the mesh.
struct NavTriangle
{
    std::array<uint32_t, 3> vertex_indices {};
    uint32_t                mesh_id { 0 };
};

/// Navigation mesh: a set of vertices and triangles with explicit neighbour
/// adjacency (pre-computed, edge-shared).
///
/// Layout contract:
///   `vertices[i]`           — 3-D position (x, y, z) in world space.
///   `triangles[i]`          — NavTriangle referencing three vertex indices.
///   `triangle_neighbors[i]` — For triangle i, the indices of the adjacent
///                             triangle sharing each edge:
///                               [0] = edge v0-v1
///                               [1] = edge v1-v2
///                               [2] = edge v2-v0
///                             UINT32_MAX (kNoNeighbor) denotes a boundary edge
///                             (no adjacent triangle).
///
/// Invariants (checked at load time):
///   * `triangles.size() == triangle_neighbors.size()`
///   * All vertex indices < `vertices.size()`
///   * Neighbour indices < `triangles.size()` or == kNoNeighbor
struct NavMesh
{
    static constexpr uint32_t kNoNeighbor = std::numeric_limits<uint32_t>::max();

    std::vector<std::array<float, 3>>    vertices         {};
    std::vector<NavTriangle>              triangles        {};
    std::vector<std::array<uint32_t, 3>> triangle_neighbors {};
};

// =============================================================================
// Query types
// =============================================================================

/// Input to `Pathfinder::find_path`.
struct PathRequest
{
    std::array<float, 3> start               {};
    std::array<float, 3> goal                {};
    /// Maximum A* search radius in world-space units.  The search is pruned
    /// once the heuristic estimate from the open node to the goal exceeds
    /// this value.  Use a very large number (e.g. FLT_MAX) to search the
    /// full reachable mesh.
    float                max_search_distance { std::numeric_limits<float>::max() };
};

/// Output from `Pathfinder::find_path`.
struct PathResult
{
    /// Ordered waypoints from start to goal (world-space positions).
    /// The first element is the start triangle centre and the last is the
    /// goal position.  Empty when `success == false`.
    std::vector<std::array<float, 3>> waypoints         {};
    bool                              success           { false };
    /// Sum of Euclidean segment lengths along the returned waypoint path.
    float                             total_distance    { 0.0F };
    /// Number of triangles popped from the A* open set (search effort).
    uint32_t                          triangles_explored { 0U };
};

// =============================================================================
// Pathfinder
// =============================================================================

/// A* pathfinder operating on an in-memory NavMesh.
///
/// Usage:
/// ```
///   cd::ai::pathfinding::Pathfinder pf;
///   pf.load_navmesh(my_mesh);
///
///   PathRequest req;
///   req.start = {0.f, 0.f, 0.f};
///   req.goal  = {10.f, 0.f, 5.f};
///   req.max_search_distance = 100.f;
///
///   PathResult res = pf.find_path(req);
///   if (res.success) { /* walk along res.waypoints */ }
/// ```
///
/// Thread safety: `find_path` and `nearest_triangle` are `const` and safe
/// to call from multiple threads concurrently once the mesh is loaded.
/// `load_navmesh` is NOT thread-safe; call it once during scene setup.
class Pathfinder
{
public:
    Pathfinder() = default;

    // Non-copyable (mesh can be large; use shared_ptr<Pathfinder> if sharing).
    Pathfinder(const Pathfinder&)            = delete;
    Pathfinder& operator=(const Pathfinder&) = delete;
    Pathfinder(Pathfinder&&)                 = default;
    Pathfinder& operator=(Pathfinder&&)      = default;

    ~Pathfinder() = default;

    // -------------------------------------------------------------------------
    // Mesh management
    // -------------------------------------------------------------------------

    /// Load (or replace) the navigation mesh.  Validates invariants and
    /// pre-computes per-triangle centroid positions used by the A* heuristic.
    /// Throws `std::invalid_argument` if the mesh violates structural
    /// invariants (mismatched sizes, out-of-range indices).
    void load_navmesh(const NavMesh& mesh);

    /// True once a non-empty, valid mesh has been loaded.
    [[nodiscard]] bool has_navmesh() const noexcept;

    // -------------------------------------------------------------------------
    // Pathfinding
    // -------------------------------------------------------------------------

    /// Run A* from `req.start` to `req.goal`.
    ///
    /// Algorithm:
    ///   1. Locate the nearest triangle to `start` and `goal` via a linear
    ///      centroid-distance scan (O(N); Sprint-2 will replace with a spatial
    ///      hash / BVH accelerator).
    ///   2. Run A* on the dual graph: each triangle is a node; edges are
    ///      the non-boundary neighbours; edge cost = Euclidean centroid
    ///      distance; heuristic = Euclidean distance from current centroid to
    ///      goal centroid (admissible + consistent).
    ///   3. Reconstruct the triangle chain and emit centroid waypoints plus
    ///      the exact goal position.
    ///
    /// If `start` or `goal` cannot be associated with any triangle, or if no
    /// path exists within `max_search_distance`, returns a result with
    /// `success == false` and an empty waypoints list.
    [[nodiscard]] PathResult find_path(const PathRequest& req) const;

    /// Return the index of the triangle whose centroid is nearest to `point`.
    /// Returns `std::nullopt` if no mesh is loaded.
    [[nodiscard]] std::optional<uint32_t> nearest_triangle(std::array<float, 3> point) const;

private:
    // -------------------------------------------------------------------------
    // Internal helpers
    // -------------------------------------------------------------------------

    /// Euclidean distance between two 3-D points.
    [[nodiscard]] static float dist3(const std::array<float, 3>& a,
                                     const std::array<float, 3>& b) noexcept;

    /// Centroid of triangle `tri_idx`.
    [[nodiscard]] std::array<float, 3> centroid(uint32_t tri_idx) const noexcept;

    // -------------------------------------------------------------------------
    // State
    // -------------------------------------------------------------------------

    NavMesh                           mesh_      {};
    std::vector<std::array<float, 3>> centroids_ {};  ///< Pre-computed per-triangle centroids.
};

}  // namespace cd::ai::pathfinding
