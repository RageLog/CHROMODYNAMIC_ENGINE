// =============================================================================
// CHROMODYNAMIC — cd/anim/GpuSkinning.hpp
// Phase 168 / v0.99.89 — GPU-side skinning helpers (vertex shader + UBO).
//
// Bridges the CPU-side cd::anim primitives (Skeleton, Pose,
// compute_skinning_matrices) to a GPU vertex pipeline that does
// the per-vertex skin transform on the GPU.
//
// What this header provides:
//   * `SkinningMatricesUbo` — std140-packed UBO record. Holds up to
//     kMaxBones (256) joint matrices.
//   * `pack_skinning_matrices(skel, pose, out)` — convenience wrapper
//     around `compute_skinning_matrices` that writes the result into
//     the UBO record's array.
//   * `kSkinningVertexShaderGlsl` — canonical 4-weight skinning
//     vertex shader as a string constant. The skinned-mesh sample
//     compiles this verbatim.
//   * `SkinnedVertex` — POD struct describing the engine's standard
//     skinned-vertex layout: position + normal + uv + bone_ids[4]
//     + bone_weights[4].
//
// Convention: 4 bone influences per vertex (matches every game
// engine + glTF skinning spec). The vertex shader normalises the
// weights so artists can hand-author non-normalized influences;
// importers usually do it offline but the cost is one extra
// dot+divide per vertex.
//
// Reference: Khronos glTF 2.0 skin chapter; Frostbite "Moving to
// PBR" 2014 §7; production Animation programming guide.
// =============================================================================
#pragma once

#include <cd/anim/Skeleton.hpp>
#include <cd/core/Defines.hpp>
#include <cd/math/Matrix.hpp>
#include <cd/math/Vector.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>

namespace cd::anim
{

/// Maximum bones per skinning UBO. 256 covers every animated
/// character asset shipped in mainstream games (default is
/// 256; production engine's `BlendShape` limit is 254). Larger skeletons split
/// into multiple draws.
inline constexpr std::uint32_t kMaxBones = 256;

/// One vertex of the canonical skinned-mesh layout. Matches glTF
/// 2.0's mandatory attributes for skinned meshes (POSITION, NORMAL,
/// TEXCOORD_0, JOINTS_0, WEIGHTS_0). 64 bytes.
struct SkinnedVertex
{
    cd::math::Vec3f position;                          // 12
    float           pos_pad { 0.0F };                 // +4 → 16
    cd::math::Vec3f normal;                            // 12
    float           nrm_pad { 0.0F };                 // +4 → 16
    cd::math::Vec2f uv;                                // 8
    std::uint16_t   bone_ids[4]     { 0, 0, 0, 0 };    // 8 → 16
    float           bone_weights[4] { 1.0F, 0.0F, 0.0F, 0.0F };  // 16
};

static_assert(sizeof(SkinnedVertex) == 64, "SkinnedVertex must be 64 B (4×16-byte rows)");

/// Per-frame skinning UBO. std140-packed mat4 array.
/// Total size: 256 × 64 = 16384 B = 16 KB, fits every GPU's
/// guaranteed UBO size cap (Vulkan minimum is 16384).
struct SkinningMatricesUbo
{
    cd::math::Mat4f bones[kMaxBones];
};

/// Convenience: compute skinning matrices for `pose` and write them
/// into `out.bones[0..joint_count]`. Joints beyond `joint_count` are
/// left at identity (vertex shader won't touch them given they're
/// not referenced by any vertex). Returns the number of joints
/// actually written.
inline std::uint32_t pack_skinning_matrices(const Skeleton& skel,
                                            const Pose& pose,
                                            SkinningMatricesUbo& out)
{
    // Fill identity defaults for unused slots so any stray
    // bone_id beyond joint_count produces zero displacement.
    for (auto& m : out.bones) m = cd::math::Mat4f::identity();

    if (skel.joint_count() == 0) return 0;
    std::vector<cd::math::Mat4f> scratch;
    compute_skinning_matrices(skel, pose, scratch);
    const auto n = std::min(static_cast<std::uint32_t>(scratch.size()), kMaxBones);
    for (std::uint32_t i = 0; i < n; ++i) out.bones[i] = scratch[i];
    return n;
}

/// Canonical 4-weight linear-blend skinning vertex shader. Reads
/// the SkinningMatricesUbo at binding (set=0, binding=0); the
/// non-skinned uniform (MVP) lives in push constants. Output is
/// world-space position + transformed normal + uv passthrough.
///
/// Includes weight renormalisation so non-normalized inputs still
/// produce a unit blend (avoids the "skin collapse" artifact when
/// an importer ships unnormalized weights).
inline constexpr const char* kSkinningVertexShaderGlsl = R"glsl(
#version 460

layout(location = 0) in vec3  a_pos;
layout(location = 1) in vec3  a_normal;
layout(location = 2) in vec2  a_uv;
layout(location = 3) in uvec4 a_bone_ids;     // 4× uint16 packed as uint
layout(location = 4) in vec4  a_bone_weights;

layout(push_constant) uniform Push {
  mat4 mvp;
  mat4 model;
} pc;

layout(set = 0, binding = 0) uniform Skinning {
  mat4 bones[256];
} skinning;

layout(location = 0) out vec3 v_world_pos;
layout(location = 1) out vec3 v_world_normal;
layout(location = 2) out vec2 v_uv;

void main() {
  // Normalize weights (defensive).
  float w_sum = max(1e-5, dot(a_bone_weights, vec4(1.0)));
  vec4  w     = a_bone_weights / w_sum;

  // 4-weight linear-blend skinning.
  mat4 skin = w.x * skinning.bones[a_bone_ids.x]
            + w.y * skinning.bones[a_bone_ids.y]
            + w.z * skinning.bones[a_bone_ids.z]
            + w.w * skinning.bones[a_bone_ids.w];

  vec4 pos_skin    = skin * vec4(a_pos, 1.0);
  vec4 normal_skin = skin * vec4(a_normal, 0.0);

  v_world_pos    = (pc.model * pos_skin).xyz;
  v_world_normal = normalize(mat3(pc.model) * normal_skin.xyz);
  v_uv           = a_uv;
  gl_Position    = pc.mvp * pos_skin;
}
)glsl";

}  // namespace cd::anim
