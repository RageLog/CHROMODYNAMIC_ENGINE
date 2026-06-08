// =============================================================================
// CHROMODYNAMIC — cd/ai/pathfinding/Pathfinding.cpp
// Phase 671 — cd::ai::pathfinding A* implementation.
//
// References:
//   * Millington & Funge, "Artificial Intelligence for Games", 2nd ed.,
//     CRC Press 2009, Ch. 4 — A* algorithm.
//   * Hart, Nilsson & Raphael, IEEE TSSC 1968 — admissible heuristic proof.
//   * Snook, "Simplified 3D Movement and Pathfinding Using Navigation Meshes",
//     Game Programming Gems 1, 2000 — triangle-centre dual-graph A*.
// =============================================================================
#include <cd/ai/pathfinding/Pathfinding.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace cd::ai::pathfinding
{

// =============================================================================
// Internal helpers
// =============================================================================

/*static*/ float Pathfinder::dist3(const std::array<float, 3>& a,
                                    const std::array<float, 3>& b) noexcept
{
    const float dx = a[0] - b[0];
    const float dy = a[1] - b[1];
    const float dz = a[2] - b[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

std::array<float, 3> Pathfinder::centroid(const uint32_t tri_idx) const noexcept
{
    const NavTriangle& tri = mesh_.triangles[tri_idx];
    const auto& v0 = mesh_.vertices[tri.vertex_indices[0]];
    const auto& v1 = mesh_.vertices[tri.vertex_indices[1]];
    const auto& v2 = mesh_.vertices[tri.vertex_indices[2]];
    return {(v0[0] + v1[0] + v2[0]) / 3.0F,
            (v0[1] + v1[1] + v2[1]) / 3.0F,
            (v0[2] + v1[2] + v2[2]) / 3.0F};
}

// =============================================================================
// Mesh management
// =============================================================================

void Pathfinder::load_navmesh(const NavMesh& mesh)
{
    // --- Structural invariant: neighbour list must match triangles list -------
    if (mesh.triangles.size() != mesh.triangle_neighbors.size())
    {
        throw std::invalid_argument(
            "cd::ai::pathfinding::Pathfinder::load_navmesh — "
            "triangles.size() != triangle_neighbors.size()");
    }

    const auto num_verts = static_cast<uint32_t>(mesh.vertices.size());
    const auto num_tris  = static_cast<uint32_t>(mesh.triangles.size());

    // --- Validate vertex indices in every triangle ---------------------------
    for (uint32_t i = 0; i < num_tris; ++i)
    {
        for (const uint32_t vi : mesh.triangles[i].vertex_indices)
        {
            if (vi >= num_verts)
            {
                throw std::invalid_argument(
                    "cd::ai::pathfinding::Pathfinder::load_navmesh — "
                    "vertex index " + std::to_string(vi) +
                    " in triangle " + std::to_string(i) +
                    " is out of range (num_vertices=" +
                    std::to_string(num_verts) + ")");
            }
        }
    }

    // --- Validate neighbour indices -----------------------------------------
    for (uint32_t i = 0; i < num_tris; ++i)
    {
        for (const uint32_t ni : mesh.triangle_neighbors[i])
        {
            if (ni != NavMesh::kNoNeighbor && ni >= num_tris)
            {
                throw std::invalid_argument(
                    "cd::ai::pathfinding::Pathfinder::load_navmesh — "
                    "neighbour index " + std::to_string(ni) +
                    " for triangle " + std::to_string(i) +
                    " is out of range (num_triangles=" +
                    std::to_string(num_tris) + ")");
            }
        }
    }

    // --- Commit and pre-compute centroids ------------------------------------
    mesh_ = mesh;
    centroids_.clear();
    centroids_.reserve(num_tris);
    for (uint32_t i = 0; i < num_tris; ++i)
    {
        centroids_.push_back(centroid(i));
    }
}

bool Pathfinder::has_navmesh() const noexcept
{
    return !mesh_.triangles.empty();
}

// =============================================================================
// nearest_triangle
// =============================================================================

std::optional<uint32_t> Pathfinder::nearest_triangle(const std::array<float, 3> point) const
{
    if (centroids_.empty()) { return std::nullopt; }

    uint32_t best     = 0U;
    float    best_d2  = std::numeric_limits<float>::max();

    const auto n = static_cast<uint32_t>(centroids_.size());
    for (uint32_t i = 0; i < n; ++i)
    {
        const auto& c = centroids_[i];
        const float dx = point[0] - c[0];
        const float dy = point[1] - c[1];
        const float dz = point[2] - c[2];
        const float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 < best_d2)
        {
            best_d2 = d2;
            best    = i;
        }
    }
    return best;
}

// =============================================================================
// find_path — A* on the triangle dual-graph
// =============================================================================

PathResult Pathfinder::find_path(const PathRequest& req) const
{
    PathResult result {};

    if (!has_navmesh()) { return result; }

    // --- Locate start and goal triangles ------------------------------------
    const auto start_tri_opt = nearest_triangle(req.start);
    const auto goal_tri_opt  = nearest_triangle(req.goal);

    // Should never be nullopt since has_navmesh() == true, but guard anyway.
    if (!start_tri_opt || !goal_tri_opt) { return result; }

    const uint32_t start_tri = *start_tri_opt;
    const uint32_t goal_tri  = *goal_tri_opt;

    // --- Trivial same-triangle case ------------------------------------------
    // If both points map to the same triangle we emit a direct two-point path.
    if (start_tri == goal_tri)
    {
        result.waypoints      = {req.start, req.goal};
        result.success        = true;
        result.total_distance = dist3(req.start, req.goal);
        result.triangles_explored = 1U;
        return result;
    }

    // --- A* ------------------------------------------------------------------
    // Node in the open set: (f_score, triangle_index).
    // Lower f_score = higher priority.
    using AStarNode = std::pair<float, uint32_t>;
    std::priority_queue<AStarNode,
                        std::vector<AStarNode>,
                        std::greater<>> open_set;

    const auto num_tris = static_cast<uint32_t>(centroids_.size());

    // g_score[i] = cost from start to triangle i (centroid distance).
    std::vector<float> g_score(num_tris, std::numeric_limits<float>::max());

    // came_from[i] = parent triangle index in the A* tree.
    constexpr uint32_t kNone = std::numeric_limits<uint32_t>::max();
    std::vector<uint32_t> came_from(num_tris, kNone);

    const std::array<float, 3>& goal_centroid = centroids_[goal_tri];

    g_score[start_tri] = 0.0F;
    const float h_start = dist3(centroids_[start_tri], goal_centroid);
    open_set.emplace(h_start, start_tri);

    uint32_t explored = 0U;
    bool     found    = false;

    while (!open_set.empty())
    {
        auto [f_cur, cur] = open_set.top();
        open_set.pop();
        ++explored;

        // Stale entry guard: if we've already found a better g, skip.
        // f_cur = g_cur + h_cur; recover g_cur to check.
        const float h_cur   = dist3(centroids_[cur], goal_centroid);
        const float g_cur_f = f_cur - h_cur;
        if (g_cur_f > g_score[cur] + 1e-4F) { continue; }

        // Prune by max_search_distance — using the heuristic distance from
        // current to goal so we don't explore far-away triangles.
        if (h_cur > req.max_search_distance) { continue; }

        if (cur == goal_tri)
        {
            found = true;
            break;
        }

        // Expand neighbours.
        for (const uint32_t nb : mesh_.triangle_neighbors[cur])
        {
            if (nb == NavMesh::kNoNeighbor) { continue; }

            const float edge_cost = dist3(centroids_[cur], centroids_[nb]);
            const float tentative_g = g_score[cur] + edge_cost;

            if (tentative_g < g_score[nb])
            {
                came_from[nb] = cur;
                g_score[nb]   = tentative_g;
                const float h_nb = dist3(centroids_[nb], goal_centroid);
                open_set.emplace(tentative_g + h_nb, nb);
            }
        }
    }

    result.triangles_explored = explored;

    if (!found) { return result; }

    // --- Reconstruct path ----------------------------------------------------
    // Walk came_from from goal_tri back to start_tri.
    std::vector<uint32_t> tri_chain;
    {
        uint32_t cur = goal_tri;
        while (cur != kNone)
        {
            tri_chain.push_back(cur);
            cur = came_from[cur];
        }
        std::reverse(tri_chain.begin(), tri_chain.end());
    }

    // Build waypoints: centroid for each triangle in the chain, then replace
    // the last waypoint with the exact goal position.
    result.waypoints.reserve(tri_chain.size() + 1U);
    for (const uint32_t tri : tri_chain)
    {
        result.waypoints.push_back(centroids_[tri]);
    }
    // Replace the terminal waypoint with the exact goal.
    result.waypoints.back() = req.goal;

    // Compute total distance along waypoints.
    float total = 0.0F;
    for (std::size_t i = 1; i < result.waypoints.size(); ++i)
    {
        total += dist3(result.waypoints[i - 1], result.waypoints[i]);
    }
    result.total_distance = total;
    result.success        = true;

    return result;
}

}  // namespace cd::ai::pathfinding
