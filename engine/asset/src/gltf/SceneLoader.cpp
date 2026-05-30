// =============================================================================
// CHROMODYNAMIC — cd/asset/gltf/SceneLoader.cpp
//
// Implementation of the generic glTF scene loader (ADR-20260530 P1.1).
//
// Strategy:
//   1. Call existing load_gltf / load_gltf_from_memory → GltfScene
//   2. Map GltfScene → LoadedScene (rename / restructure only — no
//      reparsing of geometry).
//   3. Auto-detect Z-up via bbox heuristic.
//   4. Compute suggested_world_xform = axis_convert × uniform_scale.
//   5. Walk node tree to populate per-node local bounds and the global
//      world-space AABB.
//
// Why not introduce a new asset type: GltfScene already owns the heavy
// data (vertices, indices, decoded RGBA bytes, animation curves). We just
// rebind it into a tree-shaped, ECS-ingestable form without copying the
// per-primitive vertex / index vectors. `std::move` keeps the cost flat.
// =============================================================================
#include <cd/asset/gltf/SceneLoader.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cd::asset::gltf
{

namespace
{

constexpr float kTargetExtentMeters    = 5.0F;   ///< Frame-selected target.
constexpr float kMinClampedMaxEdge     = 0.01F;  ///< Avoid /0 for degenerate.
constexpr float kMaxClampedMaxEdge     = 1.0E6F; ///< Cap huge-unit assets.
constexpr float kZUpAspectThreshold    = 1.8F;   ///< height/width ratio.

[[nodiscard]] cd::math::Vec3f
mat4_transform_point(const cd::math::Mat4f& m, const cd::math::Vec3f& p) noexcept
{
    const cd::math::Vec4f v { p.x, p.y, p.z, 1.0F };
    const cd::math::Vec4f r = m * v;
    return cd::math::Vec3f { r.x, r.y, r.z };
}

/// Axis-aligned bounding box for an array of points, transformed by `xform`.
/// Returns min/max in world space. Empty input → zero AABB.
struct WorldAabb
{
    cd::math::Vec3f min { 0.0F, 0.0F, 0.0F };
    cd::math::Vec3f max { 0.0F, 0.0F, 0.0F };
    bool            valid { false };
};

void aabb_extend(WorldAabb& a, const cd::math::Vec3f& p) noexcept
{
    if (!a.valid)
    {
        a.min   = p;
        a.max   = p;
        a.valid = true;
        return;
    }
    a.min.x = std::min(a.min.x, p.x);
    a.min.y = std::min(a.min.y, p.y);
    a.min.z = std::min(a.min.z, p.z);
    a.max.x = std::max(a.max.x, p.x);
    a.max.y = std::max(a.max.y, p.y);
    a.max.z = std::max(a.max.z, p.z);
}

/// Heuristic Z-up detection. Pure glTF spec is Y-up; some Blender / 3DS
/// exports write Z-up data without converting. Signal:
///   - bbox extent on Z is >> Y (tall character / building lying along Z)
///   - bbox_min.z ≈ 0 (asset sits on z=0 ground plane)
///   - bbox_min.y < 0 close to bbox_max.y (origin in middle, not feet)
[[nodiscard]] CoordSystem
detect_coord_system(const cd::math::Vec3f& bbox_min,
                    const cd::math::Vec3f& bbox_max) noexcept
{
    const float ext_x = bbox_max.x - bbox_min.x;
    const float ext_y = bbox_max.y - bbox_min.y;
    const float ext_z = bbox_max.z - bbox_min.z;

    // If the dataset is degenerate / empty, default to spec (Y-up).
    if (ext_x <= 0.0F && ext_y <= 0.0F && ext_z <= 0.0F)
    {
        return CoordSystem::kYUp;
    }

    const float xy_avg = 0.5F * (ext_x + ext_y);
    if (xy_avg <= 1.0E-6F)
    {
        return CoordSystem::kYUp;
    }

    // Z >> XY-average ⇒ Z-up.
    if (ext_z / xy_avg > kZUpAspectThreshold)
    {
        return CoordSystem::kZUp;
    }
    return CoordSystem::kYUp;
}

/// Axis-convert matrix from a Z-up source to Y-up engine. Standard glTF
/// fix-up: rotate -90° about X (or equivalently swap Y/Z and flip new Y).
[[nodiscard]] cd::math::Mat4f z_up_to_y_up() noexcept
{
    auto m = cd::math::Mat4f::identity();
    // Column-major: m[col][row]
    //  1  0  0  0
    //  0  0  1  0
    //  0 -1  0  0
    //  0  0  0  1
    m[1][1] = 0.0F;
    m[1][2] = -1.0F;
    m[2][1] = 1.0F;
    m[2][2] = 0.0F;
    return m;
}

/// Build the suggested world transform: axis_convert × uniform_scale.
[[nodiscard]] cd::math::Mat4f
build_suggested_xform(CoordSystem coords,
                      const cd::math::Vec3f& bbox_min,
                      const cd::math::Vec3f& bbox_max) noexcept
{
    const cd::math::Vec3f extent {
        bbox_max.x - bbox_min.x,
        bbox_max.y - bbox_min.y,
        bbox_max.z - bbox_min.z,
    };
    const float max_edge_raw = std::max({extent.x, extent.y, extent.z});
    const float max_edge     = std::clamp(max_edge_raw, kMinClampedMaxEdge, kMaxClampedMaxEdge);
    const float s            = kTargetExtentMeters / max_edge;

    auto scale = cd::math::scaling<float>(cd::math::Vec3f { s, s, s });
    if (coords == CoordSystem::kZUp)
    {
        return z_up_to_y_up() * scale;
    }
    return scale;
}

/// Migrate a GltfMaterial → LoadedMaterial. Pure renaming + std::optional.
[[nodiscard]] LoadedMaterial
make_loaded_material(const GltfMaterial& src) noexcept
{
    LoadedMaterial m;
    m.name = src.name;
    m.base_color_factor = cd::math::Vec4f {
        src.base_color_factor[0],
        src.base_color_factor[1],
        src.base_color_factor[2],
        src.base_color_factor[3],
    };
    if (src.base_color_texture >= 0)
    {
        m.base_color_tex = static_cast<std::uint32_t>(src.base_color_texture);
    }
    // phase452: pull through every glTF texture slot the parser captured.
    if (src.metallic_roughness_texture >= 0)
        m.mr_tex = static_cast<std::uint32_t>(src.metallic_roughness_texture);
    if (src.normal_texture >= 0)
        m.normal_tex = static_cast<std::uint32_t>(src.normal_texture);
    if (src.emissive_texture >= 0)
        m.emissive_tex = static_cast<std::uint32_t>(src.emissive_texture);
    if (src.occlusion_texture >= 0)
        m.occlusion_tex = static_cast<std::uint32_t>(src.occlusion_texture);
    m.emissive_factor  = cd::math::Vec3f { src.emissive_factor[0],
                                           src.emissive_factor[1],
                                           src.emissive_factor[2] };
    m.metallic_factor  = src.metallic_factor;
    m.roughness_factor = src.roughness_factor;
    m.double_sided     = src.double_sided;
    m.alpha_mode       = src.alpha_mode;
    m.alpha_cutoff     = src.alpha_cutoff;
    return m;
}

/// Convert GltfMesh → LoadedMesh, lifting per-material alpha_mode into
/// per-primitive fields. Moves vertex / index buffers, not copies.
[[nodiscard]] LoadedMesh
make_loaded_mesh(GltfMesh&& src, const std::vector<GltfMaterial>& materials)
{
    LoadedMesh m;
    m.name = std::move(src.name);
    m.primitives.reserve(src.primitives.size());
    for (auto& src_prim : src.primitives)
    {
        LoadedPrimitive p;
        p.mesh         = std::move(src_prim);
        p.material_idx = 0U;
        // Inherit alpha info from the material; if the primitive lacks a
        // material entirely we leave the default kOpaque.
        if (p.mesh.material_index >= 0 &&
            static_cast<std::size_t>(p.mesh.material_index) < materials.size())
        {
            const auto& mat = materials[static_cast<std::size_t>(p.mesh.material_index)];
            p.material_idx  = static_cast<std::uint32_t>(p.mesh.material_index);
            p.alpha_mode    = mat.alpha_mode;
            p.alpha_cutoff  = mat.alpha_cutoff;
        }
        m.primitives.push_back(std::move(p));
    }
    return m;
}

/// Compute the local-space AABB of a single mesh by walking its
/// primitives' vertex positions.
[[nodiscard]] WorldAabb mesh_local_aabb(const LoadedMesh& m) noexcept
{
    WorldAabb a {};
    for (const auto& prim : m.primitives)
    {
        for (const auto& v : prim.mesh.vertices)
        {
            aabb_extend(a, cd::math::Vec3f { v.position.x, v.position.y, v.position.z });
        }
    }
    return a;
}

/// Validate forward-only children (no cycles, no out-of-range indices).
[[nodiscard]] bool validate_node_tree(const std::vector<LoadedNode>& nodes) noexcept
{
    const auto n = static_cast<std::uint32_t>(nodes.size());
    for (std::uint32_t i = 0U; i < n; ++i)
    {
        for (const std::uint32_t child : nodes[i].children)
        {
            // Forward-only: child index must be > parent index, AND in range.
            // (load_gltf produces strictly forward layouts; a violation
            // implies a corrupt or maliciously-edited file.)
            if (child <= i || child >= n)
            {
                return false;
            }
        }
    }
    return true;
}

/// Recursively walk the tree, transforming each node's local bounds by the
/// parent chain × world xform, into the global world bounds.
void accumulate_world_bounds(const std::vector<LoadedNode>& nodes,
                             std::uint32_t                  node_idx,
                             const cd::math::Mat4f&         parent_world,
                             WorldAabb&                     out_world)
{
    const auto& n = nodes[node_idx];
    const cd::math::Mat4f my_world = parent_world * n.local_transform;

    if (n.mesh_idx.has_value())
    {
        // Project the 8 corners of the local AABB to world.
        const cd::math::Vec3f c[8] = {
            { n.bounds_local_min.x, n.bounds_local_min.y, n.bounds_local_min.z },
            { n.bounds_local_max.x, n.bounds_local_min.y, n.bounds_local_min.z },
            { n.bounds_local_min.x, n.bounds_local_max.y, n.bounds_local_min.z },
            { n.bounds_local_max.x, n.bounds_local_max.y, n.bounds_local_min.z },
            { n.bounds_local_min.x, n.bounds_local_min.y, n.bounds_local_max.z },
            { n.bounds_local_max.x, n.bounds_local_min.y, n.bounds_local_max.z },
            { n.bounds_local_min.x, n.bounds_local_max.y, n.bounds_local_max.z },
            { n.bounds_local_max.x, n.bounds_local_max.y, n.bounds_local_max.z },
        };
        for (const auto& p : c)
        {
            aabb_extend(out_world, mat4_transform_point(my_world, p));
        }
    }

    for (const std::uint32_t child : n.children)
    {
        accumulate_world_bounds(nodes, child, my_world, out_world);
    }
}

/// Map a GltfScene → LoadedScene. Owning move-in (the caller's GltfScene is
/// left in a moved-from state).
[[nodiscard]] cd::core::Result<LoadedScene> to_loaded_scene(GltfScene&& src)
{
    LoadedScene out;

    // ---- Textures: in-place rebind, no decode work.
    out.textures.reserve(src.textures.size());
    for (auto& t : src.textures)
    {
        LoadedTexture lt;
        lt.image = std::move(t);
        out.textures.push_back(std::move(lt));
    }

    // ---- Materials.
    out.materials.reserve(src.materials.size());
    for (const auto& m : src.materials)
    {
        out.materials.push_back(make_loaded_material(m));
    }

    // ---- Meshes (moves vertex / index buffers).
    out.meshes.reserve(src.meshes.size());
    for (auto& src_mesh : src.meshes)
    {
        out.meshes.push_back(make_loaded_mesh(std::move(src_mesh), src.materials));
    }

    // ---- Skins / animations: full move-in.
    out.skins      = std::move(src.skins);
    out.animations = std::move(src.animations);

    // ---- Nodes.
    out.nodes.reserve(src.nodes.size());
    for (const auto& src_node : src.nodes)
    {
        LoadedNode n;
        n.name            = src_node.name;
        n.local_transform = src_node.local_matrix;
        n.children.reserve(src_node.children.size());
        for (const int c : src_node.children)
        {
            if (c < 0)
            {
                return std::unexpected(gltf_errors::make(
                    gltf_errors::Code::kParseFailed,
                    "SceneLoader: negative child index in node hierarchy"));
            }
            n.children.push_back(static_cast<std::uint32_t>(c));
        }
        if (src_node.mesh_index >= 0)
        {
            n.mesh_idx = static_cast<std::uint32_t>(src_node.mesh_index);
        }
        if (src_node.skin_index >= 0)
        {
            n.skin_idx = static_cast<std::uint32_t>(src_node.skin_index);
        }
        out.nodes.push_back(std::move(n));
    }

    out.root_nodes.reserve(src.roots.size());
    for (const int r : src.roots)
    {
        if (r < 0 || static_cast<std::size_t>(r) >= out.nodes.size())
        {
            return std::unexpected(gltf_errors::make(
                gltf_errors::Code::kParseFailed,
                "SceneLoader: invalid root node index"));
        }
        out.root_nodes.push_back(static_cast<std::uint32_t>(r));
    }

    if (out.root_nodes.empty() && !out.nodes.empty())
    {
        // tinygltf occasionally omits scene.nodes when the document has a
        // single implicit root; promote node 0 to keep the invariant.
        out.root_nodes.push_back(0U);
    }

    if (!validate_node_tree(out.nodes))
    {
        return std::unexpected(gltf_errors::make(
            gltf_errors::Code::kParseFailed,
            "SceneLoader: node tree contains cycle or out-of-range child"));
    }

    // ---- Per-node local bounds (mesh-bearing nodes only; pure transforms
    // inherit their children's bounds via accumulate_world_bounds).
    for (auto& n : out.nodes)
    {
        if (!n.mesh_idx.has_value())
        {
            continue;
        }
        const std::uint32_t mi = *n.mesh_idx;
        if (mi >= out.meshes.size())
        {
            return std::unexpected(gltf_errors::make(
                gltf_errors::Code::kParseFailed,
                "SceneLoader: node references out-of-range mesh"));
        }
        const WorldAabb a = mesh_local_aabb(out.meshes[mi]);
        if (a.valid)
        {
            n.bounds_local_min = a.min;
            n.bounds_local_max = a.max;
        }
    }

    // ---- Coordinate-system + bbox-derived suggested world xform.
    out.source_coords         = detect_coord_system(src.bbox_min, src.bbox_max);
    out.suggested_world_xform = build_suggested_xform(out.source_coords, src.bbox_min, src.bbox_max);

    // ---- World-space bounds via tree walk.
    WorldAabb world_bounds {};
    for (const std::uint32_t r : out.root_nodes)
    {
        accumulate_world_bounds(out.nodes, r, out.suggested_world_xform, world_bounds);
    }
    if (world_bounds.valid)
    {
        out.bounds_world_min = world_bounds.min;
        out.bounds_world_max = world_bounds.max;
    }
    else
    {
        // Asset has no meshes (animation-only / transform-only?). Synthesise
        // a unit cube so the caller doesn't get NaN-poisoned bounds.
        out.bounds_world_min = cd::math::Vec3f { -0.5F, -0.5F, -0.5F };
        out.bounds_world_max = cd::math::Vec3f {  0.5F,  0.5F,  0.5F };
    }

    return out;
}

}  // namespace

cd::core::Result<LoadedScene> load_scene(std::string_view glb_or_gltf_path)
{
    auto raw = load_gltf(glb_or_gltf_path);
    if (!raw.has_value())
    {
        return std::unexpected(raw.error());
    }
    return to_loaded_scene(std::move(*raw));
}

cd::core::Result<LoadedScene>
load_scene_from_memory(const std::uint8_t* bytes,
                       std::size_t          size,
                       std::string_view     base_dir)
{
    auto raw = load_gltf_from_memory(bytes, size, base_dir);
    if (!raw.has_value())
    {
        return std::unexpected(raw.error());
    }
    return to_loaded_scene(std::move(*raw));
}

}  // namespace cd::asset::gltf
