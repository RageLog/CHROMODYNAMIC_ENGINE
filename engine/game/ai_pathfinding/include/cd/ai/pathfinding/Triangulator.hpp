// =============================================================================
// CHROMODYNAMIC — cd/ai/pathfinding/Triangulator.hpp
// Phase 761 — cd::ai::pathfinding (Sprint-2: mesh -> NavMesh pipeline)
//
// Build a NavMesh from a raw triangulated 3D source mesh (typically a
// cd::asset::gltf-loaded "ground" surface) by:
//   1. Filtering walkable triangles by slope angle vs. world up (+Y).
//   2. Filtering by agent radius (drop triangles too small to fit the agent).
//   3. Re-keying surviving vertices into a compact NavMesh vertex buffer.
//   4. Building per-triangle neighbour adjacency via shared undirected edges.
//
// Sprint-2 keeps the algorithm intentionally simple: NO voxelisation,
// NO morphological erosion (Recast-style). The "voxelize / re-extract"
// stage of a full Recast pipeline is approximated by:
//   * walkable-slope filter on triangle normal,
//   * agent-radius filter on triangle min-altitude (smallest perpendicular
//     distance from any vertex to its opposite edge),
// which is sufficient to reject narrow corridors and steep walls for
// designer-friendly glTF level geometry. A follow-up sprint will lift this
// to full heightfield voxelisation when production levels demand it.
//
// References:
//   * Mononen, "Recast & Detour: Navigation Mesh Toolset",
//     https://github.com/recastnavigation/recastnavigation — voxel-based
//     navmesh pipeline. We adopt the slope + radius filter semantics
//     without the voxel grid (Sprint-2 scope; full rasterisation pending).
//   * Snook, "Simplified 3D Movement and Pathfinding Using Navigation
//     Meshes", Game Programming Gems 1 (2000) — triangle-centre dual graph
//     + edge-shared neighbour adjacency (used by load_navmesh consumer).
//   * Millington & Funge, "AI for Games", 2nd ed. (CRC Press 2009),
//     Ch. 4 — pathfinding pre-processing.
//
// Namespace: cd::ai::pathfinding
// Dependencies (CLAUDE.md §7): cd::core only — stdlib + std::span.
//
// Moment: A designer drops a glTF level mesh into the editor, slope and
// agent-radius are configured once in the project settings, and a navmesh
// auto-generates that NPCs walk via A* — no Recast/Detour, no separate
// authoring tool.
// =============================================================================
#pragma once

#include <cd/ai/pathfinding/Pathfinding.hpp>
#include <cd/core/Defines.hpp>

#include <array>
#include <cstdint>
#include <span>

namespace cd::ai::pathfinding
{

// =============================================================================
// TriangulationConfig
// =============================================================================

/// Configuration for the mesh -> NavMesh pipeline.
///
/// All angles are radians; all distances are world-space units consistent
/// with the source mesh vertices.
struct TriangulationConfig
{
    /// Maximum walkable slope angle, measured from world +Y up.
    /// A triangle is "walkable" when its upward-facing normal makes an angle
    /// with +Y at most equal to this value. Default 45 degrees (pi/4).
    ///
    /// Equivalent comparison: a triangle is walkable iff
    ///   abs(normal.y) >= cos(max_walkable_slope).
    /// (We use abs() so triangle winding does not matter — both CW and CCW
    /// upward-facing triangles are accepted.)
    float max_walkable_slope { 0.785398163F };  // pi/4 rad (45 deg)

    /// Agent radius in world units. Triangles whose minimum altitude
    /// (smallest perpendicular distance from any vertex to its opposite
    /// edge) is smaller than this value are discarded as "too narrow"
    /// for the agent to traverse. Set to 0 to disable the filter.
    float agent_radius { 0.25F };

    /// Agent height in world units. Currently unused by the simplified
    /// pipeline (would require a heightfield / overhead clearance test
    /// that needs voxelisation). Kept in the API so callers can configure
    /// it now without breaking when full Recast lands. Set to 0 to ignore.
    float agent_height { 1.80F };
};

// =============================================================================
// MeshInput
// =============================================================================

/// Raw triangulated 3D mesh input to the triangulator.
///
/// `vertices[i]`  — (x, y, z) world-space position.
/// `triangles[i]` — three indices into `vertices`. Winding can be CW or CCW;
///                  the slope filter uses |normal.y| so either works.
struct MeshInput
{
    std::span<const std::array<float, 3>>    vertices  {};
    std::span<const std::array<uint32_t, 3>> triangles {};
};

// =============================================================================
// Triangulator
// =============================================================================

/// Stateful mesh -> NavMesh builder.
///
/// Usage:
/// ```
///   cd::ai::pathfinding::Triangulator t;
///   t.configure({.max_walkable_slope = 0.6F, .agent_radius = 0.3F});
///   cd::ai::pathfinding::NavMesh nm = t.build(input);
///   cd::ai::pathfinding::Pathfinder pf;
///   pf.load_navmesh(nm);
/// ```
///
/// Thread safety: `build` is `const` and may be called concurrently with
/// itself on the same Triangulator instance. `configure` is NOT thread-safe.
class Triangulator
{
public:
    Triangulator() = default;

    Triangulator(const Triangulator&)            = default;
    Triangulator& operator=(const Triangulator&) = default;
    Triangulator(Triangulator&&)                 = default;
    Triangulator& operator=(Triangulator&&)      = default;

    ~Triangulator() = default;

    /// Apply a new configuration. The configuration is consumed by the next
    /// call to `build`; existing NavMeshes returned by prior calls are NOT
    /// invalidated (the builder is stateless w.r.t. its outputs).
    void configure(const TriangulationConfig& cfg) noexcept;

    /// Current configuration (defaults if `configure` was never called).
    [[nodiscard]] const TriangulationConfig& config() const noexcept;

    /// Build a NavMesh from a raw source mesh.
    ///
    /// Algorithm:
    ///   1. For every input triangle, compute the geometric normal and
    ///      keep only those with |normal.y| >= cos(max_walkable_slope).
    ///   2. For every walkable triangle, compute the minimum altitude
    ///      (perpendicular distance from any vertex to its opposite edge)
    ///      and drop the triangle when min-altitude < agent_radius.
    ///   3. Re-key the surviving triangles into a compact NavMesh vertex
    ///      buffer (only referenced vertices are emitted).
    ///   4. Build neighbour adjacency by hashing every undirected edge
    ///      (vmin, vmax) to its incident triangles, then for every triangle
    ///      filling in the neighbour index for each of its three edges.
    ///
    /// The returned NavMesh is guaranteed to satisfy
    /// `Pathfinder::load_navmesh` invariants (matched sizes, indices in
    /// range), even when `input` is empty.
    ///
    /// If multiple (>2) triangles share a single edge — which is invalid
    /// for a manifold navmesh — only the first two are linked as neighbours
    /// and the remainder are treated as boundary on that edge. The build
    /// does NOT throw; the consumer can still walk a valid graph.
    [[nodiscard]] NavMesh build(const MeshInput& input) const;

private:
    TriangulationConfig cfg_ {};
};

}  // namespace cd::ai::pathfinding
