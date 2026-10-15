// =============================================================================
// cd::gluon — froxel_cluster.glsl
// Canonical 3D froxel cluster indexing & logarithmic depth slice helpers
// matching CPU-side cd::render::cluster::ClusterGrid (DOOM 2016 / Olsson 2012).
// =============================================================================
#ifndef CD_GLUON_FROXEL_CLUSTER_GLSL
#define CD_GLUON_FROXEL_CLUSTER_GLSL

#include <cd/gluon/math_common.glsl>

/// Compute the logarithmic depth slice index in [0, tiles_z - 1] for a positive
/// view-space depth `view_z` given `near_z` and `far_z`.
uint cd_cluster_z_slice(float view_z, float near_z, float far_z, uint tiles_z)
{
    if (tiles_z == 0u || far_z <= near_z || near_z <= 0.0)
        return 0u;
    float clamped = clamp(view_z, near_z, far_z);
    float log_ratio = log(far_z / near_z);
    if (log_ratio <= CD_EPSILON)
        return 0u;
    float t = log(clamped / near_z) / log_ratio;
    int s = int(floor(t * float(tiles_z)));
    s = clamp(s, 0, int(tiles_z) - 1);
    return uint(s);
}

/// Flatten a 3D froxel coordinate `(tx, ty, tz)` into a linear cluster index
/// in `[0, tiles_x * tiles_y * tiles_z - 1]`.
uint cd_cluster_flat_index(uvec3 tile, uvec3 grid_dims)
{
    return (tile.z * grid_dims.y + tile.y) * grid_dims.x + tile.x;
}

/// Compute the 3D froxel coordinate `(tx, ty, tz)` from screen pixel coordinate
/// `frag_xy`, viewport dimensions `viewport_wh`, positive view-space depth
/// `view_z`, `near_z`, `far_z`, and `grid_dims`.
uvec3 cd_cluster_coord_from_frag(
    vec2 frag_xy,
    vec2 viewport_wh,
    float view_z,
    float near_z,
    float far_z,
    uvec3 grid_dims
)
{
    vec2 safe_vp = max(viewport_wh, vec2(1.0));
    vec2 uv = clamp(frag_xy / safe_vp, vec2(0.0), vec2(0.999999));
    uint tx = min(uint(uv.x * float(grid_dims.x)), max(grid_dims.x, 1u) - 1u);
    uint ty = min(uint(uv.y * float(grid_dims.y)), max(grid_dims.y, 1u) - 1u);
    uint tz = cd_cluster_z_slice(view_z, near_z, far_z, grid_dims.z);
    return uvec3(tx, ty, tz);
}

#endif  // CD_GLUON_FROXEL_CLUSTER_GLSL
