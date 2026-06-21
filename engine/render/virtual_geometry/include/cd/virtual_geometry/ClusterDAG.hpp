// =============================================================================
// CHROMODYNAMIC — cd/virtual_geometry/ClusterDAG.hpp
// phase528 — Nanite-style hierarchical cluster DAG + CPU builder.
//
// Provides:
//   struct Triangle    — three vertex indices
//   struct AABB        — axis-aligned bounding box
//   struct Cluster     — one DAG node (≤128 triangles, bbox, LOD linkage)
//   class  ClusterDAG  — immutable built DAG (build + query)
//   class  ClusterDAGBuilder — offline mesh-to-DAG converter
//
// Reference: Karis, Stenson, Sjödahl 2021 "Nanite: A Deep Dive" SIGGRAPH.
//   Cluster size spec: ≤128 triangles, ≤256 vertices.
//   DAG invariant: parent_lod < lod_level of every child (strictly coarser).
// =============================================================================
#pragma once

#include <cd/math/Vector.hpp>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cd::virtual_geometry
{

// ---------------------------------------------------------------------------
// Primitives
// ---------------------------------------------------------------------------

/// Triangle as three vertex indices into a shared vertex buffer.
struct Triangle
{
    std::uint32_t v[3] {};
};

/// Axis-aligned bounding box (min/max corners).
struct AABB
{
    cd::math::Vec3f min_corner { std::numeric_limits<float>::max(),
                                  std::numeric_limits<float>::max(),
                                  std::numeric_limits<float>::max() };
    cd::math::Vec3f max_corner { std::numeric_limits<float>::lowest(),
                                  std::numeric_limits<float>::lowest(),
                                  std::numeric_limits<float>::lowest() };

    /// Expand box to include point p.
    void expand(cd::math::Vec3f p) noexcept
    {
        min_corner.x = std::min(p.x, min_corner.x);
        min_corner.y = std::min(p.y, min_corner.y);
        min_corner.z = std::min(p.z, min_corner.z);
        max_corner.x = std::max(p.x, max_corner.x);
        max_corner.y = std::max(p.y, max_corner.y);
        max_corner.z = std::max(p.z, max_corner.z);
    }

    [[nodiscard]] cd::math::Vec3f centre() const noexcept
    {
        return { (min_corner.x + max_corner.x) * 0.5F,
                 (min_corner.y + max_corner.y) * 0.5F,
                 (min_corner.z + max_corner.z) * 0.5F };
    }

    [[nodiscard]] float half_diagonal() const noexcept
    {
        const float dx = (max_corner.x - min_corner.x) * 0.5F;
        const float dy = (max_corner.y - min_corner.y) * 0.5F;
        const float dz = (max_corner.z - min_corner.z) * 0.5F;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }
};

// ---------------------------------------------------------------------------
// Cluster — one node in the DAG
// ---------------------------------------------------------------------------

/// Maximum triangles per cluster (Nanite spec).
inline constexpr std::uint32_t kMaxClusterTriangles = 128U;

/// One cluster = one DAG node.
/// `parent_lod` is the LOD level (depth from leaf) of the immediate parent
/// group; always > `lod_level` of this cluster (strictly coarser direction).
/// `parent_lod == UINT32_MAX` marks the DAG root (no parent).
struct Cluster
{
    /// Indices of triangles (offsets into the builder's triangle array).
    std::vector<std::uint32_t> triangles {};

    /// Tight bounding box over all vertices of this cluster.
    AABB bbox {};

    /// LOD level: 0 = finest (leaf), increasing toward root.
    std::uint32_t lod_level    { 0 };

    /// LOD level of the immediate parent group (UINT32_MAX at root).
    std::uint32_t parent_lod   { std::numeric_limits<std::uint32_t>::max() };

    /// Number of finer children this cluster was derived from.
    std::uint32_t child_count  { 0 };

    /// Max geometric error introduced when using this cluster instead of
    /// the original mesh geometry. Measured in world-space units.
    float self_error  { 0.0F };

    /// Max geometric error of the parent cluster group (used for LOD pick).
    float parent_error { std::numeric_limits<float>::max() };

    [[nodiscard]] bool valid() const noexcept
    {
        return !triangles.empty()
            && triangles.size() <= kMaxClusterTriangles;
    }
};

// ---------------------------------------------------------------------------
// ClusterDAG — the immutable built DAG
// ---------------------------------------------------------------------------

/// Immutable DAG produced by ClusterDAGBuilder.
/// `clusters()` returns all nodes ordered fine-to-coarse:
///   clusters()[0 .. leaf_count-1]  = LOD 0 (finest)
///   clusters()[..             ]    = increasing LOD levels
///   clusters().back()              = root cluster
class ClusterDAG
{
public:
    ClusterDAG() = default;
    explicit ClusterDAG(std::vector<Cluster> nodes)
        : m_nodes(std::move(nodes)) {}

    [[nodiscard]] std::span<const Cluster> clusters() const noexcept
    {
        return m_nodes;
    }

    /// Number of distinct LOD levels in the DAG.
    [[nodiscard]] std::uint32_t lod_levels() const noexcept
    {
        if (m_nodes.empty()) return 0U;
        std::uint32_t max_lod = 0U;
        for (const auto& c : m_nodes)
            max_lod = std::max(c.lod_level, max_lod);
        return max_lod + 1U;
    }

    /// True when the DAG satisfies the strict-parent invariant:
    /// for every cluster with a valid parent_lod, parent_lod > lod_level.
    [[nodiscard]] bool is_acyclic() const noexcept
    {
        constexpr auto kNoParent = std::numeric_limits<std::uint32_t>::max();
        return std::ranges::all_of(m_nodes, [](const auto& c)
        {
            return c.parent_lod == kNoParent || c.parent_lod > c.lod_level;
        });
    }

private:
    std::vector<Cluster> m_nodes;
};

// ---------------------------------------------------------------------------
// ClusterDAGBuilder — offline mesh-to-DAG converter
// ---------------------------------------------------------------------------

/// Builds a hierarchical cluster DAG from a raw triangle soup.
///
/// Algorithm:
///   1. Leaf pass: BFS-based partition of input triangles into clusters
///      of ≤ kMaxClusterTriangles each (METIS-style greedy adjacency walk).
///   2. Simplification pass: adjacent leaf clusters are grouped and their
///      triangles edge-collapsed (QEM skeleton: midpoint collapse used here
///      as a structural placeholder — full QEM is wired in phase529).
///   3. Re-cluster simplified mesh to produce the next LOD level.
///   4. Repeat until ≤ kMaxClusterTriangles remain (root cluster).
///
/// Input: flat vertex buffer + flat index buffer (triangle list).
class ClusterDAGBuilder
{
public:
    /// Build the DAG and return the result.
    ///
    /// @param vertices  World-space vertex positions.
    /// @param indices   Triangle indices (size must be divisible by 3).
    ///
    /// @throws std::invalid_argument if indices.size() % 3 != 0 or empty.
    [[nodiscard]] ClusterDAG build(std::span<const cd::math::Vec3f> vertices,
                                   std::span<const std::uint32_t>   indices) const
    {
        if (indices.size() % 3 != 0 || indices.empty())
            throw std::invalid_argument("ClusterDAGBuilder: indices must be "
                                        "a non-empty multiple of 3");

        std::vector<Cluster> all_nodes;

        // --- LOD 0: partition input triangles into leaf clusters ----------
        const auto tri_count =
            static_cast<std::uint32_t>(indices.size() / 3);

        std::vector<std::uint32_t> leaf_tris(tri_count);
        for (std::uint32_t t = 0; t < tri_count; ++t)
            leaf_tris[t] = t;

        auto leaf_clusters = partition_into_clusters(vertices, indices,
                                                     leaf_tris, /*lod=*/0U);
        all_nodes.insert(all_nodes.end(),
                         leaf_clusters.begin(), leaf_clusters.end());

        // --- LOD k+1: simplify + re-cluster until single cluster ----------
        std::vector<Cluster> current_level = std::move(leaf_clusters);
        std::uint32_t current_lod = 0U;

        while (current_level.size() > 1U)
        {
            ++current_lod;
            // Collect all triangles from previous level and midpoint-collapse
            // edges to produce a simplified triangle list at ~50% tri count.
            auto [simp_verts, simp_indices] =
                simplify_level(vertices, indices, current_level);

            const auto s_tri_count =
                static_cast<std::uint32_t>(simp_indices.size() / 3);
            std::vector<std::uint32_t> s_tris(s_tri_count);
            for (std::uint32_t t = 0; t < s_tri_count; ++t) s_tris[t] = t;

            auto next_level = partition_into_clusters(simp_verts, simp_indices,
                                                      s_tris, current_lod);

            // Wire parent linkage: each prev-level cluster's parent_lod =
            // current_lod and parent_error = self_error of first next cluster
            // (conservative — full per-group error in phase529).
            const float representative_parent_error =
                next_level.empty() ? 0.0F : next_level.front().self_error;

            const std::size_t prev_start =
                all_nodes.size() - current_level.size();
            for (std::size_t i = prev_start; i < all_nodes.size(); ++i)
            {
                all_nodes[i].parent_lod   = current_lod;
                all_nodes[i].child_count  = 0U; // leaves set per-cluster
                all_nodes[i].parent_error = representative_parent_error;
            }

            all_nodes.insert(all_nodes.end(),
                             next_level.begin(), next_level.end());
            current_level = std::move(next_level);
        }

        // Root cluster: no parent
        if (!all_nodes.empty())
        {
            all_nodes.back().parent_lod   =
                std::numeric_limits<std::uint32_t>::max();
            all_nodes.back().parent_error =
                std::numeric_limits<float>::max();
        }

        return ClusterDAG { std::move(all_nodes) };
    }

private:
    // -----------------------------------------------------------------------
    // BFS graph-partition: assign `tris` to clusters of ≤128 triangles.
    // Uses triangle indices local to `indices` span; vertices from `verts`.
    // -----------------------------------------------------------------------
    static std::vector<Cluster>
    partition_into_clusters(std::span<const cd::math::Vec3f> verts,
                            std::span<const std::uint32_t>   idx,
                            std::span<const std::uint32_t>   tri_ids,
                            std::uint32_t                    lod)
    {
        std::vector<Cluster> result;
        if (tri_ids.empty()) return result;

        // Build triangle adjacency: tri -> list of adjacent tri indices
        // (share at least one vertex).
        const std::size_t n = tri_ids.size();
        std::vector<std::vector<std::uint32_t>> adj(n);
        build_adjacency(idx, tri_ids, adj);

        std::vector<bool> visited(n, false);
        std::vector<std::uint32_t> queue;
        queue.reserve(kMaxClusterTriangles);

        for (std::uint32_t seed = 0; seed < static_cast<std::uint32_t>(n); ++seed)
        {
            if (visited[seed]) continue;

            Cluster cl;
            cl.lod_level = lod;
            queue.clear();
            queue.push_back(seed);
            visited[seed] = true;

            std::size_t head = 0;
            while (head < queue.size()
                   && cl.triangles.size() < kMaxClusterTriangles)
            {
                const std::uint32_t local_t = queue[head++];
                cl.triangles.push_back(tri_ids[local_t]);

                // Expand bbox over triangle vertices
                const std::uint32_t base = tri_ids[local_t] * 3U;
                for (int k = 0; k < 3; ++k)
                    cl.bbox.expand(verts[idx[base + static_cast<std::size_t>(k)]]);

                // Enqueue unvisited neighbours
                for (const std::uint32_t nb : adj[local_t])
                {
                    if (!visited[nb]
                        && cl.triangles.size() < kMaxClusterTriangles)
                    {
                        visited[nb] = true;
                        queue.push_back(nb);
                    }
                }
            }

            // Geometric error for LOD 0 = 0 (exact). Higher levels set by
            // simplification (diagonal of bbox as conservative placeholder).
            cl.self_error = (lod == 0U) ? 0.0F : cl.bbox.half_diagonal();
            result.push_back(std::move(cl));
        }

        return result;
    }

    // -----------------------------------------------------------------------
    // Build triangle adjacency within the given tri_ids subset.
    // Two triangles are adjacent if they share a vertex index.
    // -----------------------------------------------------------------------
    static void build_adjacency(std::span<const std::uint32_t>   idx,
                                std::span<const std::uint32_t>   tri_ids,
                                std::vector<std::vector<std::uint32_t>>& adj)
    {
        const std::size_t n = tri_ids.size();
        // vertex -> list of local triangle indices that use it
        std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> vtx_to_tris;
        vtx_to_tris.reserve(n * 3);

        for (std::uint32_t local = 0; local < static_cast<std::uint32_t>(n); ++local)
        {
            const std::uint32_t base = tri_ids[local] * 3U;
            for (int k = 0; k < 3; ++k)
            {
                const std::uint32_t v = idx[base + static_cast<std::size_t>(k)];
                vtx_to_tris[v].push_back(local);
            }
        }

        for (const auto& [vtx, locals] : vtx_to_tris)
        {
            for (std::size_t a = 0; a < locals.size(); ++a)
                for (std::size_t b = a + 1; b < locals.size(); ++b)
                {
                    adj[locals[a]].push_back(locals[b]);
                    adj[locals[b]].push_back(locals[a]);
                }
        }

        // Deduplicate adjacency lists
        for (auto& list : adj)
        {
            std::ranges::sort(list);
            list.erase(std::ranges::unique(list).begin(), list.end());
        }
    }

    // -----------------------------------------------------------------------
    // Midpoint-collapse simplification (structural skeleton).
    // Takes the union of all triangles in `level`, collapses each shared
    // edge to its midpoint (approximately halving triangle count), and
    // returns a new self-contained vertex + index buffer.
    //
    // Full QEM (Garland–Heckbert 1997) is wired in phase529.
    // -----------------------------------------------------------------------
    static std::pair<std::vector<cd::math::Vec3f>, std::vector<std::uint32_t>>
    simplify_level(std::span<const cd::math::Vec3f> orig_verts,
                   std::span<const std::uint32_t>   orig_idx,
                   const std::vector<Cluster>&       level)
    {
        // Gather all original vertex positions referenced by this level.
        std::vector<cd::math::Vec3f> new_verts;
        std::vector<std::uint32_t>   new_idx;

        // Map from original vertex index -> new vertex index
        std::unordered_map<std::uint32_t, std::uint32_t> remap;

        for (const Cluster& cl : level)
        {
            for (const std::uint32_t tri_id : cl.triangles)
            {
                const std::uint32_t base = tri_id * 3U;
                std::uint32_t remapped[3] {};
                for (int k = 0; k < 3; ++k)
                {
                    const std::uint32_t orig_v =
                        orig_idx[base + static_cast<std::size_t>(k)];
                    auto it = remap.find(orig_v);
                    if (it == remap.end())
                    {
                        const auto nv =
                            static_cast<std::uint32_t>(new_verts.size());
                        remap[orig_v] = nv;
                        new_verts.push_back(orig_verts[orig_v]);
                        remapped[k] = nv;
                    }
                    else
                    {
                        remapped[k] = it->second;
                    }
                }
                new_idx.push_back(remapped[0]);
                new_idx.push_back(remapped[1]);
                new_idx.push_back(remapped[2]);
            }
        }

        // --- Midpoint collapse: merge every other pair of vertices ----------
        // For each triangle, collapse edge (v0,v1) to midpoint -> replaces
        // both with a new vertex. This is a structural skeleton that
        // approximately halves triangle count by degenerate-triangle removal.
        const auto nverts =
            static_cast<std::uint32_t>(new_verts.size());
        std::vector<std::uint32_t> collapse_target(nverts);
        for (std::uint32_t i = 0; i < nverts; ++i) collapse_target[i] = i;

        // Collapse even-indexed vertices to midpoint with next vertex.
        for (std::uint32_t i = 0; i + 1 < nverts; i += 2)
        {
            const auto& a = new_verts[i];
            const auto& b = new_verts[i + 1];
            new_verts[i] = { (a.x + b.x) * 0.5F,
                             (a.y + b.y) * 0.5F,
                             (a.z + b.z) * 0.5F };
            collapse_target[i + 1] = i;  // remap i+1 -> i
        }

        // Remap indices, discard degenerate triangles.
        std::vector<std::uint32_t> simplified_idx;
        simplified_idx.reserve(new_idx.size());
        for (std::size_t t = 0; t + 2 < new_idx.size(); t += 3)
        {
            const std::uint32_t a = collapse_target[new_idx[t]];
            const std::uint32_t b = collapse_target[new_idx[t + 1]];
            const std::uint32_t c = collapse_target[new_idx[t + 2]];
            if (a == b || b == c || a == c) continue;  // degenerate
            simplified_idx.push_back(a);
            simplified_idx.push_back(b);
            simplified_idx.push_back(c);
        }

        // Ensure at least one triangle survives (root degenerate guard).
        if (simplified_idx.empty() && !new_idx.empty())
        {
            simplified_idx.push_back(new_idx[0]);
            simplified_idx.push_back(new_idx[1]);
            simplified_idx.push_back(new_idx[2]);
        }

        return { std::move(new_verts), std::move(simplified_idx) };
    }

};

}  // namespace cd::virtual_geometry
