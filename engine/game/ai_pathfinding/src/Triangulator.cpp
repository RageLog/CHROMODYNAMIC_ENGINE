// =============================================================================
// CHROMODYNAMIC — cd/ai/pathfinding/Triangulator.cpp
// Phase 761 — Triangulator: mesh -> NavMesh pipeline implementation.
//
// References:
//   * Mononen, "Recast & Detour" — slope + radius filter semantics.
//   * Snook, "Simplified 3D Movement and Pathfinding Using Navigation
//     Meshes", Game Programming Gems 1, 2000 — triangle-centre dual graph
//     with edge-shared neighbour adjacency.
// =============================================================================
#include <cd/ai/pathfinding/Triangulator.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cd::ai::pathfinding
{

// =============================================================================
// Local helpers (anonymous namespace)
// =============================================================================
namespace
{

using Vec3 = std::array<float, 3>;

[[nodiscard]] Vec3 vsub(const Vec3& a, const Vec3& b) noexcept
{
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}

[[nodiscard]] Vec3 vcross(const Vec3& a, const Vec3& b) noexcept
{
    return {a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0]};
}

[[nodiscard]] float vlen(const Vec3& a) noexcept
{
    return std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
}

/// Triangle normal (un-normalised cross of two edges). Caller normalises.
[[nodiscard]] Vec3 triangle_normal(const Vec3& v0, const Vec3& v1, const Vec3& v2) noexcept
{
    return vcross(vsub(v1, v0), vsub(v2, v0));
}

/// Minimum altitude of a triangle = (2 * area) / longest_edge_length.
/// This is the smallest perpendicular distance from any vertex to its
/// opposite edge, i.e. how "thin" the triangle is. A narrow corridor
/// triangle has a small minimum altitude.
[[nodiscard]] float min_altitude(const Vec3& v0, const Vec3& v1, const Vec3& v2) noexcept
{
    const Vec3 normal2 = triangle_normal(v0, v1, v2);
    const float area2  = vlen(normal2);          // |cross| = 2 * area
    if (area2 <= 0.0F) { return 0.0F; }          // degenerate triangle

    const float e01 = vlen(vsub(v1, v0));
    const float e12 = vlen(vsub(v2, v1));
    const float e20 = vlen(vsub(v0, v2));
    const float longest = std::max(e01, std::max(e12, e20));
    if (longest <= 0.0F) { return 0.0F; }
    return area2 / longest;                      // 2A / longest_edge
}

/// Undirected edge key: (min_vertex_index, max_vertex_index).
struct EdgeKey
{
    uint32_t a;
    uint32_t b;
    bool operator==(const EdgeKey& other) const noexcept
    {
        return a == other.a && b == other.b;
    }
};

struct EdgeKeyHash
{
    std::size_t operator()(const EdgeKey& e) const noexcept
    {
        // Cantor-style 64-bit mix. Both halves fit in size_t on 64-bit
        // platforms; on 32-bit we still get a well-distributed hash.
        const auto x = static_cast<std::uint64_t>(e.a);
        const auto y = static_cast<std::uint64_t>(e.b);
        std::uint64_t h = x * 0x9E3779B97F4A7C15ULL;
        h ^= y + 0x9E3779B97F4A7C15ULL + (h << 6U) + (h >> 2U);
        return static_cast<std::size_t>(h);
    }
};

[[nodiscard]] EdgeKey make_edge(const uint32_t a, const uint32_t b) noexcept
{
    return (a < b) ? EdgeKey{a, b} : EdgeKey{b, a};
}

}  // namespace

// =============================================================================
// Triangulator
// =============================================================================

void Triangulator::configure(const TriangulationConfig& cfg) noexcept
{
    cfg_ = cfg;
}

const TriangulationConfig& Triangulator::config() const noexcept
{
    return cfg_;
}

NavMesh Triangulator::build(const MeshInput& input) const
{
    NavMesh out {};

    // --- Step 1: slope + radius filter on input triangles --------------------
    // We collect (input_tri_index) for each survivor; vertex re-keying
    // happens in Step 2.
    const float cos_slope = std::cos(cfg_.max_walkable_slope);
    const float r         = cfg_.agent_radius;

    std::vector<uint32_t> walkable;
    walkable.reserve(input.triangles.size());

    const auto num_in_tris  = static_cast<uint32_t>(input.triangles.size());
    const auto num_in_verts = static_cast<uint32_t>(input.vertices.size());

    for (uint32_t i = 0; i < num_in_tris; ++i)
    {
        const auto& tri = input.triangles[i];

        // Bounds check: silently drop triangles with out-of-range indices
        // (defensive — Recast does the same on malformed importer output).
        if (tri[0] >= num_in_verts || tri[1] >= num_in_verts || tri[2] >= num_in_verts)
        {
            continue;
        }

        const Vec3& v0 = input.vertices[tri[0]];
        const Vec3& v1 = input.vertices[tri[1]];
        const Vec3& v2 = input.vertices[tri[2]];

        const Vec3 n2 = triangle_normal(v0, v1, v2);
        const float len = vlen(n2);
        if (len <= 0.0F) { continue; }   // degenerate

        // Slope test: |normal.y| / |normal| >= cos_slope.
        // Use |normal.y| so winding does not matter.
        const float ny_abs = std::fabs(n2[1]);
        if (ny_abs < cos_slope * len) { continue; }

        // Agent-radius test (skip when r <= 0).
        if (r > 0.0F)
        {
            if (min_altitude(v0, v1, v2) < r) { continue; }
        }

        walkable.push_back(i);
    }

    if (walkable.empty()) { return out; }  // valid empty NavMesh

    // --- Step 2: re-key vertices into a compact buffer -----------------------
    // remap[old_vert_idx] = new_vert_idx, or kUnmapped if not yet emitted.
    constexpr uint32_t kUnmapped = std::numeric_limits<uint32_t>::max();
    std::vector<uint32_t> remap(num_in_verts, kUnmapped);

    out.vertices.reserve(walkable.size() * 3U);
    out.triangles.reserve(walkable.size());

    for (const uint32_t i : walkable)
    {
        const auto& src = input.triangles[i];
        NavTriangle nt {};
        for (std::size_t k = 0; k < 3U; ++k)
        {
            const uint32_t old_idx = src[k];
            if (remap[old_idx] == kUnmapped)
            {
                remap[old_idx] = static_cast<uint32_t>(out.vertices.size());
                out.vertices.push_back(input.vertices[old_idx]);
            }
            nt.vertex_indices[k] = remap[old_idx];
        }
        nt.mesh_id = 0U;
        out.triangles.push_back(nt);
    }

    // --- Step 3: build edge -> incident-triangles map ------------------------
    // For every undirected edge of every emitted triangle, we record the
    // (triangle_index, edge_slot_in_triangle) pairs that share that edge.
    //
    // edge_slot encodes which of the triangle's three edges this is:
    //   slot 0 = edge v0-v1 (NavMesh contract)
    //   slot 1 = edge v1-v2
    //   slot 2 = edge v2-v0
    struct Incidence { uint32_t tri; uint32_t slot; };
    std::unordered_map<EdgeKey, std::array<Incidence, 2>, EdgeKeyHash> edge_map;
    // counts[edge] = how many triangles we've recorded on this edge so far
    // (0, 1, or 2+; 2+ overflows -> first two win, rest treated as boundary).
    std::unordered_map<EdgeKey, uint32_t, EdgeKeyHash> edge_count;

    const auto num_out_tris = static_cast<uint32_t>(out.triangles.size());

    for (uint32_t t = 0; t < num_out_tris; ++t)
    {
        const auto& nt = out.triangles[t];
        const std::array<EdgeKey, 3> edges = {
            make_edge(nt.vertex_indices[0], nt.vertex_indices[1]),
            make_edge(nt.vertex_indices[1], nt.vertex_indices[2]),
            make_edge(nt.vertex_indices[2], nt.vertex_indices[0]),
        };

        for (uint32_t slot = 0; slot < 3U; ++slot)
        {
            const EdgeKey& e = edges[slot];
            const uint32_t c = edge_count[e];
            if (c < 2U)
            {
                edge_map[e][c] = Incidence{t, slot};
            }
            // c >= 2 -> non-manifold; ignore subsequent incidences.
            edge_count[e] = c + 1U;
        }
    }

    // --- Step 4: fill neighbour table ----------------------------------------
    out.triangle_neighbors.assign(
        num_out_tris,
        std::array<uint32_t, 3>{NavMesh::kNoNeighbor,
                                NavMesh::kNoNeighbor,
                                NavMesh::kNoNeighbor});

    for (const auto& kv : edge_map)
    {
        const EdgeKey& e = kv.first;
        const uint32_t c = edge_count.at(e);
        if (c < 2U) { continue; }                // boundary edge

        const auto& inc = kv.second;
        const Incidence& a = inc[0];
        const Incidence& b = inc[1];

        out.triangle_neighbors[a.tri][a.slot] = b.tri;
        out.triangle_neighbors[b.tri][b.slot] = a.tri;
        // If c > 2 the third+ incident triangles are silently skipped
        // (non-manifold; they will see kNoNeighbor on this edge).
    }

    return out;
}

}  // namespace cd::ai::pathfinding
