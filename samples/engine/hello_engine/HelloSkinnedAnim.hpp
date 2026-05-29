// =============================================================================
// HelloSkinnedAnim.hpp
// -----------------------------------------------------------------------------
// hello_engine-local per-frame CPU-LBS skinning step. Lifted out of
// main() in Marathon Run 11 phase N10.
//
// When the loaded asset carries skin + animation data (CesiumMan ships
// both), this samples the first animation track at current time, builds
// the matrix palette via cd::anim::compute_skinning_matrices, CPU-skins
// every vertex with 4-weight LBS, and re-uploads the deformed
// PrimitiveVertex buffer to the GPU vertex buffer. The existing prim
// pipeline then draws the deformed character without needing a separate
// skinned-vertex pipeline.
//
// CesiumMan has ~3k vertices so the per-frame cost stays well under
// 1 ms on a modern CPU.
//
// SK-fix preserved: skin_joint_remap translates vertex JOINTS_0
// (skin-joint indices) to skeleton-joint indices before palette lookup.
// Without this the limbs render twisted.
//
// The W4-F fallback turntable (for assets without skin data) stays
// inline in main() because it touches entity transforms / scene
// locals that are not part of skinned state.
// =============================================================================
#pragma once

#include "HelloSkinned.hpp"

#include <cd/anim/Animation.hpp>
#include <cd/anim/Skeleton.hpp>
#include <cd/asset/Primitives.hpp>
#include <cd/asset_gltf/SkinnedMeshBridge.hpp>
#include <cd/math/Matrix.hpp>
#include <cd/math/Vector.hpp>
#include <cd/rhi/IDevice.hpp>

#include <cstddef>
#include <cstdint>
#include <span>

namespace cd_sample {

// ---- update_skinned_animation ---------------------------------------------
// Advances animation time + CPU-LBS-skins + uploads deformed verts.
// Returns true if the skinned path ran; false if the asset has no skin
// data or no valid vertex buffer (caller may then run the fallback
// turntable).
inline bool
update_skinned_animation(SkinnedRuntime& skinned,
                         cd::rhi::IDevice& device,
                         cd::rhi::BufferHandle gltf_vb,
                         float dt)
{
    if (!skinned.valid || !gltf_vb.is_valid())
        return false;

    // Advance + loop animation time.
    skinned.anim_t += dt;
    if (skinned.animation.duration > 0.0F)
    {
        while (skinned.anim_t > skinned.animation.duration)
            skinned.anim_t -= skinned.animation.duration;
    }
    // Reset to bind pose then overlay the animation channels;
    // joints without an animation track stay at their bind position
    // (correct glTF sampling semantics).
    skinned.pose = cd::anim::Pose::bind_pose(skinned.skeleton);
    cd::asset_gltf::sample_gltf_animation(
        skinned.animation,
        skinned.node_to_joint,
        skinned.anim_t,
        skinned.pose);
    // Per-joint skinning matrices = world(pose) * inverse_bind.
    cd::anim::compute_skinning_matrices(
        skinned.skeleton, skinned.pose, skinned.palette_scratch);

    // CPU-skin every vertex.
    const auto& bone_palette = skinned.palette_scratch;
    const std::size_t nv = skinned.base_positions.size();
    for (std::size_t i = 0; i < nv; ++i)
    {
        const auto& inf = skinned.influences[i];
        float w_sum = inf.weights[0] + inf.weights[1]
                    + inf.weights[2] + inf.weights[3];
        if (w_sum < 1e-5F)
            w_sum = 1.0F;
        const float inv_w = 1.0F / w_sum;
        cd::math::Mat4f skin_mat {};  // zero
        for (std::size_t k = 0; k < 4; ++k)
        {
            const std::uint16_t skin_joint = inf.joints[k];
            const float w = inf.weights[k] * inv_w;
            if (w <= 0.0F)
                continue;
            // SK-fix: translate skin-joint index to skeleton-joint index.
            if (skin_joint >= skinned.skin_joint_remap.size())
                continue;
            const std::int32_t sj = skinned.skin_joint_remap[skin_joint];
            if (sj < 0 || sj >= static_cast<std::int32_t>(bone_palette.size()))
                continue;
            const auto& m = bone_palette[static_cast<std::size_t>(sj)];
            for (std::size_t c = 0; c < 4; ++c)
                for (std::size_t r = 0; r < 4; ++r)
                    skin_mat[c][r] += w * m[c][r];
        }
        const auto& bp = skinned.base_positions[i];
        const auto& bn = skinned.base_normals[i];
        cd::math::Vec4f p4 {
            skin_mat[0][0] * bp.x + skin_mat[1][0] * bp.y
                + skin_mat[2][0] * bp.z + skin_mat[3][0],
            skin_mat[0][1] * bp.x + skin_mat[1][1] * bp.y
                + skin_mat[2][1] * bp.z + skin_mat[3][1],
            skin_mat[0][2] * bp.x + skin_mat[1][2] * bp.y
                + skin_mat[2][2] * bp.z + skin_mat[3][2],
            skin_mat[0][3] * bp.x + skin_mat[1][3] * bp.y
                + skin_mat[2][3] * bp.z + skin_mat[3][3]
        };
        cd::math::Vec3f n3 {
            skin_mat[0][0] * bn.x + skin_mat[1][0] * bn.y
                + skin_mat[2][0] * bn.z,
            skin_mat[0][1] * bn.x + skin_mat[1][1] * bn.y
                + skin_mat[2][1] * bn.z,
            skin_mat[0][2] * bn.x + skin_mat[1][2] * bn.y
                + skin_mat[2][2] * bn.z
        };
        auto& out = skinned.deformed_scratch[i];
        out.pos[0]    = p4.x;
        out.pos[1]    = p4.y;
        out.pos[2]    = p4.z;
        out.normal[0] = n3.x;
        out.normal[1] = n3.y;
        out.normal[2] = n3.z;
        out.uv[0]     = skinned.base_uvs[i].x;
        out.uv[1]     = skinned.base_uvs[i].y;
        out.color[0]  = 0.85F;
        out.color[1]  = 0.82F;
        out.color[2]  = 0.78F;
    }
    (void)device.upload_buffer(
        gltf_vb,
        0,
        std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(
                skinned.deformed_scratch.data()),
            skinned.deformed_scratch.size()
                * sizeof(cd::asset::PrimitiveVertex)));
    return true;
}

}  // namespace cd_sample
