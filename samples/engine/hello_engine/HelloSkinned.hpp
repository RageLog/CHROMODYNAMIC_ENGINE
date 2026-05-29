// =============================================================================
// HelloSkinned.hpp
// -----------------------------------------------------------------------------
// hello_engine-local POD aggregate carrying the CPU-LBS skinning runtime
// state for a glTF skinned asset (CesiumMan-style). Lifted out of main()
// in Marathon Run 11 phase N9-prep so the per-frame skinning loop and
// the TLAS rebuild can share the same type without two ~3000-line
// captures.
//
// Scope rule: parallel arrays (positions / normals / uvs / influences)
// MUST stay the same length — same vertex layout assumption as
// cd::asset::Primitives. The scratch buffers are pre-sized in the load
// path and re-used every frame to avoid allocator churn.
//
// This type intentionally has no methods. The per-frame transform comes
// from cd::anim::compute_skinning_matrices + a 4-weight LBS in main().
// =============================================================================
#pragma once

#include <cd/anim/Animation.hpp>
#include <cd/anim/Skeleton.hpp>
#include <cd/asset/Primitives.hpp>
#include <cd/asset/gltf/GltfLoader.hpp>
#include <cd/asset/gltf/SkinnedMeshBridge.hpp>
#include <cd/math/Matrix.hpp>
#include <cd/math/Vector.hpp>

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace cd_sample {

// SK4: skinned-mesh state captured at gltf load (CesiumMan-style
// assets). When valid, the per-frame loop CPU-skins the source vertices
// via cd::anim::compute_skinning_matrices + a 4-weight LBS and
// re-uploads them to gltf_mesh.vb so the existing prim pipeline draws
// the deformed character without needing a separate skinned-vertex
// pipeline.
struct SkinnedRuntime
{
    bool valid { false };
    cd::anim::Skeleton skeleton {};
    std::unordered_map<int, std::int32_t> node_to_joint {};
    // SK-fix: vertex JOINTS_0 hold skin-joint indices, NOT skeleton-joint
    // indices. Skin joint i -> skeleton joint skin_joint_remap[i].
    // Without this remap CPU-LBS reads the wrong matrix and the
    // character renders as a twisted mess.
    std::vector<std::int32_t> skin_joint_remap;
    cd::asset::gltf::GltfAnimation animation {};
    // Per-vertex source data (bind-pose positions + normals + uvs +
    // bone influences). Parallel arrays — same length.
    std::vector<cd::math::Vec3f> base_positions;
    std::vector<cd::math::Vec3f> base_normals;
    std::vector<cd::math::Vec2f> base_uvs;
    std::vector<cd::asset::gltf::GltfSkinVertex> influences;
    // Scratch buffers reused per frame.
    std::vector<cd::math::Mat4f> palette_scratch;
    std::vector<cd::asset::PrimitiveVertex> deformed_scratch;
    cd::anim::Pose pose {};
    float anim_t { 0.0F };
};

}  // namespace cd_sample
