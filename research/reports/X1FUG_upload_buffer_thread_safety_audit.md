# X1-FU-G — `IDevice::upload_buffer` Thread-Safety Spec Audit

**Date**: 2026-06-11 (Sprint 1, phase 1075)
**Scope**: spec audit only — no behavioural code change. Deliverables:
this report + the threading-contract documentation block added to
`cd/rhi/IDevice.hpp`.
**Trigger**: ADR-20260528 (job system) Consequences list; priority
raised after `cd::debug_draw` made buffer upload a multi-consumer path
(hello_engine + hello_editor) and X1 Phase 2 plans more CPU-parallel
producers.

## 1. Evidence collected

| Backend | upload path | resource tables | id counter |
|---|---|---|---|
| Vulkan (`VulkanDevice.cpp:968`) | `vmaCopyMemoryToAllocation` (map+memcpy+flush+unmap, atomic per call) | `buffers_` / `buffer_alloc_` / `buffer_meta_` — plain `std::unordered_map`, **no mutex** | `std::uint32_t next_id_` (`:4395`), **not atomic**, `next_id_++` at 20+ create sites |
| D3D12 (`D3D12Device.cpp:2799`) | persistent-map + `memcpy` | plain maps, **no mutex** | plain `std::uint32_t` (`:3255`) |
| OpenGL (`OpenGLDevice.cpp:956`) | `glNamedBufferSubData` (DSA) | plain maps, **no mutex** | plain (`:1121`) — additionally GL has a thread-affine context |
| Null (`NullDevice.hpp:154`) | `memcpy` into owned vector | plain maps, **no mutex** | plain (`:568`) |

VMA note: the allocator is created **without**
`VMA_ALLOCATOR_CREATE_EXTERNALLY_SYNCHRONIZED_BIT`, so VMA's internal
mutexes make `vmaCopyMemoryToAllocation` itself thread-safe; the race
surface is OUR handle tables, not VMA.

## 2. Hazard matrix

| Concurrent pair | Verdict | Why |
|---|---|---|
| `upload_buffer(A)` ∥ `upload_buffer(B)`, A≠B | **safe** (today) | map reads only; VMA copy internally synchronized; D3D12 persistent map per-resource; GL DSA per-buffer |
| `upload_buffer(A)` ∥ `upload_buffer(A)`, disjoint ranges | safe at backend level, **discouraged** | no torn bytes (separate memcpy ranges), but no ordering guarantee |
| `upload_buffer(A)` ∥ `upload_buffer(A)`, overlapping ranges | **caller race** | last-writer-wins per byte; never meaningful |
| `upload_buffer` ∥ `create_*` / `destroy_*` | **DATA RACE (UB)** | `unordered_map` rehash/erase vs find; classic invalidation |
| `create_*` ∥ `create_*` | **DATA RACE (UB)** | non-atomic `next_id_++` → duplicate handles + map race |
| `upload_buffer` ∥ command-buffer recording referencing the same buffer | GPU-timeline hazard, out of scope | covered by the frames-in-flight / park-margin conventions (see debug_draw ADR) |

## 3. Current engine usage (why nothing is broken today)

Every producer that reaches `upload_buffer` today funnels through the
render thread: hello_engine frame loop, hello_editor frame loop,
`cd::debug_draw::Renderer::flush`, TLAS ring uploads, IBL bake (boot,
before the loop starts). The X1 Phase-2 parallel passes (parallel TLAS
instance build, phase 283+) parallelise the CPU **fill**, not the
upload call. Verdict: the implementation is **thread-compatible**, the
engine is **single-upload-threaded**, and the two were consistent but
UNDOCUMENTED — any future parallel asset-streaming work could silently
break it. That documentation gap is what this audit closes.

## 4. Contract adopted (now in IDevice.hpp)

1. **Resource lifetime calls** (`create_*`, `destroy_*`) require
   external synchronization with EVERY other IDevice call.
2. **`upload_buffer` / `download_buffer`** are thread-compatible:
   concurrent calls on distinct handles are allowed provided rule 1
   holds (no concurrent create/destroy in flight).
3. Concurrent same-handle uploads are the caller's responsibility;
   overlapping ranges are always a bug.
4. CPU↔GPU timeline hazards stay with the caller (fif fencing /
   park-margin, per the debug_draw ADR).

## 5. Follow-up options (decision deferred — needs X1 Phase 2 owner)

- **F1 (cheap)**: make `next_id_` atomic in all four backends —
  removes the duplicate-handle hazard only; maps still race.
- **F2 (medium)**: `std::shared_mutex` over the resource tables
  (shared for lookups, exclusive for create/destroy). Cost: one
  uncontended shared lock per upload/draw-path lookup.
- **F3 (full)**: handle-table redesign (slot array + generation, the
  bindless direction) giving lock-free lookup; aligns with the X4
  D3D12 parity work. Recommended timing: with X4, not before.

Recommendation: keep the documented external-synchronization contract
until X1 Phase 2 actually introduces a second producer thread; then F2
as the stopgap, F3 with X4.
