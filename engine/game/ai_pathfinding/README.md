# cd::ai::pathfinding

In-memory `NavMesh` + A* pathfinding on a triangulated navigation
mesh. Operates over arbitrary 3D meshes; flat 2D navmeshes are a
degenerate case (`y = 0` for all vertices).

A* runs over the **triangle dual-graph** — one node per triangle,
edges between triangles that share an edge. Each node's centroid is
its position; transitions are scored by Euclidean centroid distance
plus the standard heuristic to the goal centroid.

## Sprints

| Sprint | Phase | Surface |
|--------|-------|---------|
| 1 | `phase671` | NavMesh + A* search over the centroid dual-graph. |
| 2 | queued     | Spatial hash / BVH for nearest-triangle queries (today O(N) linear), funnel-algorithm path smoothing, Recast-compatible triangulation import. |

`cd::core` is the only dependency. No ECS / render coupling.

## Public surface

```cpp
namespace cd::ai::pathfinding {

struct NavTriangle  { uint32_t v[3]; };

class NavMesh
{
public:
    void              add_vertex(cd::math::Vec3f);
    void              add_triangle(NavTriangle);
    void              build();  // builds the dual-graph adjacency

    [[nodiscard]] uint32_t nearest_triangle(cd::math::Vec3f world_pos) const;
};

struct PathResult
{
    bool                          found;
    std::vector<cd::math::Vec3f>  waypoints;
    float                         total_distance;
};

[[nodiscard]] PathResult find_path(const NavMesh& mesh,
                                   cd::math::Vec3f start_pos,
                                   cd::math::Vec3f goal_pos);
}
```

## Algorithm

* `nearest_triangle(world_pos)` — O(N) linear scan over triangles,
  picks the one with the closest centroid. Sprint-2 will replace
  with a BVH / spatial-hash.
* `find_path(mesh, start, goal)` —
  1. Find start / goal triangles via `nearest_triangle`.
  2. Run A* over the dual-graph; node cost = `g(n) + h(n)` where
     `g(n)` is accumulated centroid distance and `h(n)` is Euclidean
     distance to goal-centroid (admissible — never overestimates).
  3. Reconstruct waypoints by walking the parent chain from goal to
     start; the returned `waypoints` are triangle centroids, with
     the actual start / goal positions appended at the ends.

The centroid path is not minimal — Sprint-2's funnel-algorithm
smoothing (Snook 2000) will straighten it across portals.

## References

* Hart, Nilsson & Raphael. *A Formal Basis for the Heuristic
  Determination of Minimum Cost Paths.* IEEE TSSC, 1968.
* Millington & Funge. *AI for Games.* CRC Press, 2009. Chapter 4.
* Snook, G. *Simplified 3D Movement and Pathfinding Using Navigation
  Meshes.* Game Programming Gems 1, 2000.
