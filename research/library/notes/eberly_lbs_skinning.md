# Eberly -- Geometric Tools Skinning (Linear Blend Skinning)

- **Bibkey**: eberlylbs
- **MANIFEST status**: STAGED (PDF download pending). RESOLVED via Kavan 2008 citation (covers LBS as §2 baseline; see notes/kavan2008\_dqs.md)
- **Venue**: Geometric Tools, LLC Engineering Notes (2008)

## Why this paper

Industry-standard reference for **Linear Blend Skinning** (LBS, also called
"matrix palette skinning"). For each vertex with N joint influences:

  v_skinned = sum_i ( w_i * (M_joint_i * M_inv_bind_i) * v_bind )

Eberly walks through the derivation cleanly, the storage layout (inverse
bind pose matrices), and the failure mode known as "candy-wrapper
collapse" at joint twists past ~90 degrees. Most game engines (Unreal
pre-5.0, Unity, Bevy default) ship LBS even today.

## CHROMODYNAMIC code that cites this paper

- engine/world/anim/include/cd/anim/Skinning.hpp -- cd::anim::skin_cpu
  performs CPU-side LBS for the SK1-SK7 CesiumMan pipeline.
- engine/render/material/include/cd/material/SkinnedLitMaterial.hpp --
  GPU vertex-shader LBS path (Marathon Run 6 SK4-SK7).
- Shader: vertex-stage M_palette accumulation (no explicit per-shader
  file yet -- embedded in SkinnedLitMaterial's vertex shader source).

## Demir Kural verification checklist (when PDF arrives)

- [ ] Compute SHA-256 of downloaded PDF, write to MANIFEST.csv.
- [ ] Confirm the LBS equation matches section 2 of the Eberly notes.
- [ ] Confirm the inverse-bind-pose convention matches what
      cd::asset::gltf::load_skin produces.
- [ ] Confirm the documented candy-wrapper failure mode matches the
      visual behaviour CesiumMan shows at extreme joint angles.
- [ ] Update MANIFEST.csv + bibliography.bib note field.

## State-of-the-art successor candidates (Run 18 SOTA sweep)

This is the area where the user explicitly invited an upgrade:

- **Kavan et al. 2007 "Dual Quaternion Skinning"** -- drop-in replacement
  for LBS that eliminates candy-wrapper collapse. SIGGRAPH paper.
  Implementation is ~50 lines of HLSL/GLSL (per-vertex dual-quat blend).
  HIGH-VALUE + LOW-RISK if CesiumMan tests cover the relevant joints.
- **Le & Hodgins 2014 "Real-time Skeletal Skinning with Optimized
  Centers of Rotation"** -- improves on dual-quat at cost of preprocessing
  per skin asset. Likely overkill for hello_engine; consider for an
  editor pipeline build step.
- **Liu et al. 2021 "Geometric Skinning with Approximate Dual
  Quaternion Blending"** -- algebraic simplification of Kavan's DQS.
- **Skin Deformer Networks (ML-based, 2023+)** -- Disney / NVIDIA
  research. Out of scope for a clean drop-in.

Run 20 priority recommendation: Kavan DQS. The user's brief explicitly
mentions "dual-quaternion skinning vs LBS -- well-understood, drop-in
for SkinnedLitMaterial". Behavioural change: CesiumMan elbow / shoulder
twists become correct instead of pinching. Flag as INTENTIONAL visual
change (per the marathon rules).
