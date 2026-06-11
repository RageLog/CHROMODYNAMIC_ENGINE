// =============================================================================
// CHROMODYNAMIC — cd/asset/gltf/SkinnedMeshBridge.hpp
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
#include <cd/asset/gltf/GltfLoader.hpp>
#include <cd/core/Defines.hpp>
#include <cd/math/Matrix.hpp>

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace cd::asset::gltf
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
    const float r00 = m[0][0]*inv_sx;
    const float r01 = m[0][1]*inv_sx;
    const float r02 = m[0][2]*inv_sx;
    const float r10 = m[1][0]*inv_sy;
    const float r11 = m[1][1]*inv_sy;
    const float r12 = m[1][2]*inv_sy;
    const float r20 = m[2][0]*inv_sz;
    const float r21 = m[2][1]*inv_sz;
    const float r22 = m[2][2]*inv_sz;
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
    std::ranges::stable_sort(order,
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

// =============================================================================
// SK3 (phase 227) — animation bridge.
//
// `to_skeleton` re-orders joints into topological depth-sorted order, which
// means cd::anim::Skeleton joint indices DO NOT match GltfSkin.joints[]
// indices. The bridge below exposes the remap so animation channels (which
// target glTF *node* indices) can be resolved to the correct skeleton joint
// index at sample time.
// =============================================================================

/// Skeleton + glTF-node->joint mapping. The mapping is exactly the inverse of
/// the remap `to_skeleton` builds internally; `bundle.node_to_joint[gltf_node]`
/// returns the skeleton joint index that drives that node.
struct SkeletonBundle
{
    cd::anim::Skeleton skeleton;
    std::unordered_map<int, std::int32_t> node_to_joint;  ///< glTF node index -> skeleton joint index
    /// glTF skin joint index (position within gskin.joints[]) -> skeleton joint index.
    /// Critical for CPU/GPU skinning: vertex JOINTS_0 attributes use skin-joint
    /// indices, not node indices or skeleton joint indices, so this remap turns
    /// a vertex influence into the right slot of the matrix palette.
    std::vector<std::int32_t> skin_joint_remap;
};

/// Sister of `to_skeleton` that also returns the glTF-node-index ->
/// skeleton-joint-index map. Use this when you need to drive the skeleton
/// from animation channels (each channel targets a glTF node, not a joint).
[[nodiscard]] inline SkeletonBundle
to_skeleton_bundle(const GltfScene& scene, std::size_t skin_index)
{
    SkeletonBundle out;
    if (skin_index >= scene.skins.size()) return out;
    const auto& gskin = scene.skins[skin_index];

    std::unordered_map<int, std::int32_t> node_to_joint_orig;
    node_to_joint_orig.reserve(gskin.joints.size());
    for (std::size_t i = 0; i < gskin.joints.size(); ++i)
        node_to_joint_orig[gskin.joints[i]] = static_cast<std::int32_t>(i);

    auto parent_in_skin = [&](int node_idx) -> std::int32_t
    {
        int cursor = node_idx;
        while (cursor >= 0 && cursor < static_cast<int>(scene.nodes.size()))
        {
            const int parent = scene.nodes[static_cast<std::size_t>(cursor)].parent;
            if (parent < 0) return -1;
            auto it = node_to_joint_orig.find(parent);
            if (it != node_to_joint_orig.end()) return it->second;
            cursor = parent;
        }
        return -1;
    };

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
    std::vector<std::size_t> order(joints.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::ranges::stable_sort(order,
        [&](std::size_t a, std::size_t b) { return depths[a] < depths[b]; });

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

    out.skeleton = cd::anim::Skeleton { std::move(sorted_joints) };
    // Final glTF-node -> skeleton-joint mapping = compose original joint
    // index with the topological-sort remap.
    out.node_to_joint.reserve(node_to_joint_orig.size());
    for (const auto& [node_idx, orig_joint] : node_to_joint_orig)
    {
        out.node_to_joint[node_idx] = remap[static_cast<std::size_t>(orig_joint)];
    }
    // skin_joint_remap[i] = sorted skeleton joint index for the i-th
    // entry of gskin.joints[]. Used by callers to translate vertex
    // JOINTS_0 attributes (which are skin-joint indices) into matrix-
    // palette indices.
    out.skin_joint_remap = std::move(remap);
    return out;
}

// =============================================================================
// Per-channel sampling helpers (SK3 phase 227).
// Inline because each one is a handful of lines and the bridge consumer is a
// single end-to-end path (hello_engine).
// =============================================================================

namespace detail
{

[[nodiscard]] inline std::size_t find_keyframe_index(
    const std::vector<float>& times, float t) noexcept
{
    // Linear scan is fine for short clips (<= a few hundred frames). For
    // longer clips a binary search would help; CesiumMan has <50 frames so
    // we stay simple.
    if (times.empty()) return 0;
    if (t <= times.front()) return 0;
    if (t >= times.back())  return times.size() - 1;
    for (std::size_t i = 1; i < times.size(); ++i)
        if (t < times[i]) return i - 1;
    return times.size() - 2;
}

[[nodiscard]] inline float interp_alpha(
    const std::vector<float>& times, float t, std::size_t i0) noexcept
{
    if (i0 + 1 >= times.size()) return 0.0F;
    const float t0 = times[i0];
    const float t1 = times[i0 + 1];
    const float dt = t1 - t0;
    if (dt < 1e-6F) return 0.0F;
    return std::clamp((t - t0) / dt, 0.0F, 1.0F);
}

[[nodiscard]] inline cd::math::Vec3f read_vec3(
    const std::vector<float>& values, std::size_t key_idx) noexcept
{
    const std::size_t base = key_idx * 3;
    return { values[base + 0], values[base + 1], values[base + 2] };
}

[[nodiscard]] inline cd::math::Quatf read_quat(
    const std::vector<float>& values, std::size_t key_idx) noexcept
{
    const std::size_t base = key_idx * 4;
    return { values[base + 0], values[base + 1], values[base + 2], values[base + 3] };
}

[[nodiscard]] inline cd::math::Quatf slerp_quat(
    cd::math::Quatf a, cd::math::Quatf b, float t) noexcept
{
    // Standard 4D dot + sign-flip for short-arc slerp.
    float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (d < 0.0F) { b = { -b.x, -b.y, -b.z, -b.w }; d = -d; }
    if (d > 0.9995F)
    {
        // Linear interpolation when nearly aligned, then normalise.
        cd::math::Quatf r { a.x + (b.x - a.x) * t,
                           a.y + (b.y - a.y) * t,
                           a.z + (b.z - a.z) * t,
                           a.w + (b.w - a.w) * t };
        const float n = std::sqrt(r.x*r.x + r.y*r.y + r.z*r.z + r.w*r.w);
        if (n > 1e-6F) { r.x /= n; r.y /= n; r.z /= n; r.w /= n; }
        return r;
    }
    const float theta = std::acos(std::clamp(d, -1.0F, 1.0F));
    const float st    = std::sin(theta);
    const float wa    = std::sin((1.0F - t) * theta) / st;
    const float wb    = std::sin(t * theta) / st;
    return { wa * a.x + wb * b.x,
             wa * a.y + wb * b.y,
             wa * a.z + wb * b.z,
             wa * a.w + wb * b.w };
}

}  // namespace detail

/// Sample one glTF animation at time `t` (seconds, caller wraps for looping)
/// and write the resulting per-joint Transforms into `pose`. Channels whose
/// target node is outside the skin are silently skipped. Channels with the
/// kMorphWeights path are skipped (morph targets not yet supported).
inline void sample_gltf_animation(
    const GltfAnimation& anim,
    const std::unordered_map<int, std::int32_t>& node_to_joint,
    float t,
    cd::anim::Pose& pose)
{
    for (const auto& ch : anim.channels)
    {
        if (ch.path == GltfTargetPath::kMorphWeights) continue;
        if (ch.sampler_index < 0 ||
            ch.sampler_index >= static_cast<int>(anim.samplers.size())) continue;
        auto it = node_to_joint.find(ch.target_node);
        if (it == node_to_joint.end()) continue;
        const std::int32_t joint = it->second;
        if (joint < 0 ||
            joint >= static_cast<std::int32_t>(pose.joint_locals.size())) continue;

        const auto& s = anim.samplers[static_cast<std::size_t>(ch.sampler_index)];
        if (s.times.empty()) continue;
        const std::size_t i0 = detail::find_keyframe_index(s.times, t);
        const float alpha = (s.interpolation == GltfInterpolation::kStep)
                                ? 0.0F  // STEP holds the lower key
                                : detail::interp_alpha(s.times, t, i0);
        // CUBICSPLINE keyframes are 3 packed entries per time
        // (inTangent, value, outTangent). We currently treat them as LINEAR
        // by reading just the middle (value) entry — visually close, avoids
        // tangent maths. Promote later if user assets need it.
        const bool cubic = (s.interpolation == GltfInterpolation::kCubicSpline);
        auto value_index = [&](std::size_t key) -> std::size_t {
            return cubic ? (key * 3 + 1) : key;
        };

        auto& target = pose.joint_locals[static_cast<std::size_t>(joint)];
        const std::size_t i1 = std::min(i0 + 1, s.times.size() - 1);

        if (ch.path == GltfTargetPath::kTranslation)
        {
            const auto a = detail::read_vec3(s.values, value_index(i0));
            const auto b = detail::read_vec3(s.values, value_index(i1));
            target.position = { a.x + (b.x - a.x) * alpha,
                                a.y + (b.y - a.y) * alpha,
                                a.z + (b.z - a.z) * alpha };
        }
        else if (ch.path == GltfTargetPath::kRotation)
        {
            const auto a = detail::read_quat(s.values, value_index(i0));
            const auto b = detail::read_quat(s.values, value_index(i1));
            target.rotation = detail::slerp_quat(a, b, alpha);
        }
        else if (ch.path == GltfTargetPath::kScale)
        {
            const auto a = detail::read_vec3(s.values, value_index(i0));
            const auto b = detail::read_vec3(s.values, value_index(i1));
            target.scale = { a.x + (b.x - a.x) * alpha,
                             a.y + (b.y - a.y) * alpha,
                             a.z + (b.z - a.z) * alpha };
        }
    }
}

}  // namespace cd::asset::gltf
