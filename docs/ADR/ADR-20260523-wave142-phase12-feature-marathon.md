# ADR — Wave 142 — Phase 12 feature marathon plan

- Date: 2026-05-23
- Status: Accepted
- Wave: 142
- Predecessor: ADR-20260523-wave133 (Phase 11 plan — partially closed
  via AI surrogates in Waves 134-141; B1-B5 reviewer blockers
  intentionally skipped per user decision)

## Context

The Phase 11 v1.0 maturity work landed AI-surrogate closure on all
four axes (A: NVIDIA goldens, B: Intel iGPU cross-vendor noise floor,
C: external_app downstream blueprint, D: independent-auditor review).
The reviewer surfaced 5 blocking items (B1-B5 in
`docs/REVIEW-independent-auditor-20260523.md`) — the user explicitly
chose to **skip** those and instead open Phase 12 as a feature
marathon. The reviewer items remain in tree as open issues for a
future hardening pass.

The user is unavailable for review during Phase 12. The marathon
runs autonomously under the rules captured in
`memory/feedback_long_autonomous_marathon.md`:

- Stop one phase → immediately plan + start the next.
- Minor tags (v0.26+) are AI-autonomous.
- v1.0 tag remains user-only.
- Stay on `dev`, push immediately per atomic chunk.

Ordering directive: **general → specific, easy → hard, fast → slow**.

## Decision

Phase 12 has four sub-phases. Each closes on a minor release tag.
When 12.D closes, autonomously plan Phase 13.

### 12.A — Performance + profile sweep → v0.26.0

**Why first:** broadest payoff per hour, lowest implementation
risk, easiest to verify (numbers don't lie).

**Scope:**
- Expand `hello_bench` coverage from 7 to ~20 micro-benchmarks
  hitting the foundation + asset + ECS + render hot paths.
- Commit a `tests/bench-baseline.json` reference set captured on the
  NVIDIA / clang-cl Debug+Release configurations.
- Tighten `bench-regression.yml` from the existing 10% threshold to
  per-bench thresholds based on observed run-to-run variance.
- Hot-path audit: find at least 3 measurable wins (allocations per
  frame, vector-vs-span return, unnecessary copies on Result chains).
- Profile sweep doc in `docs/PERFORMANCE.md` (new) covering how to
  read the bench output, how to add a new bench, and what the
  current numbers mean.

**Stop:** v0.26.0 tagged.

### 12.B — D3D12 backend completion → v0.27.0

**Why second:** Windows-native, AI can test the full path on this
box; closes "real cross-API" engine claim. Currently `cd::rhi_d3d12`
is a skeleton + ADR.

**Scope:**
- Implement `cd::rhi::IDevice` against ID3D12Device.
- Swapchain via IDXGISwapChain3.
- Command list + command queue.
- HLSL shader compile via DXC (DirectX Shader Compiler — vendored
  via FetchContent like glslang).
- `hello_triangle_d3d12` sample variant — same shaders, different
  backend.
- D3D12 golden set in `tests/golden/d3d12_nvidia/`.

**Stop:** v0.27.0 tagged.

### 12.C — Editor stack maturation → v0.28.0

**Why third:** more domain-specific, user-facing, slower to land
because UX iteration eats wall time. `cd::editor` + `cd::editor_ui`
are currently early-stage.

**Scope:**
- Scene tree panel that drives a real 3D viewport (combine
  `hello_inspector` + `hello_scene_graph` patterns).
- Asset inspector — drag-drop, property edit, on-disk save.
- Undo/redo stack via `cd::events` integration.
- Dock + multi-pane ImGui layout (the docking branch is already
  vendored).
- `hello_editor` sample showcasing the full UX.

**Stop:** v0.28.0 tagged.

### 12.D — Ray tracing + mesh shaders → v0.29.0

**Why last:** most specialized, hardest, slowest. NVIDIA RTX 3080
Laptop supports both — `VK_KHR_ray_tracing_pipeline` (tier 1) +
`EXT_mesh_shader`. Modern render feature parity.

**Scope:**
- `VK_KHR_acceleration_structure` BLAS/TLAS build through
  `cd::rhi`.
- `VK_KHR_ray_tracing_pipeline` — rgen/rmiss/rchit shaders, SBT
  layout, dispatch.
- `hello_raytrace` sample — a single bounce, sphere + plane,
  hard shadows.
- `VK_EXT_mesh_shader` — emit a simple procedural mesh from the
  mesh shader stage.
- `hello_meshshader` sample.

**Stop:** v0.29.0 tagged.

## Rejected alternatives

- *Address the 5 reviewer blockers (B1-B5) first.* Rejected per
  user directive at Phase 12 setup. The reviewer items will be
  addressed in a separate hardening pass once feature work is
  paused for review.
- *Cut v1.0 if all four sub-phases land cleanly.* Rejected —
  marathon rule: v1.0 stays user-only. The maturity gate needs
  the human B1-B5 cleanup pass + a real downstream consumer +
  the un-AI-surrogate D-axis review.
- *Spawn parallel subagents for 12.A/B/C/D.* Rejected — they share
  too much state (RHI surface touches across A's bench expansion,
  B's D3D12, D's RT pipeline). Sequential is safer; parallel can
  open up inside each sub-phase if a clean cut exists.

## Consequences

- Phase 12 runs autonomously. Expected duration: ~50-100 waves
  across the four sub-phases.
- Each sub-phase produces a tagged minor release; the engine ships
  `v0.26.0 → v0.27.0 → v0.28.0 → v0.29.0` over the marathon.
- The 5 reviewer blockers stay open in tree as
  `docs/REVIEW-independent-auditor-20260523.md` until the user is
  back; their fix is the natural Phase 13 setup.
- v1.0 path is not advanced in Phase 12 — by design. v1.0 wants
  human sign-off on the maturity gate.

## What chains after Phase 12.D

When 12.D closes, the marathon enters Phase 13 autonomously. The
sensible candidates:

1. **B1-B5 hardening pass** (the deferred reviewer items).
2. **Mobile RHI completion** — `cd::rhi_metal` for Apple Silicon
   via MoltenVK; Android Vulkan via the existing rhi_vulkan with
   surface platform extension.
3. **Linux Vulkan golden capture** under lavapipe (the
   `linux-vulkan-sw` CI job goldens are still missing).
4. **RT denoise / accumulation** (extend 12.D's raytrace path).

Phase 13's ADR will pick from the above when this ADR's last
sub-phase closes.

## References

- ADR-20260523-wave133 (Phase 11 plan — superseded for forward
  motion by this ADR; the 4-axis v1.0 gate definition still binds)
- `docs/REVIEW-independent-auditor-20260523.md` — the 5 deferred
  blockers
- `memory/feedback_long_autonomous_marathon.md` — operating rules
  for this marathon style
