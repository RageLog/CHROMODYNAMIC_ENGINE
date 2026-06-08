// =============================================================================
// CHROMODYNAMIC — cd/render/scene/SceneIngest.cpp
//
// Implementation of ingest_gltf_scene (ADR-20260530 P1.2). Walks the
// LoadedScene tree exactly once, depth-first, and:
//
//   * Uploads each unique mesh primitive to the GPU via
//     `cd::render::upload_mesh<GltfPrimitive>`.
//   * Uploads each texture (RGBA8) via `cd::render::upload_texture_2d_rgba8`.
//   * Creates ECS entities + LocalTransform components via the Scene helper.
//   * Wires the parent-child relationships via Scene::attach.
//   * Accumulates a final world-space AABB into the IngestResult.
//
// Failure handling: any allocation error during upload causes a complete
// rollback. We track every handle / entity we created so we can free them
// (strong-exception guarantee) without leaving partial scenes around.
//
// Matrix decomposition: glTF nodes carry a local Mat4f. We decompose into
// (translation, rotation, scale) so ::cd::scene::LocalTransform (TRS form)
// can hold it. The decomposition is the standard pull-translation-from-
// column-3 + per-axis length for scale + orthonormalized columns → quat
// path. Skew is *not* preserved — pragmatic for v1 (every glTF the editor
// reasonably accepts has no shear on its node hierarchy).
// =============================================================================
#include <cd/render/scene/SceneIngest.hpp>

#include <cd/ecs/World.hpp>
#include <cd/math/Quaternion.hpp>
#include <cd/math/Vector.hpp>
#include <cd/scene/Scene.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace cd::render::scene
{

namespace
{

constexpr std::uint32_t kIngestErrorDomain = 0x000E;

[[nodiscard]] cd::core::ErrorCode
make_ingest_error(std::uint32_t code, std::string_view message) noexcept
{
    return cd::core::ErrorCode { kIngestErrorDomain, code, message };
}

[[nodiscard]] float vec3_len(const cd::math::Vec3f& v) noexcept
{
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

[[nodiscard]] cd::math::Vec3f vec3_scale(const cd::math::Vec3f& v, float s) noexcept
{
    return cd::math::Vec3f { v.x * s, v.y * s, v.z * s };
}

/// Decompose a column-major Mat4 into TRS. Skew is assumed absent; if
/// present (rare in glTF node hierarchies) it shows up as a non-orthogonal
/// rotation residue that the matrix-to-quat path silently absorbs. For
/// editor workflows this is acceptable for v1.
[[nodiscard]] cd::math::Transformf decompose_mat4(const cd::math::Mat4f& m) noexcept
{
    cd::math::Transformf out {};

    // Translation = column 3.
    out.position = cd::math::Vec3f { m[3][0], m[3][1], m[3][2] };

    // Scale = length of columns 0..2.
    const cd::math::Vec3f c0 { m[0][0], m[0][1], m[0][2] };
    const cd::math::Vec3f c1 { m[1][0], m[1][1], m[1][2] };
    const cd::math::Vec3f c2 { m[2][0], m[2][1], m[2][2] };
    const float sx = vec3_len(c0);
    const float sy = vec3_len(c1);
    const float sz = vec3_len(c2);
    out.scale = cd::math::Vec3f { sx, sy, sz };

    // Rotation = orthonormalized columns -> matrix-to-quat.
    constexpr float kEps = 1.0E-8F;
    const cd::math::Vec3f r0 = (sx > kEps) ? vec3_scale(c0, 1.0F / sx) : cd::math::Vec3f { 1.0F, 0.0F, 0.0F };
    const cd::math::Vec3f r1 = (sy > kEps) ? vec3_scale(c1, 1.0F / sy) : cd::math::Vec3f { 0.0F, 1.0F, 0.0F };
    const cd::math::Vec3f r2 = (sz > kEps) ? vec3_scale(c2, 1.0F / sz) : cd::math::Vec3f { 0.0F, 0.0F, 1.0F };

    // Shepherd's matrix-to-quat (classic Shoemake 1985).
    const float trace = r0.x + r1.y + r2.z;
    cd::math::Quatf q {};
    if (trace > 0.0F)
    {
        const float s = std::sqrt(trace + 1.0F) * 2.0F;
        q.w = 0.25F * s;
        q.x = (r1.z - r2.y) / s;
        q.y = (r2.x - r0.z) / s;
        q.z = (r0.y - r1.x) / s;
    }
    else if (r0.x > r1.y && r0.x > r2.z)
    {
        const float s = std::sqrt(1.0F + r0.x - r1.y - r2.z) * 2.0F;
        q.w = (r1.z - r2.y) / s;
        q.x = 0.25F * s;
        q.y = (r1.x + r0.y) / s;
        q.z = (r2.x + r0.z) / s;
    }
    else if (r1.y > r2.z)
    {
        const float s = std::sqrt(1.0F + r1.y - r0.x - r2.z) * 2.0F;
        q.w = (r2.x - r0.z) / s;
        q.x = (r1.x + r0.y) / s;
        q.y = 0.25F * s;
        q.z = (r2.y + r1.z) / s;
    }
    else
    {
        const float s = std::sqrt(1.0F + r2.z - r0.x - r1.y) * 2.0F;
        q.w = (r0.y - r1.x) / s;
        q.x = (r2.x + r0.z) / s;
        q.y = (r2.y + r1.z) / s;
        q.z = 0.25F * s;
    }
    out.rotation = q;
    return out;
}

/// Tear down every GPU handle owned by a partially-built IngestResult.
void rollback_gpu(cd::rhi::IDevice& dev, IngestResult& r) noexcept
{
    for (auto& mesh_set : r.gpu_meshes)
    {
        for (auto& gm : mesh_set)
        {
            cd::render::destroy_mesh(dev, gm);
        }
    }
    r.gpu_meshes.clear();

    for (auto& tex : r.gpu_textures)
    {
        cd::render::destroy_texture_2d(dev, tex);
    }
    r.gpu_textures.clear();
}

}  // namespace

cd::core::Result<IngestResult>
ingest_gltf_scene(cd::rhi::IDevice&                          device,
                  cd::ecs::World&                            world,
                  ::cd::scene::Scene&                          scene_tree,
                  const cd::asset::gltf::LoadedScene&        src,
                  IngestOptions                              opts)
{
    IngestResult r {};

    // ---- 1. Texture uploads (one staging buffer per texture, freed before
    // returning from upload_texture_2d_rgba8).
    if (opts.upload_textures)
    {
        r.gpu_textures.reserve(src.textures.size());
        for (const auto& tex : src.textures)
        {
            const auto& img = tex.image;
            if (img.width == 0U || img.height == 0U || img.rgba.empty())
            {
                // Empty / unloaded slots stay as default-constructed handles.
                r.gpu_textures.emplace_back();
                continue;
            }
            auto gt = cd::render::upload_texture_2d_rgba8(
                device, img.rgba.data(), img.width, img.height);
            if (!gt.image.is_valid())
            {
                rollback_gpu(device, r);
                return std::unexpected(make_ingest_error(
                    1U, "ingest_gltf_scene: texture upload failed"));
            }
            r.gpu_textures.push_back(gt);
        }
    }

    // ---- 2. Mesh primitives. One GpuMesh per primitive, kept in a
    // per-mesh vector parallel to LoadedScene::meshes.
    r.gpu_meshes.reserve(src.meshes.size());
    for (const auto& m : src.meshes)
    {
        std::vector<cd::render::GpuMesh> per_prim;
        per_prim.reserve(m.primitives.size());
        for (const auto& p : m.primitives)
        {
            cd::render::GpuMesh gm = cd::render::upload_mesh(device, p.mesh);
            if (!gm.vb.is_valid() || !gm.ib.is_valid())
            {
                rollback_gpu(device, r);
                return std::unexpected(make_ingest_error(
                    2U, "ingest_gltf_scene: mesh upload failed"));
            }
            per_prim.push_back(gm);
        }
        r.gpu_meshes.push_back(std::move(per_prim));
    }

    // ---- 3. ECS entity creation + parent/child wiring.
    if (opts.create_ecs_nodes)
    {
        const cd::math::Mat4f root_xform = opts.use_suggested_xform
            ? src.suggested_world_xform
            : opts.world_xform;

        r.root_entity = scene_tree.create_node();
        if (auto* lt = world.get<::cd::scene::LocalTransform>(r.root_entity))
        {
            lt->value = decompose_mat4(root_xform);
        }

        // Create node entities first (without attachment) so child index
        // references resolve regardless of walk order.
        r.node_entities.reserve(src.nodes.size());
        for (const auto& node : src.nodes)
        {
            const auto e = scene_tree.create_node();
            if (auto* lt = world.get<::cd::scene::LocalTransform>(e))
            {
                lt->value = decompose_mat4(node.local_transform);
            }
            r.node_entities.push_back(e);
        }

        // Attach: every child references its parent index; iterate parents
        // and attach each listed child.
        for (std::size_t parent_i = 0; parent_i < src.nodes.size(); ++parent_i)
        {
            for (const std::uint32_t child_i : src.nodes[parent_i].children)
            {
                if (child_i < r.node_entities.size())
                {
                    scene_tree.attach(r.node_entities[child_i], r.node_entities[parent_i]);
                }
            }
        }

        // Attach each root_node under the synthetic wrapper.
        for (const std::uint32_t root_i : src.root_nodes)
        {
            if (root_i < r.node_entities.size())
            {
                scene_tree.attach(r.node_entities[root_i], r.root_entity);
            }
        }

        // Refresh world transforms so the caller can immediately read
        // `WorldTransform` on any node and have it match LocalTransform
        // composition.
        scene_tree.update_transforms();
    }

    // ---- 4. Pass through the bounds (LoadedScene already computed them).
    r.bounds_world_min = src.bounds_world_min;
    r.bounds_world_max = src.bounds_world_max;

    return r;
}

void destroy_ingest_result(cd::rhi::IDevice& device, IngestResult& r) noexcept
{
    rollback_gpu(device, r);
    // ECS / Scene cleanup is the caller's responsibility — see header docstring.
    r.root_entity   = {};
    r.node_entities.clear();
    r.bounds_world_min = {};
    r.bounds_world_max = {};
}

}  // namespace cd::render::scene
