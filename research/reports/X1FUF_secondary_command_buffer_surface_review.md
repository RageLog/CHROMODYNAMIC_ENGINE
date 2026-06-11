# X1-FU-F — Secondary Command Buffer / Parallel Recording Surface Review

**Date**: 2026-06-11 (Sprint 2, phase 1081)
**Scope**: surface review ONLY (sanctioned pre-K1 per
PLAN_2026-06-11 §D item 8). The ADR + implementation start after the
user approves ROADMAP_PHASE_2 (K1). No code changes in this phase.
**Reviewed surface**: `cd/rhi/ICommandBuffer.hpp` (29 virtuals,
primary-only), `IDevice::create_command_buffer` (NVI since phase
1069), the X1-FU-G threading contract (phase 1075), Vulkan/D3D12/
OpenGL/Null/Metal backend command-buffer impls.

## 1. Why this exists

ADR-20260528 deferred the original X1E scope ("threaded cmd record per
render pass") because ICommandBuffer has no secondary-buffer concept.
The X1 prep/submit pattern (parallel_for fills PrimPush scratch,
serial loop records) already removed the CPU fill cost; what remains
serial is the **vkCmd\* recording itself** (~one call set per draw ×
~hundreds of draws across HDR/shadow/CSM passes in hello_engine).

## 2. Per-backend reality check

| Backend | Native mechanism | Constraints that shape the abstraction |
|---|---|---|
| Vulkan | SECONDARY level + `vkCmdExecuteCommands`; `VkCommandBufferInheritanceInfo` (render pass + subpass + framebuffer) for within-pass recording | One `VkCommandPool` **per recording thread** (pools are externally synchronized); secondaries recorded with `RENDER_PASS_CONTINUE` |
| D3D12 | Bundles are NOT the analogue (no RT changes, heavy restrictions). The native model is N **direct command lists** recorded in parallel + ordered `ExecuteCommandLists` | Allocator per recording thread; splitting one render pass across lists requires re-binding RT/viewport per list |
| Metal | `MTLParallelRenderCommandEncoder` → N sub-encoders, one per thread | Order fixed at encoder creation; pass-scoped by construction |
| OpenGL | none — emulate (record into a CPU command list, replay serially) | The existing GLCommandBuffer is already a CPU replay list — emulation is nearly free |
| Null | trivial | test double; records the parallel topology for assertions |

**Conclusion**: exposing Vulkan's "secondary command buffer" raw is
the wrong abstraction — D3D12 and Metal don't have that object. All
four map cleanly onto a **pass-scoped parallel recorder** instead:

## 3. Recommended API shape (for the post-K1 ADR)

```cpp
// On ICommandBuffer (primary):
//   Begin a render pass whose draw recording will be split across
//   threads. Returns a recorder factory bound to this pass.
[[nodiscard]] virtual std::unique_ptr<IParallelPassRecorder>
begin_parallel_render_pass(const RenderPassBeginInfo& info,
                           std::uint32_t lane_count);

// IParallelPassRecorder:
//   lane(i) -> ICommandBuffer&  (draw-subset surface, thread i only)
//   finish() -> joins lanes back into the primary in lane order.
```

- `lane(i)` returns the EXISTING ICommandBuffer interface but with a
  documented **draw-subset contract**: pipeline/descriptor/vertex/
  index/push/draw/debug-group only. begin/end/begin_render_pass/
  barrier/copy on a lane = programming error (Null backend asserts;
  Vulkan validation layers catch it for free).
- Vulkan: lane = secondary buffer with inheritance info; finish =
  `vkCmdExecuteCommands`. D3D12: lane = direct list with RT re-bound;
  finish = ordered `ExecuteCommandLists` (the primary's pass is split
  into per-lane segments). Metal: lane = sub-encoder. GL/Null: lanes
  are CPU lists replayed in order.
- Determinism rule: lane ORDER is the submission order — same-input
  frames replay identically regardless of which worker filled which
  lane (golden-image safety).

## 4. Threading contract extensions required

1. Each lane is single-thread-recorded (mirrors Vulkan pool rule);
   the pool that backs a lane belongs to the recording thread.
   Natural fit: `WorkStealingThreadPool::thread_count()` lanes, lane
   index = worker index — the X1 prep/submit pattern's scratch
   partitioning carries over unchanged.
2. The IDevice rule-1 contract (phase 1075) extends: lane creation /
   pool allocation is resource creation → externally synchronized;
   per-frame lane RESET must be fenced by frames-in-flight (same
   fif=2 + park-margin discipline as buffers, see debug_draw ADR).
3. `push_debug_group` string-copy contract already matches secondary
   recording (backend copies before return) — no change.

## 5. Pre-existing surface friction found by this review

- `ICommandBuffer` lifecycle (begin/end) and pass scope
  (begin_render_pass/end_render_pass) live on the same interface the
  lanes would expose — the draw-subset contract is documentation +
  Null-assert, not type-system enforced. A split interface
  (IDrawRecorder base, ICommandBuffer extends it) would enforce it at
  compile time; costs a base-class re-parent of all 5 backends.
  **Recommendation: do the split** — it is mechanical, NVI-compatible
  with the phase-1069 pattern, and removes the whole misuse class.
- `bind_descriptor_set(set_index, set)` is stateless per call — good;
  no hidden state leaks across lanes besides pipeline/dynamic state,
  which each lane must re-establish (documented per backend above).
- The bindless dedicated SET (memory rule 9) binds per pass — each
  lane re-binds it; cheap, and the Vulkan inheritance info carries no
  descriptor state anyway.

## 6. Effort breakdown (post-K1, est. ~1 week per ROADMAP)

1. ADR + IDrawRecorder split + Null lanes with assertions + tests.
2. Vulkan lanes (per-thread pools, inheritance, ExecuteCommands) +
   golden parity capture (lane_count 1 vs N must be pixel-identical).
3. hello_engine HDR-pass adoption behind a panel toggle (lane_count
   slider 1..workers; perf counter line in the frame profiler).
4. D3D12 segmented lists (with X4 parity work), Metal sub-encoders
   (with K2), GL/Null emulation first-class from step 1.

**Gate**: safety-integration review on step 2 (pool lifetime + reset
fencing) before merge — same bar as the Sprint-2 concurrency work.
