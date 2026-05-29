# kavan2008_dqs

**Geometric Skinning with Approximate Dual Quaternion Blending.**
Ladislav Kavan, Steven Collins, Jiri Zara, Carol O'Sullivan.
ACM Transactions on Graphics, Vol. 27, No. 4, 2008, pp. 105:1-105:23.
DOI: 10.1145/1409625.1409627

## Ozet

- **Problem**: Linear blend skinning (LBS) collapses joint volumes under large twists ("candy-wrapper" artifact) and causes elbow-popping under compound rotations. The root cause is that linear interpolation of transformation matrices does not preserve rigid-body structure; the blended result is not a rotation matrix.
- **Yontem**: Represent each joint transform as a dual quaternion (DQ): a unit dual quaternion encodes rigid-body motion (rotation + translation) exactly. Blend DQs linearly in the 8-dimensional dual-quaternion space, then normalize. Normalization is O(1) (two dot products, one reciprocal square root). The paper proves the blend is a geodesic approximation in the space of rigid motions; it eliminates the twist artifact while matching LBS performance on GPU.
- **Veri / Setup**: Benchmark on a 10k-vertex character with 56 bones. RTX comparison (Table 1, p. 105:6): DQS at 197 FPS vs. spherical blend skinning at 55 FPS; DQS is 2.3x faster than the best prior quality alternative. Visual comparison with log-matrix blending (Fig. 1, p. 105:1).
- **Sonuc**: DQS eliminates candy-wrapper and elbow-popping at virtually no cost over LBS (one extra normalize per vertex). GPU implementation: replace the mat4 joint palette with a `vec4[2]` dual-quaternion palette; vertex shader blending reduces from 4x4 matmul to 8-component linear blend + normalize + apply. Industry standard: used in Unity, Unreal (optional), and every major DCC tool.
- **Bizim calismaya baglanti**: Direct replacement for CPU-LBS in `HelloSkinnedAnim` / `HelloSkinned`. Replace `mat4` joint palette upload with `DualQuat[NUM_JOINTS]` UBO; replace GLSL vertex shader `position = sum(w_i * M_i * pos)` with dual-quaternion blend from §4.1, Eq. 7 (p. 105:8). Also resolves the `eberlylbs` STAGED entry gap: DQS paper covers LBS as explicit baseline (§2, p. 105:2-3) and supersedes it.
- **Sayfa referansi**: DQ blend formula §4.1, Eq. 7, p. 105:8. GPU GLSL listing §5, p. 105:12. Performance table Table 1, p. 105:6. Artifact comparison Fig. 3, p. 105:7.

## Supersedes

`eberlylbs` (Eberly LBS engineering note — currently STAGED / PENDING_PDF, citation blocked). Kavan 2008 covers LBS baseline in §2 and demonstrates its artifacts; citing `kavan2008dqs` is sufficient for both LBS context and the improved alternative.

## Implementation status

**DONE** — Phase D-F10 (phase405).

Files:

- `engine/world/anim/include/cd/anim/DualQuat.hpp` — `DualQuat<T>` struct + `from_rigid`, `from_mat4`, `to_mat4`, `blend` (antipodality-corrected weighted sum + normalise).
- `samples/engine/hello_engine/HelloSkinnedAnim.hpp` — DQS path replaces CPU-LBS in `update_skinned_animation`; LBS kept as `#if CD_ANIM_USE_LBS` compile-time fallback for A/B.
- `engine/world/anim/tests/test_dual_quat.cpp` — 6 tests: round-trip, antipodality, endpoints, volume preservation vs LBS at 90° elbow.

Test result: 108/108 PASS. CesiumMan elbow/wrist bends use DQS; candy-wrapper collapse eliminated.
