# ADR — Wave 151 — Phase 13 plan

- Date: 2026-05-23
- Status: Accepted
- Wave: 151
- Predecessor: ADR-20260523-wave142 (Phase 12 master) — Phase 12
  closed at v0.29.0 / Wave 150.

## Context

Phase 12 shipped four detection / primitive milestones (v0.26.0 →
v0.29.0). Three structural items were deferred from Phase 12 because
they need human pixel-validation:

- D3D12 full surface (beyond `Wave 148` boot)
- RT + mesh-shader render path (beyond `Wave 150` detection)
- Editor UI bridge (beyond `Wave 149` undo/redo primitive)

Plus the **5 reviewer blockers** (B1-B5) from
`docs/REVIEW-independent-auditor-20260523.md` that Wave 142 explicitly
deferred. The reviewer estimated "weeks, not months" for closure;
those are exactly the items that need the marathon's
shipping-discipline less than they need attention.

User's autonomous marathon directive still binds: stay on dev, push
immediately, chain phases without pause, ordering general →
specific easy → hard fast → slow.

## Decision

Phase 13 opens with the four sub-phases below.

### 13.A — Reviewer blockers B1-B5 → v0.30.0

**Why first:** all five are concrete, smallish, AI-actionable without
visual review. Closing them is the cleanest forward motion and
materially improves the engine's ABI-promise integrity.

- B1: LIBRARIES.md lists phantom `cd::serialization`; library count
  drift (48 doc vs. 55 actual). Doc-only fix; grep + rewrite the
  table.
- B2: Docs reference `Engine/` (capital E); on-disk is `engine/`.
  Case-sensitivity landmine. Doc-only fix; mass-replace across
  docs/* + Readme.md.
- B3: Vulkan debug-label uses a stack-buffer pointer. RHI bug;
  switch to a persistent allocation or document the lifetime
  contract clearly in the header.
- B4: CMake `SameMajorVersion` policy is wrong for 0.x. Should be
  `SameMinorVersion` until v1.0. One-line CMakePackageConfigHelpers
  argument flip + ConfigVersion regeneration.
- B5: Six `catch(...)` violations of ARCHITECTURE.md §3.1. Either
  rewrite the rule to match practice, or remove the catches. Pick
  the rewrite (the intent is honored; the doc is wrong).

### 13.B — Linux lavapipe goldens → v0.31.0

CI `linux-vulkan-sw` job runs but never captured a golden set.
Adds:
- A workflow_dispatch path that captures `tests/golden/lavapipe/`
  references and uploads them as an artifact.
- One-time human-run of the capture; on first land, commit the
  artifact.
- `linux-vulkan-sw` job extended with a golden compare against the
  committed lavapipe set (in addition to the smoke pass).

### 13.C — D3D12 buffer + texture + swapchain → v0.32.0

Beyond `Wave 148` boot. Adds the smallest set of methods that lets
a real `hello_d3d12_clear` sample render a color clear:
- `create_buffer` / `upload_buffer` / `download_buffer` /
  `destroy_buffer`
- `create_texture` / `destroy_texture` (no upload for now)
- `create_swapchain` / `acquire_next_image` / `present` /
  `swapchain_image` / `swapchain_image_view` / `swapchain_image_count`
- A trivial command-list path (no real PSO needed for clear)

### 13.D — Editor UI bridge → v0.33.0

The cd::editor stack uses cd::ui; the existing engine samples use
ImGui via cd::imgui_backend. Need a bridge or a fresh ImGui editor.

Decision deferred to the start of 13.D (it's a UX-shape question).
Likely outcome: a `hello_editor` sample driven by ImGui that
re-uses the World + Scene + EditHistory primitives directly. The
cd::ui-based Editor class stays for headless test coverage.

## Rejected alternatives

- *RT + mesh-shader render path as Phase 13.* Rejected: same
  silent-failure risk as Phase 12.D's reasoning. Until B1-B5 close,
  the marathon-quality bar isn't ready for risky shader-level work.
- *Open all four 13.x in parallel.* Rejected: B1-B5 is a
  prerequisite cleanup; the other three want it clean first.
- *Skip B5 (`catch(...)` audit) — it's pedantic.* Rejected: the
  reviewer specifically flagged it as a doc-vs-code-must-agree
  item. Fix in 13.A.

## Consequences

- Phase 13 runs autonomously. Stop condition for each sub-phase:
  minor tag cut.
- Phase 14 candidates listed at end of this ADR will be picked
  when 13.D closes:
  - Asset cooker improvements (BC7 quality, KTX2 mip gen)
  - Networking — actual reliable channel + replication
  - Audio — proper DSP graph + 3D positional via WASAPI
  - Mobile platforms (Android Vulkan / iOS Metal)
  - Real RT render path (BLAS/TLAS through cd::rhi)
- v1.0 path is still user-only. Phase 13 does NOT promote to v1.0
  even if all 4 axes feel met.
