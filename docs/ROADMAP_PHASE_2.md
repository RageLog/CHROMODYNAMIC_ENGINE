# CHROMODYNAMIC — Phase 2 Architecture Roadmap

> **Status**: Architect draft, 2026-06-08. **Scope**: research + design.
> No code, no ADRs written from this doc. ADRs proposed at end of each
> deep-dive land only after user approves the roadmap.
> **Charter**: cross-platform / cross-API library-oriented hybrid 2D+3D
> engine; SOTA target = Filament / bgfx / EnTT / Bevy AŞILACAK; modern
> C++23; every library standalone-consumable.
> **Snapshot context**: Marathon Runs 7-9 era, post-Phase 132+ Vulkan
> AS-build cmd path + Phase 2 concurrency (parallel TLAS, boot bake,
> ECS prep, CSM/planar caster prep).
> **Authoritative references**: `docs/STATUS_AND_PLAN_W8.md` §4,
> `docs/ADR/ADR-20260528-job-system-design.md` (Sonuclar + Addendum),
> `docs/ADR/ADR-20260529-X4-d3d12-parity-status.md`,
> `docs/ADR/ADR-20260529-X5-shader-on-disk-hot-reload.md`.

Roadmap captures three queued Phase 2 architecture milestones (X4, X5,
X1-FU-F), expands their scope into DAGs with owner-agent assignments,
and proposes execution ordering. ADR titles at the end of each deep-dive
are placeholders for the next architect-team meeting.

---

## §1. Phase 2 milestone catalog

Excerpt from `STATUS_AND_PLAN_W8.md` §4 (NEXT tier) plus the X1
follow-up board in `ADR-20260528-job-system-design.md` Sonuclar.

| ID        | Name                                       | Status this snapshot | Blocker / pre-req                                                  | Owner-agent recommendation (lead → support)                          |
| --------- | ------------------------------------------ | -------------------- | ------------------------------------------------------------------ | -------------------------------------------------------------------- |
| **SL**    | SOTA shader library (cd::shader_library)   | ACTIVE (user-priority 2026-06-12) | SOTA research (running); X4-A toolchain ADR cross-input | researcher → architect → developer × N → tester |
| **X4**    | D3D12 backend parity + RT                  | QUEUED (3-4 weeks)   | RHI surface stable; ~~image-readback API~~ ✅ phase1127; SPIRV-Cross or DXIL path   | architect → researcher → developer × N → tester → build-devops       |
| **X5**    | Shader on-disk + hot-reload                | QUEUED (1 week)      | `cd::shader::FileWatcher` + `cd::shader::ICompiler` already live   | architect → developer → tester → doc-writer                          |
| **X1-FU-F** | Vulkan secondary command buffer pipeline | QUEUED (~1 week)     | `cd::rhi::ICommandBuffer` surface review; 4-backend impl needed    | architect → safety-integration → developer × 4 → tester              |
| X1-FU-A   | `cv` → `std::atomic::wait/notify_one`      | Polish (1-2 days)    | X1-FU-B (TSan run) green                                           | safety-integration → developer → tester                              |
| X1-FU-B   | TSan preset run                            | Polish (1 day)       | X3 CI multi-runner matrix                                          | build-devops → tester                                                |
| X1-FU-C   | Hazard-pointer reclamation (WSD)           | Polish (3-5 days)    | none                                                               | safety-integration → developer → tester                              |
| X1-FU-D   | Priority-aware steal ordering              | Polish (2-3 days)    | none                                                               | developer → tester                                                   |
| X1-FU-E   | `concurrency/README.md`                    | Polish (≤1 day)      | none                                                               | doc-writer                                                           |
| X1-FU-G   | `IDevice::upload_buffer` thread-safety     | Spec audit (2 days)  | none                                                               | safety-integration → architect                                       |
| X1-FU-H   | Parallel ECS scale re-measurement          | After X7 lands       | X7 (ECS v2 archetype)                                              | tester → analyst                                                     |
| X1 Phase 2| WSL integration into render/asset/ECS      | 2-3 weeks            | X4 + X7                                                            | architect → safety-integration → developer × 3                       |

The three milestones in this roadmap (**X4 / X5 / X1-FU-F**) sit at the
intersection of "needed by X1 Phase 2 integration" and "blocks the
cross-API, library-oriented, A++ claim". X1-FU-A..H are *polish*
follow-ups against the already-Accepted job system (ADR-20260528) and
are not redesigned here — only listed for routing.

---

## §2. X4 — D3D12 backend parity + RT pipeline

### 2.1 Scope (what shipped vs what's queued)

**Shipped at this snapshot** (per ADR-20260529-X4-d3d12-parity-status.md
Run 28 audit):

- 2906-line `D3D12Device.cpp` already substantive (~73 % parity vs
  Vulkan's 3989-line backend).
- 3 D3D12 samples (`hello_d3d12_boot`, `hello_d3d12_clear`,
  `hello_d3d12_triangle`) verified live.
- CPU-side parity smoke (`test_backend_parity.cpp`, 8 cases) gates the
  enum / handle / struct surface against silent drift.

**Queued (this roadmap)** — 5 kNotImplemented sites in `D3D12Device.cpp`:

| # | Site                                       | Vulkan equivalent                          | Severity      |
| - | ------------------------------------------ | ------------------------------------------ | ------------- |
| 1 | `create_texture` k1D / k3D                 | `vkCreateImage` 1D/3D                      | Medium        |
| 2 | `create_texture_view` non-2D / non-cube    | `vkCreateImageView` 3D / array             | Medium        |
| 3 | `update_descriptor_set` rare types         | full descriptor surface                    | Low           |
| 4 | `submit(SubmitDesc)` semaphore-based       | `vkQueueSubmit2` w/ timeline + binary sema | High          |
| 5 | `create_acceleration_structure` (DXR)      | `vkCmdBuildAccelerationStructuresKHR`      | High (RT)     |

Plus the **hello_d3d12_pbr** sample (gold-compare-able PBR cube +
ImGui + bloom) that the M4 milestone calls out.

### 2.2 Prerequisites (must be in tree before X4 starts)

1. **SPIRV-Cross integration** — current GLSL → DXIL path is undefined.
   Either pull `spirv-cross` (Tier-B FetchContent per ADR-016) or
   commit to native DXIL via DirectXShaderCompiler. Decision input for
   X4's first ADR.
2. **Image-readback API** in `cd::rhi::IDevice` — ✅ DONE (phase1127;
   the API itself shipped at phase377). `copy_image_to_buffer` exists
   in all 3 backends; phase1127 added `ImageRegion::src_state` (the
   legacy kUndefined transition could legally DISCARD rendered
   contents on Vulkan), fixed the D3D12 path to de-pitch its 256-byte
   row alignment into the contract's tightly-packed layout, and added
   the GPU clear-colour round-trip test running the SAME body against
   Vulkan and D3D12 (test_image_readback.cpp, X4-B gate).
3. **NVIDIA self-hosted CI lane (X3)** — D3D12 runtime tests can't run
   on GitHub-hosted Linux runners. Hardware unavailability is an
   explicit X4 honest-scope flag.
4. **ImGui DX12 backend vendoring** — `hello_d3d12_pbr` needs ImGui;
   only Vulkan backend is currently vendored.

### 2.3 Sub-task DAG

```text
                    X4-A (architect)
                    │  ADR: shader-toolchain-decision
                    │  (SPIRV-Cross-glue lib vs native DXC)
                    │
       ┌────────────┼────────────────────────┐
       │            │                        │
   X4-B            X4-C                   X4-D
   image-readback  spirv-cross-glue       ImGui DX12 vendor
   API (RHI surf)  library (rhi tier B)   (build-devops + ui-architect)
   architect       researcher → developer
       │            │                        │
       └────────────┴────────────┬───────────┘
                                 │
              ┌──────────────────┼────────────────┐
              │                  │                │
          X4-E1               X4-E2            X4-E3
          1D/3D textures      SubmitDesc       DXR pipeline
          + views             semaphore path   + AS build (per
          developer           developer        Phase 132 Vk path
          (rhi_d3d12)         (rhi_d3d12)      mirrored to D3D12)
                                               developer + safety-integration
              │                  │                │
              └──────────────────┴────────────────┘
                                 │
                              X4-F
                              hello_d3d12_pbr sample
                              developer (samples + shader glue)
                                 │
                              X4-G
                              parity golden test
                              tester (FLIP / SSIM 1e-3 tol)
                                 │
                              X4-H
                              CI matrix hookup
                              build-devops (X3 NVIDIA lane on-prem)
```

Critical-path: X4-A → (X4-B parallel X4-C parallel X4-D) → X4-E* in
parallel → X4-F → X4-G → X4-H.

### 2.4 Test gate

- `test_backend_parity.cpp` stays green (regression net for §2.1
  CPU-side surface).
- New: `test_rhi_image_readback.cpp` — readback parity (Vulkan vs D3D12
  vs Null) for a 256×256 RGBA8 + a 64×64×64 R32F volume.
- New: `test_d3d12_pbr_golden.cpp` — FLIP ≤ 1e-3 against
  `hello_engine` chrome demo at fixture #5 (chrome-Sponza). Gated on
  X3 lane (`allow_failure: true` until hardware lands).
- TSan unaffected; D3D12 backend has no new thread-shared state.

### 2.5 Owner-agents

| Sub-task | Lead                  | Support                          | Why                                                                |
| -------- | --------------------- | -------------------------------- | ------------------------------------------------------------------ |
| X4-A     | architect             | researcher                       | Toolchain choice = irreversible; SOTA scan SPIRV-Cross vs DXC      |
| X4-B     | architect             | developer (rhi)                  | New `IDevice` surface = interface change                           |
| X4-C     | researcher            | developer                        | SPIRV-Cross integration is a Tier-B FetchContent investigation     |
| X4-D     | build-devops          | ui-architect                     | Vendor + CMake gate; UI input on ImGui backend choice              |
| X4-E1/2/3| developer × 3 parallel| safety-integration (E3 DXR only) | Pure impl; DXR needs concurrency review (AS build + cmd lists)     |
| X4-F     | developer             | ui-developer                     | Sample shipping + ImGui panel                                      |
| X4-G     | tester                | analyst                          | Golden capture + regression net                                    |
| X4-H     | build-devops          | release-manager                  | CI YAML + on-prem runner registration                              |

### 2.6 Recommended ADRs for X4

- **ADR-20260608-x4-shader-toolchain-decision** (Iglberger)
  *Context*: GLSL → DXIL crossing requires a path; SPIRV-Cross-glue
  library vs native DirectXShaderCompiler. *Reddedilen alternatifler*:
  fork two source trees per backend; DXC-only (lose Vulkan reuse);
  SPIRV-Cross-only (lose HLSL author affordance).
- **ADR-20260608-x4-rhi-image-readback-api** (Iglberger)
  *Context*: Golden-compare parity needs image readback; absent today.
  Designs `IDevice::map_texture` vs `IDevice::copy_texture_to_buffer`
  vs an async future-returning variant.
- **ADR-20260608-x4-d3d12-dxr-pipeline-shape**
  Mirrors ADR-20260523-wave163-v0.41.0-phase14g-rt-api-shape into the
  D3D12 implementation; locks SBT layout and `create_acceleration_structure`
  contract symmetric with `rhi_vulkan`.
- **ADR-20260608-x4-submit-desc-semaphore-semantics** (only if
  cross-backend semaphore semantics diverge during X4-E2 — see §6 Q3).

---

## §3. X5 — Shader on-disk + hot-reload

### 3.1 Scope

Move GLSL from `inline constexpr const char*` literals (~990 lines
embedded in `samples/engine/hello_engine/PrimShader*.hpp`) onto disk
as `.vert.glsl` / `.frag.glsl` files. Add a `Material::recreate` path
keyed off `cd::shader::FileWatcher` so editing a shader during a live
session re-builds the pipeline within ~1 frame, with old RHI handles
released only after `wait_idle`.

This is the *smallest* of the three milestones (sized 1 week) and
the most leveraged for developer-experience: editing the tonemap
operator in the composite shader without restarting `hello_engine`
collapses the iteration loop from ~30 s to <1 s.

### 3.2 Prerequisites

1. `cd::shader::FileWatcher` (header at
   `engine/render/shader/include/cd/shader/FileWatcher.hpp`) — already
   present at this snapshot.
2. `cd::shader::ICompiler` (glslang-backed) — already present.
3. `cd::material::Material::create(MaterialDesc&&)` — present; new
   recreate path is additive, not breaking.
4. `cd::vfs` — present but **rejected** for V1 (see §3.6 / R4).

### 3.3 Sub-task DAG

```text
              X5-A (architect)
              │  ADR: file-source variant on MaterialDesc
              │       + recreate contract
              │
      ┌───────┴────────┐
      │                │
   X5-B             X5-C
   shader move      Material::recreate impl
   embedded → disk  + path resolution
   developer        developer (material lib)
   (samples-side)
      │                │
      └────────┬───────┘
               │
            X5-D
            hello_engine FileWatcher harness
            (HelloShaderWatch.hpp anon ns header)
            developer
               │
            X5-E
            edit-and-revert smoke test
            tester (2 cases: tonemap tweak,
                    revert; pipeline rebuild count)
               │
            X5-F
            doc + ADR + sample README
            doc-writer
```

Critical-path: X5-A → X5-B parallel X5-C → X5-D → X5-E → X5-F.

### 3.4 Test gate

- `test_material_recreate.cpp` — 2 cases:
  1. Edit-detected: write file → poll → assert
     `materials.prim.pipeline()` handle changed; assert old handle
     deferred-release.
  2. Revert: write back original → poll → assert second rebuild;
     assert no GPU-side timeline regression (no `wait_idle` blocking
     present queue).
- `test_shader_watcher.cpp` (polish): 100 Hz poll over 1 s with no
  edits → 0 spurious recompiles.
- `cd_test_hello_engine_shader_watch` integration (Tier B smoke):
  boot hello_engine, edit `composite.frag.glsl`, assert ImGui still
  responsive after rebuild.

### 3.5 Owner-agents

| Sub-task | Lead       | Support      | Why                                                            |
| -------- | ---------- | ------------ | -------------------------------------------------------------- |
| X5-A     | architect  | -            | `MaterialDesc` additive field + recreate contract = ADR-worthy |
| X5-B     | developer  | -            | Pure mechanical move + `configure_file` for release fallback   |
| X5-C     | developer  | safety-integration | Deferred-release ordering = GPU-lifetime hazard          |
| X5-D     | developer  | -            | Sample-side wiring                                             |
| X5-E     | tester     | -            | Golden-friendly; not load-bearing for X4/X1-FU-F               |
| X5-F     | doc-writer | -            | Sample README + 1 ADR rendering                                |

### 3.6 Recommended ADRs for X5

- **ADR-20260608-x5-material-desc-file-source-variant** (Iglberger)
  *Context*: `MaterialDesc::vertex_glsl_path` mutually-exclusive with
  inline GLSL. *Reddedilen alternatifler*: `IFileSource` indirection
  (vfs has no write-notify); single combined `.glsl` w/ `#stage`
  blocks (breaks editor LSP); raw `std::filesystem::path` field
  (lifetime hazard).
- **ADR-20260608-x5-material-recreate-deferred-release**
  Locks the "create new → wait_idle → release old" sequence so
  future contributors can't introduce a hot-swap-by-patching-PSO
  regression (Vulkan PSOs are immutable; this is a permanent
  invariant).

---

## §4. X1-FU-F — Vulkan secondary command buffer pipeline

### 4.1 Scope

X1-FU-F is the **deferred half of the original X1E** scope (per
ADR-20260528 Addendum § "Out of scope (still open)"). Marathon Run 7
shipped X1E *prep* (`parallel_for` over CSM + planar caster math) but
deferred the actual **secondary command buffer recording across worker
threads** because:

> "the current `cd::rhi::ICommandBuffer` surface has no
> secondary-buffer concept and adding `ISecondaryCommandBuffer` +
> Vulkan/D3D12/OpenGL/Metal/Null impls + inheritance state propagation
> is past the >500-line stop condition in the marathon mandate."
> — ADR-20260528 Addendum

This is the milestone that delivers the **"true parallel render"
claim** for the engine. Without it, every backend records commands
serially on the main thread regardless of how many workers the WSL
pool has. With it, `hello_engine` can record each render pass on its
own worker.

### 4.2 Prerequisites

1. **`cd::rhi::ICommandBuffer` API review** — current surface assumes
   primary-only. Need to introduce `ISecondaryCommandBuffer` as a
   distinct type (or `CommandBufferLevel` enum on `BeginInfo`),
   `begin_render_pass(..., SubpassContents::kSecondaryCommandBuffers)`,
   `execute_commands(span<ISecondaryCommandBuffer*>)`.
2. **Inheritance-state propagation** — Vulkan secondaries need
   `VkCommandBufferInheritanceInfo` (render pass, subpass index,
   framebuffer, occlusion query state, query precision). Wrap in
   `cd::rhi::CommandBufferInheritance` struct.
3. **Per-backend impl** — Vulkan native; D3D12 bundles (different
   model — no inheritance, immutable-after-close); OpenGL no-op
   (legacy; serial fallback); Metal indirect command encoder (deferred
   to L1 milestone — stub OK); Null no-op.
4. **WSL pool integration** — `X1 Phase 2` consumer; this milestone
   only delivers the **interface + Vulkan impl**, not the hello_engine
   wiring (that's X1 Phase 2 territory).
5. **TSan baseline** (X1-FU-B) — required before claiming "thread-safe"
   on the new types. X3 NVIDIA lane hardware is the blocker.

### 4.3 Sub-task DAG

```text
                X1-FU-F-A (architect + safety-integration BLOCKING)
                │  ADR: ISecondaryCommandBuffer surface
                │       + CommandBufferInheritance + execute_commands
                │
        ┌───────┴────────────────────┐
        │                            │
  X1-FU-F-B                    X1-FU-F-C
  primary cmd buffer           cd::rhi::ICommandBuffer
  refactor (level enum)        BeginInfo + RenderPassBegin
  architect                    contract update
                               architect
        │                            │
        └─────────────┬──────────────┘
                      │
   ┌──────────┬───────┴────────┬──────────────┐
   │          │                │              │
   F-D-vk    F-D-d3d12        F-D-gl         F-D-mtl + F-D-null
   developer developer        developer      developer (stubs)
   + safety  (bundles model)  (no-op)
   integration
   (BLOCKING
    review)
                      │
                X1-FU-F-E
                tests: secondary record + replay + ordering
                tester (stress: 8 threads × 16 secondaries × 100 it)
                      │
                X1-FU-F-F
                TSan run on the new tests
                tester (blocked on X3 NVIDIA lane)
                      │
                X1-FU-F-G
                hello_engine consumer wiring
                (NOT THIS MILESTONE — filed under X1 Phase 2)
```

Critical-path: F-A → F-B parallel F-C → F-D-vk (others parallel) →
F-E → F-F (gated on X3).

### 4.4 Test gate

- `test_secondary_command_buffer.cpp`:
  - Create primary + 4 secondaries; record clear / draw on
    secondaries; `execute_commands`; assert no validation error.
  - Inheritance state correctness: secondary requesting subpass=1 in a
    pass with 2 subpasses must validate; same with mismatched render
    pass handle must reject.
  - Cross-thread: 8 worker threads each create a secondary, fill,
    handoff to main; main `execute_commands` all 8. TSan-clean.
- `test_d3d12_bundle.cpp`: parity smoke for D3D12 bundles — assert
  immutable-after-close, no inheritance contract (different mental
  model; ADR documents the divergence).
- TSan: full concurrency suite re-run with the new tests added
  (blocked on X1-FU-B / X3).
- ASan: existing baseline; new test target must not regress.

### 4.5 Owner-agents

| Sub-task     | Lead                | Support                       | Why                                                                  |
| ------------ | ------------------- | ----------------------------- | -------------------------------------------------------------------- |
| F-A          | architect           | safety-integration (BLOCKING) | New cross-thread API surface = concurrency hazard surface            |
| F-B / F-C    | architect           | -                             | Surface refactor; no impl                                            |
| F-D-vk       | developer           | safety-integration (BLOCKING) | Vulkan secondary + inheritance is the canonical reference impl       |
| F-D-d3d12    | developer           | architect                     | D3D12 bundles diverge — ADR addendum needed                          |
| F-D-gl/mtl/null | developer × 3     | -                             | Stubs; no thread hazard                                              |
| F-E          | tester              | safety-integration            | Stress + ordering oracle                                             |
| F-F          | tester              | build-devops                  | TSan preset already wired; X3 lane is the blocker                    |
| F-G          | (out of scope)      | -                             | Filed under X1 Phase 2                                               |

### 4.6 Recommended ADRs for X1-FU-F

- **ADR-20260608-x1-fuf-secondary-command-buffer-surface** (Iglberger)
  *Context*: Two-tier command buffer model (primary + secondary) +
  inheritance state across 5 backends. *Reddedilen alternatifler*:
  enum-on-primary (loses type safety); bundle-only (D3D12 model has no
  Vulkan-equivalent); separate `IRenderPassRecorder` lambda-callback
  surface (loses Vulkan ergonomics).
- **ADR-20260608-x1-fuf-d3d12-bundle-divergence**
  Addendum-style: D3D12 bundles are immutable-after-close and have no
  inheritance state. ADR locks the equivalence map (which
  `ISecondaryCommandBuffer` calls are no-ops, which are rejected, which
  trigger bundle-record vs immediate-record).
- **ADR-20260608-x1-fuf-command-buffer-thread-safety-contract**
  *Context*: which `ICommandBuffer` calls are safe from concurrent
  worker threads, which are owner-only. Required for safety-integration
  sign-off on F-D-vk. Likely supersedes a section of ADR-015.

---

## §5. Suggested execution ordering

### 5.1 Recommended order

```text
Week 1                Week 2                Week 3-4              Week 5-7
─────────────────────────────────────────────────────────────────────────
X5  (1 wk)            X1-FU-F (1 wk)        X4-A..D (parallel)    X4-E..H
ship developer-       ship interface +      research +            close 5 NotImpl
experience win        Vulkan impl           toolchain             + DXR + sample
                                            decisions             + golden
```

Rationale:

1. **X5 first** — cheapest (1 week), lowest risk, biggest immediate
   payoff (shader iteration speed). Unblocks every downstream graphics
   tweak the user is currently doing in hello_engine. Does not depend
   on X4 or X1-FU-F.

2. **X1-FU-F second** — second-cheapest (1 week for interface +
   Vulkan), and unblocks the **X1 Phase 2** integration that the job
   system ADR explicitly waits on. Should run *before* X4 because
   X4-E3 (DXR cmd-list authoring) will benefit from having the
   secondary cmd surface settled — DXR command list build is a
   natural candidate for off-thread recording.

3. **X4 last** — biggest milestone (3-4 weeks). Splits into a
   prerequisite phase (X4-A toolchain decision; X4-B image-readback
   API; X4-C SPIRV-Cross-glue lib; X4-D ImGui DX12 vendoring) that
   can run **in parallel** during week 3-4, then an implementation
   phase (X4-E*) that lands in week 5-7. The X3 CI hardware question
   is orthogonal — X4 ships its CI lane as `allow_failure: true` and
   flips green when the NVIDIA self-hosted runner physically arrives.

### 5.2 Parallelization opportunities

| Pair / triple                    | Independent? | Notes                                                             |
| -------------------------------- | ------------ | ----------------------------------------------------------------- |
| **X5** ∥ **X1-FU-F-A/B/C**       | YES          | Material lib (X5) vs RHI surface (F): zero touchpoints            |
| **X1-FU-F-D-vk** ∥ X5-E test     | YES          | RHI impl vs sample-side test, different libs                      |
| **X4-A** (toolchain) ∥ **X4-B** (image-readback) ∥ **X4-D** (ImGui DX12) | YES | Three different parts of the toolchain prereq layer  |
| **X4-E1** (1D/3D tex) ∥ **X4-E2** (SubmitDesc) ∥ **X4-E3** (DXR)         | YES | Three different D3D12Device.cpp regions, no shared state          |
| **X1-FU-F-F** (TSan) ∥ X4-G (golden) | YES        | Different CI lanes (TSan = Linux/Clang; golden = NVIDIA-on-prem)  |

### 5.3 Anti-parallel (must serialize)

| Pair                              | Why                                                                                  |
| --------------------------------- | ------------------------------------------------------------------------------------ |
| X1-FU-F-A → all F-D-* impls       | Surface must settle before impls                                                     |
| X4-A → X4-F (hello_d3d12_pbr)     | Sample needs final shader-toolchain choice                                           |
| X4-B → X4-G (golden test)         | Golden test needs image-readback API                                                 |
| X5-A → X5-C (recreate impl)       | Recreate contract must be ADR'd before impl                                          |
| X1 Phase 2 integration → X4 + X7  | Per ADR-20260528 D5 + Sonuclar; X1-FU-F is interface only, integration waits         |

---

## §6. Open questions for the next architect-team meeting

These need a **user decision** or a **multi-agent council** (architect +
researcher + safety-integration) before the ADRs in §2.6 / §3.6 / §4.6
can be drafted.

### Q1 — X4 shader-toolchain direction

**Question**: GLSL → DXIL crossing for D3D12. Three options:
- (a) SPIRV-Cross-glue Tier-B FetchContent library; one shader source
  (GLSL), one cross-compile to DXIL at build/runtime.
- (b) Native DXIL via DirectXShaderCompiler; author HLSL + GLSL
  side-by-side; double the shader corpus, simpler runtime path.
- (c) HLSL-only; flip GLSL to HLSL as the engine source-of-truth;
  glslang HLSL-frontend for the Vulkan path.

**Why it's a council Q**: irreversible toolchain choice; affects every
shader the engine will ever ship; X5 (on-disk shaders) is mostly
agnostic but ships before the choice is made — so X5's path resolver
must accept whichever extension lands here.

### Q2 — X1-FU-F: enum-on-primary vs separate `ISecondaryCommandBuffer` type

**Question**: do we expose secondaries as
`CommandBufferLevel::kSecondary` on a single `ICommandBuffer`
interface, or as a distinct `ISecondaryCommandBuffer` type?

**Why it's a Q**: type-safety vs API surface count. Vulkan models it
as the same `VkCommandBuffer` w/ level enum; D3D12 bundles are a
*different type* (`ID3D12GraphicsCommandList` w/ `D3D12_COMMAND_LIST_TYPE_BUNDLE`
but practically a different object lifecycle). Architect recommendation:
two types, because the bundle-divergence ADR is cleaner when D3D12
returns a different handle.

### Q3 — X4-E2 SubmitDesc semaphore semantics: timeline-only or binary-supported?

**Question**: Vulkan supports binary + timeline semaphores; D3D12
fences are *only* timeline. Do we ship the cross-API `SubmitDesc` as
timeline-only (simpler) or with a binary-semaphore-compatibility shim
for Vulkan-native paths?

**Why it's a Q**: cross-platform consistency vs Vulkan-native power.
Filament chose timeline-only; bgfx exposes neither. Recommendation:
timeline-only public API; reserve `cd::rhi::vulkan::native::binary_semaphore`
as an advanced-tier escape hatch.

### Q4 — X5 release-mode fallback: keep embedded literals or ship `shaders/` folder?

**Question**: `HELLO_ENGINE_USE_ON_DISK_SHADERS=ON|OFF` is proposed
default-ON debug, default-OFF release. But default-OFF release means
the `inline constexpr` literals stay in source and a `configure_file`
keeps them in sync with disk. Acceptable, or should release ship
`shaders/` next to the binary?

**Why it's a Q**: deployment model. Library-as-product (engine as
embedded dep) prefers literal-embedded; standalone editor (L5) prefers
shipped folder. Pick once.

### Q5 — X1-FU-F precedes or follows X1 Phase 2?

**Question**: X1 Phase 2 (WSL integration into render/asset/ECS) is
queued behind X4 + X7. X1-FU-F (the secondary cmd interface) is the
*interface* prerequisite for the render half of X1 Phase 2 — should
X1-FU-F slip in *between* the current X1 Phase 1 (DONE) and X1 Phase 2,
or should we collapse X1-FU-F + the X1 Phase 2 render slice into one
sprint? Recommended ordering above (§5.1) keeps them separate.

### Q6 — X4 honest-scope: ship without NVIDIA self-hosted runner?

**Question**: ADR-20260529-X4 already lists "X3 NVIDIA self-hosted CI
lane has no hardware yet" as a rejected alternative for full-runtime
testing. Do we ship X4 with `allow_failure: true` on the D3D12-runtime
CI row (the only honest option) and flip when hardware lands? Or do we
*gate* X4 close-out on the hardware arrival?

**Why it's a Q**: CLAUDE.md §3 ("evidence-based / build temiz + test
geçer") vs marathon "scope DOWN and ship the slice that works" rule.
The CPU-side parity smoke + golden-on-developer-box are evidence; CI
green is *enforcement*. Recommendation: ship + allow_failure; mark X4
status as "DONE pending CI gate flip".

---

## §7. Out-of-scope for this roadmap

The following are tracked but explicitly excluded from Phase 2 X4/X5/X1-FU-F:

- **X1 Phase 2 hello_engine integration sites** — handled per
  ADR-20260528 Addendum once X4 + X7 land.
- **X1-FU-A/B/C/D/E/G/H polish items** — separate work, see §1 catalog.
- **L1 Metal backend** — Phase 3 (Section C vision tier), not Phase 2.
- **L3 DDGI / L4 Nanite / L5 Editor binary** — Phase 3+.
- **Demir Kural academic pilot** — N4 sample, orthogonal to X-tier
  engineering milestones (no academic ADR claims in X4/X5/X1-FU-F).
- **`cd::sample_framework` library redesign** — required to push
  `hello_engine main()` body under 500 lines, but does not block any
  X-tier milestone; queued separately.

---

## §8. Next steps

1. **User**: approve roadmap OR redirect §6 open questions.
2. **architect** (this agent): on approval, draft ADRs in §2.6 / §3.6 /
   §4.6 in Iglberger format under `docs/ADR/ADR-20260608-*`.
3. **team-lead**: dispatch sub-tasks per §5.1 ordering once ADRs land.

No code, no further ADR writing from this roadmap pass.

---

## §9. SL — SOTA shader library (user-priority addition, 2026-06-12)

User direction: "çok gelişmiş state-of-the-art seviyesi bir shader
library; yapısı normal kütüphane yapımıza yakın/aynı olabilir;
kütüphaneleşebilecek mevcut shader'lar + yeni shader'lar plana
eklenmeli." Reusable shader MODULE library, consumable like any
`engine/<lib>` library and shippable standalone.

### 9.1 Evidence (duplication inventory, phase1128 probe)

76 files carry embedded GLSL strings; 7 on-disk GLSL files (hello_engine
X5 path + cluster_assign.comp). Duplicated core snippets across them:
Fresnel ×9 files, GGX-D ×5, Smith-G ×6, fBm noise ×6, octahedral
encode/decode ×8, hash/PCG ×8, Hammersley/importance-sampling ×5,
tonemap operators spread over 34 mention sites. Every duplicate is a
silent-drift hazard (the W8 sweeps repeatedly fixed one copy and missed
another).

### 9.2 Sub-task DAG

```text
SL-A  SOTA research (researcher, RUNNING 2026-06-12)
      Slang / Unity SRP-Core ShaderLibrary / Unreal .ush / Filament
      matc / bgfx shaderc / Godot includes / glslang include EXT +
      variant-management SOTA -> research/notes/shader_library_sota.md
   │
SL-B  ADR (architect): module granularity + include/import mechanism
      (glslang DirStackFileIncluder vs Slang adoption vs hybrid),
      variant/permutation strategy, CachedCompiler cache-key impact
      (key MUST hash the include CLOSURE, not just the root source),
      X4-A D3D12 toolchain ADR as cross-input (single-source story).
   │
SL-C  Library skeleton: engine/render/shader_library/
      (cd::shader_library) — .glsl module files + C++ registry +
      include resolver wired into cd::shader::CachedCompiler + X5
      hot-reload (watch the closure, not just roots).
   │
SL-D  Extraction wave 1 (golden-pinned per consumer migration):
      brdf_common.glsl (Fresnel/GGX/Smith) -> noise.glsl (hash/PCG/
      fBm/curl) -> sampling.glsl (Hammersley/importance/oct) ->
      tonemap.glsl (ACES/Reinhard/Uchimura/AgX) -> color.glsl ->
      shadow.glsl (PCF family). Consumers migrate ONE AT A TIME with
      chrome_probe + per-feature golden pins.
   │
SL-E  New-shader policy: every new shader is born as modules + thin
      entry point; headless compile-all-modules ctest gate (glslang)
      + permutation smoke. Doc: README + module catalogue.
```

Test gate: headless compile of every module permutation (no GPU
needed) + existing golden fixtures stay byte-identical through each
extraction step. Owner chain: researcher → architect → developer × N →
tester. SL-A/SL-B block SL-C; SL-D items land independently after
SL-C and can interleave with X4 work.
