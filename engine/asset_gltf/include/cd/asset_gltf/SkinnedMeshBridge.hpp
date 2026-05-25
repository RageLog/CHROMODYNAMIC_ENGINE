// =============================================================================
// CHROMODYNAMIC — cd/asset_gltf/SkinnedMeshBridge.hpp
// Phase 170 / v0.99.91 — glTF skin/mesh → cd::anim + cd::anim::SkinnedVertex.
//
// The glTF importer (Phase 71+) decodes skin data into `GltfSkin` +
// `GltfSkinVertex` records. The skinning pipeline (Phase 156 + 168)
// consumes `cd::anim::Skeleton`, `cd::anim::SkinnedVertex`, and
// `cd::anim::SkinningMatricesUbo`. This header bridges the two
// without forcing the importer to depend on cd::anim or vice versa
// — it's pure conversion functions.
//
// API:
//   to_skeleton(scene, skin_index)
//       Build a cd::anim::Skeleton from one of the scene's skins.
//       Joints are reordered into topological order (root-first) so
//       the existing Skeleton constructor accepts them.
//
//   to_skinned_vertices(prim)
//       Convert a primitive's interleaved vertices + parallel skin
//       attributes into the cd::anim::SkinnedVertex layout.
//
//   build_node_bind_map(scene)
//       Helper that walks the node tree and produces the world-space
//       transform per node — needed because glTF's joint hierarchy
//       references node indices but the joint's local bind transform
//       comes from the node's local_matrix.
//
// Header-only; depends on cd::anim + cd::asset_gltf.
// =============================================================================
#pragma once

#include <cd/anim/GpuSkinning.hpp>
#include <cd/anim/Skeleton.hpp>
#include <cd/asset_gltf/GltfLoader.hpp>
#include <cd/core/Defines.hpp>
#include <cd/math/Matrix.hpp>

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace cd::asset_gltf
{

/// Convert a primitive's interleaved + skin attributes into
/// cd::anim::SkinnedVertex records. The output has the same length
/// as `prim.vertices`. If `prim.skin_vertices` is empty (non-skinned
/// primitive), every output vertex falls back to a single full-
/// weight influence on joint 0 — the renderer can still feed it
/// through the skinning shader without ill effect.
[[nodiscard]] inline std::vector<cd::anim::SkinnedVertex>
to_skinned_vertices(const GltfPrimitive& prim)
{
    std::vector<cd::anim::SkinnedVertex> out;
    out.reserve(prim.vertices.size());
    const bool has_skin = !prim.skin_vertices.empty();
    for (std::size_t i = 0; i < prim.vertices.size(); ++i)
    {
        const auto& v = prim.vertices[i];
        cd::anim::SkinnedVertex sv;
        sv.position = v.position;
        sv.normal   = v.normal;
        sv.uv       = v.texcoord0;
        if (has_skin && i < prim.skin_vertices.size())
        {
            const auto& s = prim.skin_vertices[i];
            sv.bone_ids[0] = s.joints[0];
            sv.bone_ids[1] = s.joints[1];
            sv.bone_ids[2] = s.joints[2];
            sv.bone_ids[3] = s.joints[3];
            sv.bone_weights[0] = s.weights[0];
            sv.bone_weights[1] = s.weights[1];
            sv.bone_weights[2] = s.weights[2];
            sv.bone_weights[3] = s.weights[3];
        }
        out.push_back(sv);
    }
    return out;
}

/// Extract a Transformf from a 4x4 column-major matrix.
/// Decomposes into translation + rotation (assuming no shear) +
/// uniform scale (using the length of the first column).
[[nodiscard]] inline cd::math::Transformf
decompose_local(const cd::math::Mat4f& m) noexcept
{
    cd::math::Transformf xf;
    xf.position = { m[3][0], m[3][1], m[3][2] };
    const float sx = std::sqrt(m[0][0]*m[0][0] + m[0][1]*m[0][1] + m[0][2]*m[0][2]);
    const float sy = std::sqrt(m[1][0]*m[1][0] + m[1][1]*m[1][1] + m[1][2]*m[1][2]);
    const float sz = std::sqrt(m[2][0]*m[2][0] + m[2][1]*m[2][1] + m[2][2]*m[2][2]);
    xf.scale = { sx, sy, sz };
    // Rotation extraction (Shoemake): build the 3x3 rotation by
    // dividing each column by its scale, then convert to quaternion.
    const float inv_sx = sx > 1e-6F ? 1.0F / sx : 0.0F;
    const float inv_sy = sy > 1e-6F ? 1.0F / sy : 0.0F;
    const float inv_sz = sz > 1e-6F ? 1.0F / sz : 0.0F;
    const float r00 = m[0][0]*inv_sx, r01 = m[0][1]*inv_sx, r02 = m[0][2]*inv_sx;
    const float r10 = m[1][0]*inv_sy, r11 = m[1][1]*inv_sy, r12 = m[1][2]*inv_sy;
    const float r20 = m[2][0]*inv_sz, r21 = m[2][1]*inv_sz, r22 = m[2][2]*inv_sz;
    // Quaternion from rotation matrix (Sarrus / Shepperd variant).
    const float trace = r00 + r11 + r22;
    if (trace > 0.0F)
    {
        const float s = std::sqrt(trace + 1.0F) * 2.0F;
        xf.rotation.w = 0.25F * s;
        xf.rotation.x = (r12 - r21) / s;
        xf.rotation.y = (r20 - r02) / s;
        xf.rotation.z = (r01 - r10) / s;
    }
    else if (r00 > r11 && r00 > r22)
    {
        const float s = std::sqrt(1.0F + r00 - r11 - r22) * 2.0F;
        xf.rotation.w = (r12 - r21) / s;
        xf.rotation.x = 0.25F * s;
        xf.rotation.y = (r10 + r01) / s;
        xf.rotation.z = (r20 + r02) / s;
    }
    else if (r11 > r22)
    {
        const float s = std::sqrt(1.0F + r11 - r00 - r22) * 2.0F;
        xf.rotation.w = (r20 - r02) / s;
        xf.rotation.x = (r10 + r01) / s;
        xf.rotation.y = 0.25F * s;
        xf.rotation.z = (r21 + r12) / s;
    }
    else
    {
        const float s = std::sqrt(1.0F + r22 - r00 - r11) * 2.0F;
        xf.rotation.w = (r01 - r10) / s;
        xf.rotation.x = (r20 + r02) / s;
        xf.rotation.y = (r21 + r12) / s;
        xf.rotation.z = 0.25F * s;
    }
    return xf;
}

/// Build a cd::anim::Skeleton from glTF skin #skin_index.
/// Joints are remapped into topological order (parent precedes
/// child) so the Skeleton constructor's invariant holds.
/// Returns an empty Skeleton if skin_index is out of range.
[[nodiscard]] inline cd::anim::Skeleton
to_skeleton(const GltfScene& scene, std::size_t skin_index)
{
    if (skin_index >= scene.skins.size()) return cd::anim::Skeleton {};
    const auto& gskin = scene.skins[skin_index];

    // Map glTF node index → joint index (position within gskin.joints).
    std::unordered_map<int, std::int32_t> node_to_joint;
    node_to_joint.reserve(gskin.joints.size());
    for (std::size_t i = 0; i < gskin.joints.size(); ++i)
        node_to_joint[gskin.joints[i]] = static_cast<std::int32_t>(i);

    // Compute each joint's parent (within the joint set). Walk up the
    // node tree until we hit a node that's also a joint.
    auto parent_in_skin = [&](int node_idx) -> std::int32_t
    {
        int cursor = node_idx;
        while (cursor >= 0 && cursor < static_cast<int>(scene.nodes.size()))
        {
            const int parent = scene.nodes[static_cast<std::size_t>(cursor)].parent;
            if (parent < 0) return -1;
            auto it = node_to_joint.find(parent);
            if (it != node_to_joint.end()) return it->second;
            cursor = parent;
        }
        return -1;
    };

    // Build a candidate joint list in the order glTF gave us.
    std::vector<cd::anim::Joint> joints;
    joints.reserve(gskin.joints.size());
    for (std::size_t i = 0; i < gskin.joints.size(); ++i)
    {
        const int node_idx = gskin.joints[i];
        cd::anim::Joint j;
        j.name = (node_idx >= 0 && node_idx < static_cast<int>(scene.nodes.size()))
                     ? scene.nodes[static_cast<std::size_t>(node_idx)].name
                     : std::string {};
        j.parent = parent_in_skin(node_idx);
        if (node_idx >= 0 && node_idx < static_cast<int>(scene.nodes.size()))
            j.local_bind = decompose_local(scene.nodes[static_cast<std::size_t>(node_idx)].local_matrix);
        if (i < gskin.inverse_bind_matrices.size())
            j.inverse_bind_matrix = gskin.inverse_bind_matrices[i];
        joints.push_back(std::move(j));
    }

    // Reorder into topological order (root-first): a stable bucket
    // sort by depth. A joint's depth = parent depth + 1.
    std::vector<std::int32_t> depths(joints.size(), 0);
    bool changed = true;
    while (changed)
    {
        changed = false;
        for (std::size_t i = 0; i < joints.size(); ++i)
        {
            if (joints[i].parent < 0) continue;
            const std::int32_t want = depths[static_cast<std::size_t>(joints[i].parent)] + 1;
            if (depths[i] < want) { depths[i] = want; changed = true; }
        }
    }
    // Build the new ordering: sort by (depth, original index).
    std::vector<std::size_t> order(joints.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(),
        [&](std::size_t a, std::size_t b) { return depths[a] < depths[b]; });

    // Re-map old indices → new indices, then build the final joint list.
    std::vector<std::int32_t> remap(joints.size(), -1);
    for (std::size_t new_i = 0; new_i < order.size(); ++new_i)
        remap[order[new_i]] = static_cast<std::int32_t>(new_i);
    std::vector<cd::anim::Joint> sorted_joints;
    sorted_joints.reserve(joints.size());
    for (auto old_i : order)
    {
        auto j = joints[old_i];
        if (j.parent >= 0) j.parent = remap[static_cast<std::size_t>(j.parent)];
        sorted_joints.push_back(std::move(j));
    }

    return cd::anim::Skeleton { std::move(sorted_joints) };
}

}  // namespace cd::asset_gltf
