# cd::anim

**Purpose**: animation runtime -- skeleton + keyframe channels + pose + LBS skinning + blend trees + state machines. CPU LBS today; GPU compute skinning queued behind cd::anim::GpuSkinning.

**Namespace**: `cd::anim`.

**Headers**: `cd/anim/{Skeleton,Animation,CurveTrack,BlendTree2,StateMachine,Pose,PoseBlend,PoseAlign,AdditiveBlend,BoneMask,BoneSocket,GpuSkinning,EventTrack}.hpp`.

**Primary types**:
- `cd::anim::Skeleton` -- joint hierarchy + bind pose + name table.
- `cd::anim::Animation` -- channels of CurveTrack keyframes (translation / rotation / scale per joint).
- `cd::anim::Pose` -- per-frame joint transforms; supports bind_pose factory + composition.
- `cd::anim::compute_skinning_matrices(pose, skel)` -- bind-inverse * world; feeds the LBS kernel.
- `cd::anim::BlendTree2` / `cd::anim::StateMachine` -- gameplay-driven animation blending.
- `cd::anim::BoneSocket` -- attach world-space transforms to joints (weapon-bone, fx-bone).

**Test command**: `ctest --preset ninja-debug -R cd_test_anim --output-on-failure`.

**Notes**:
- hello_engine wraps the per-frame CPU-LBS step in `cd_sample::update_skinned_animation` (HelloSkinnedAnim.hpp / Marathon Run 11 phase N10).
- CesiumMan demo: 22-bone skeleton, 1 idle animation, ~16k vertices LBS-deformed per frame.
- GpuSkinning header reserved for the planned compute-shader path; currently no-op.
