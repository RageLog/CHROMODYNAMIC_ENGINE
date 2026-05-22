# ADR-010 — Animation System

- **Status**: Accepted (Phase 1 Design)
- **Date**: 2026-05-17
- **Related**: ADR-004 (ECS), ADR-006 (Asset), ADR-015 (Concurrency), ADR-016 (Vendor Matrix)

## Bağlam

`cd::anim::` library-oriented; hybrid 2D+3D unified API. Hedef: UE5 AnimGraph + Lyra ALS + Motion Matching plugin, Unity Mecanim + Animation Rigging + Kinematica state-of-art **aşan**.

User decisions:
- T20.Q1 = state-of-art araştır (skeletal)
- T20.Q2 = D (hybrid HSM + blend tree + script)
- T20.Q3 = D (all IK layers)
- T20.Q4 = A (motion matching first-class)
- T20.Q5 = A (blendshape/morph first-class)
- T20.Q6 = state-of-art araştır (compression)

## Karar

### A. Skinning Strategy (T20.Q1)

**LBS default + DQS opt-in per-mesh + compute-shader skinning preferred**:
- **LBS** (Linear Blend Skinning, Magnenat-Thalmann 1988): matrix-palette, 4 influences/vertex (configurable up to 8); twist correctives mitigate candy-wrapper.
- **DQS** (Dual Quaternion, Kavan 2007): preserves volume on twist, ~1.3× LBS cost; per-mesh opt-in flag.
- **Compute shader skinning**: single dispatch per skinned mesh; transient vertex buffer consumed by raster. Skin once, draw N times (shadow + main + reflection). Morph target accumulation in same dispatch.
- **Vertex shader skinning fallback**: tile-based mobile + no-compute engines.

ozz-animation reference: CPU-only SoA SIMD. Aşma: GPU path + CPU available for RT BVH refits + physics cloth.

### B. Animation Graph (T20.Q2 = D)

**Hierarchical State Machine + Blend Trees + AnimGraph DSL** — UE-style.

```
cd::anim::AnimGraph
  ├── HSM (states + sub-state-machines)
  ├── BlendTree (1D/2D blendspace, additive layers, montages)
  └── AnimGraph DSL (node graph → compiled flat eval list, zero per-frame heap alloc)
```

DSL inspired by UE5 AnimGraph + Houdini KineFX. Authoring via data (JSON/YAML), runtime via C++ visitor. Small embedded script (Lua planned) drives blackboard updates without C++ rebuild.

**Inertialization** (Bollo GDC 2018) replaces crossfade as default transition — zero source-pose memory required, no double-eval cost.

Aşma: library-embeddable (no editor required) + motion matching first-class graph node.

### C. IK Stack (T20.Q3 = D) — 4 Layer

| Layer | Algorithm | Use case | Cost |
|---|---|---|---|
| L1 | **Two-bone analytical** (law of cosines) | Arms, legs, simple reach | ~50 ns/chain |
| L2 | **FABRIK** (Aristidou & Lasenby 2011) | Spines, tails, multi-joint chains | ~1-3 µs, 8 iter |
| L3 | **Full-body IK** (Jacobian transpose / CCD hybrid, joint constraints) | Cinematic poses, hand-on-prop | ~20-50 µs |
| L4 | **Procedural foot placement** (raycast + pelvis lower + foot-lock + L1 two-bone) | Locomotion grounding, stairs, slopes | ~5 µs/foot |

**Pipeline order**: `motion-matching → blend-tree → L3 full-body (opt) → L2 spine FABRIK → L4 foot → L1 hand reach → skinning`.

Aşma: UE5 Control Rig vs us = declarative chains in code/data, motion-matching-aware foot locking (anti-slide).

### D. Motion Matching (T20.Q4 = A first-class)

**Pipeline** (Clavet GDC 2016 + Zadziuk Ubisoft 2016 + UE5.4 Motion Matching plugin 2024):
1. **Offline**: tag unstructured mocap, extract trajectory features (3 future positions at 0.33/0.66/1.0 s, root velocity) + pose features (foot positions+velocities, hip velocity). Feature DB.
2. **Runtime**: every N frames (5-10), build query vector from gamepad-predicted trajectory + current pose. Nearest-neighbor search.
3. **Indexing**: SIMD brute-force scan (viable to ~10k frames) **or** KD-tree (UE5 uses bounded brute force with rejection). SIMD brute-force first, KD-tree only if profiling demands.
4. **Blending**: inertialization (no cross-fade DB).
5. **Pose warping / motion warping**: root-aligned correction so character hits gameplay targets.

Aşma: ALS Community (still state-machine + blend) ve UE5 plugin'dan **library-callable** olarak farklılaşır.

### E. Morph Targets / Blendshapes (T20.Q5 = A first-class)

GPU compute accumulation fused with skinning dispatch; sparse-delta storage (index + dx/dy/dz quantized 16-bit). Facial animation pipeline ready.

### F. Compression (T20.Q6) — ACL Vendor

**ACL** (Animation Compression Library, Nicholas Frechette, MIT):
- Variable-bitrate per-track quantization + per-track range reduction
- Constant/default sub-track detection (büyük savings on stable bones)
- Wavelet decomposition opsiyonel
- **~1-2 bits/sample/track** perceptually lossless quality
- Ships in UE5 as default (4.25+) — battle-tested
- C++11, no exceptions, no allocations hot path — fits CHROMODYNAMIC

`cd::anim::vendor::acl` (K2, replace yok). Abstraction: `cd::anim::ICompressor` interface for future research codec swap (Phase 5+).

**Reddedilen custom compression**: 12-24 ay ACL parity için, no shipping advantage.

## Reddedilen

- **Vertex-shader-only skinning** → GPU-driven culling entegrasyon kayıp.
- **Crossfade-only transitions** → inertialization strictly better.
- **State-machine-only locomotion (ALS-style)** → motion matching scales better with mocap volume.
- **Custom animation compression** → 1-2 year detour vs ACL parity, ROI yok.
- **Uncompressed clips** → memory budget kills.
- **Editor-required graph (UE asset)** → library-oriented violation.
- **Neural motion matching (Holden 2020)** → premature for v1, roadmap.

## Sonuçlar

**Pozitif**:
- Library-embeddable animation runtime, no editor lock-in.
- ACL parity with UE5 from day one.
- Motion matching first-class — For Honor / Matrix Awakens parity.
- ECS-native skinning matrix buffer → S4 render-batch integration.

**Negatif**:
- ACL external dep (mitigated: vendored, MIT, abstracted).
- Full-body IK most complex deliverable → Sprint 11 follow-up.
- Motion matching needs sizable mocap dataset to shine — ship with Epic's free "Game Animation Sample" CC0 starter.

**Replace-Ready (D1)**: `ICompressor` interface enables future codec swap; ACL stays vendored Phase 1-4, no replace planned.

## Açık Sorular

| ID | Soru | Çözüm |
|---|---|---|
| Q1 | Script language for AnimGraph blackboard: Lua / own bytecode / C++ only? | ADR-future scripting sprint |
| Q2 | KD-tree vs brute-force for >10k frames? | Profile-driven Sprint 10 |
| Q3 | Morph target count cap: 256 vs 512 vs 1024 (ARKit + custom)? | 512 default |
| Q4 | Cloth / secondary motion: own or defer to physics? | ADR-008 XPBD cloth covers |
| Q5 | Retargeting: IK-based (UE5 IK Rig) vs skeleton-mapping (ozz)? | Hybrid |
| Q6 | Determinism guarantee for netcode? | Deterministic SIMD ordering |
| Q7 | Graph editor: web/imgui ship or data-file only v1? | Data-file v1 |

## Cross-Cutting

- **ADR-004 (ECS)**: `cd::anim::SkeletonComponent` (skeleton ref + current pose SoA), `AnimatorComponent` (graph instance + blackboard), `SkinningMatrixBuffer` (GPU palette handle), `MorphWeightsComponent` (sparse weights). Update order: `AnimatorSystem → IKSystem → SkinningMatrixSystem → RenderSystem`.
- **ADR-015 (Concurrency)**: Animation graph eval per-entity-independent → trivially parallel via job system. Skinning compute dispatch GPU-side. Target: 1k animated characters at 0.5 ms CPU on 8 cores.
- **ADR-006 (Asset)**: `.cdanim` clip wraps ACL compressed blob + metadata (track names, root motion, sync markers). `.cdskel` skeletons. `.cdgraph` AnimGraph definitions (JSON authoring, compiled binary blob). Morph deltas sparse (index + dx/dy/dz 16-bit).

## Kanıt

- ACL: github.com/nfrechette/acl (MIT, Nicholas Frechette)
- ozz-animation: github.com/guillaumeblanc/ozz-animation
- UE5 Motion Matching plugin: https://dev.epicgames.com/documentation/en-us/unreal-engine/motion-matching-in-unreal-engine
- Clavet (2016) — Motion Matching For Honor GDC — **STUB**
- Aristidou & Lasenby (2011) — FABRIK, Graphical Models — **STUB**
- Kavan et al. (2007) — Skinning with Dual Quaternions I3D — **STUB**
- Bollo (2018) — Inertialization Gears of War GDC — **STUB**
- Frechette (2017-2024) — ACL technical blog series: https://nfrechette.github.io/
- Magnenat-Thalmann et al. (1988) — Joint-Dependent Local Deformations GI'88 — **STUB**
