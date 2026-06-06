# cd::render::lighting_clusters

Forward+ / clustered-forward 3D froxel-grid light culling.

A view-space frustum is partitioned into `x_tiles × y_tiles × z_slices`
froxels (16 × 9 × 24 default). Sphere-shaped point and spot lights
(`PointLight { world_pos, radius }`) are assigned to every froxel whose
AABB their bounding sphere overlaps. A PBR forward shader then walks
only the lights in its froxel — typically ≈5 of a 200+ visible
light set — so the per-pixel lighting cost stays bounded as the scene
light count climbs.

## Sprints

| Sprint | Phase | Surface |
|--------|-------|---------|
| 1 | `phase660` | CPU-side cluster math + AABB-vs-sphere assignment (`LightingClusters.hpp`). |
| 2 | `phase672` | GPU compute light-culling pass via `kClusterCullCS` (`DispatchPass.hpp`). |

Sprint-2 promoted the library's link surface to PUBLIC `cd::rhi`
because `DispatchPass.hpp` returns `BufferHandle` / `ComputePipelineHandle`;
`cd::shader` is PRIVATE (only the `.cpp` compiles GLSL via glslang).
Mirrors `cd::restir_di` / `cd::ddgi` for the same shader-cache opt-out.

## Public surface

```cpp
namespace cd::render::lighting_clusters {

struct ClusterGrid    { uint32_t x_tiles, y_tiles, z_slices; float near_z, far_z; };
struct PointLight     { float position[3]; float radius; };
struct LightAssignment { std::vector<uint32_t> offsets; std::vector<uint32_t> indices; };

class Clusterer {
public:
    LightAssignment assign(const ClusterGrid& grid,
                           const float view_proj_matrix[16],
                           std::span<const PointLight> lights) const;
};

// Sprint-2 GPU path
struct DispatchPassDesc { /* ... */ };
class DispatchPass {
public:
    static cd::expected<DispatchPass, Error> create(const DispatchPassDesc&);
    void record(cd::rhi::CommandList& cmd, /* ... */);
};

}
```

## Indexing contract

```cpp
linear_cluster_index = (z_slice * y_tiles + ty) * x_tiles + tx;
```

`LightAssignment::offsets[k]` is the first index into
`LightAssignment::indices` for cluster `k`; the range
`[offsets[k], offsets[k+1])` holds that cluster's light ids.
`lights_in_cluster()` is a convenience span over that slice.

## Naming-discipline note

This library is **distinct** from `cd::render::cluster` (the wave-84
header-only `ClusterGrid` using a view-space angular/depth API).
The two coexist:

* `cd::render::cluster` — viewspace-angular tile, header-only, used by
  the legacy renderer paths still in `hello_engine`.
* `cd::render::lighting_clusters` — froxel-grid AABB, GPU-ready
  layout, used by the new Forward+ pass.

The wave-84 library cannot be deleted until every consumer migrates
to the froxel layout (work tracked under `L-light-clusters-unify`).

## References

* Olsson & Assarsson, *Tiled Shading*, JCGT 2011.
* Olsson, Billeter & Assarsson, *Clustered Deferred and Forward
  Shading*, HPG 2012.
* id Tech 6, *Clustered Forward Lighting in Doom 2016*, GDC 2016.
* Quantic Dream, *Detroit: Become Human cluster forward*, 2018.
