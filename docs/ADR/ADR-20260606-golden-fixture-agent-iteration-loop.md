# ADR-20260606-golden-fixture-agent-iteration-loop

Date: 2026-06-06

## Context

CHROMODYNAMIC sample binaries (`hello_engine`, future `hello_d3d12_pbr`,
etc.) already ship a **headless golden-image capture mode** that
spawns the sample, pins a CLI-selected camera, renders one
deterministic frame, dumps a PNG, and exits. The original design
target (T1.7 Phase 543) was a Tier-1 CI gate that detects "the
renderer suddenly went black" / "alphas flipped" regressions across
the five canonical Sponza fixtures.

The chrome-Sponza reflection bug fix chain (phases 796–799,
[ADR W8-BD](ADR-20260606-W8-BD-per-geom-albedo-SSBO-and-curtain-reflections.md))
discovered a second, much higher-value use of the same machinery:
**closed-loop agent-driven visual debugging**. Three observations
drove this:

1. The user's bug report ("chrome spheres in a different universe
   than Sponza") was visual — *no* text log or numeric metric could
   surface the symptom; the agent had to *see* the picture.
2. The bug had three independent root causes (per-tex factor
   collapse, kMaxGeomsPerInst clamp, grid placement). Diagnosing
   them sequentially required 5+ iteration cycles.
3. Each cycle required the user to launch the binary, take a
   screenshot, and paste it back — minutes of user latency per
   cycle. With a multi-bug stack, the user's time-to-fix scaled
   linearly with bug count.

The fix shipped a **sixth fixture (`chrome_probe`)** + **a manual-
mode pin fix** (the existing five fixtures were silently broken — the
camera override didn't latch, so `scene_cam.update()` overwrote the
pose every frame). Crucially, **the agent could read the captured
PNG back multimodally**, completing the loop without the user in it.

The session-level result: 17 commits in one continuous run, with the
user's only interactive contribution after the initial bug report
being "devam et" ("continue") halfway through.

This ADR documents the pattern as a reusable engineering methodology.
Future visual bugs (RT denoise artefacts, TAA ghosting, post-process
ordering bugs, font-glyph atlas corruption) can adopt the same
loop without re-inventing the harness.

## Decision

**Every CHROMODYNAMIC sample that visually demonstrates a feature
SHIPS a `--golden-fixture` capture path AND a dedicated agent-
iteration fixture wired with a manual-mode camera pin, a UI-disable
gate, and an opt-in scene-tweak path.**

The three sub-decisions:

### 1. CLI surface

```
--golden-fixture N          ; N is the fixture index (0..K-1)
--golden-out <path.png>     ; output PNG
--golden-frames N           ; capture-on-frame index (default 3; raise for boot-time settle)
```

When `--golden-fixture` is set:

* The fixture's `eye`, `target`, `fov_y_deg` override the default
  camera.
* `scene_cam.set_auto_spin(false)` and **`free_look.manual_mode = true`**
  (NOT `false`) — the manual-mode pin is what stops
  `scene_cam.update()` from overwriting the camera on the next
  frame. The pre-phase797 fixtures all silently regressed to walls
  because the original code path set `manual_mode = false`.
* After `end_frame` on the requested frame, the swapchain is copied
  to a host-visible buffer, BGRA-swapped to RGBA, written as PNG
  via `cd::asset::image::write_png_rgba`, then `request_shutdown()`.
* Exit code 0 on capture success, non-zero on failure.

### 2. Dedicated agent-iteration fixture

Reserve **at least one fixture slot** (today: index 5,
`chrome_probe`) specifically for **agent-side iteration**. Its job
is *not* to be a stable CI gate; it is to frame a diagnostic scene
that the agent can read back and modify in tight loops.

For each agent-iteration fixture, ship:

* **A camera pose** that frames the subject without occlusion.
* **A SUBJECT-SPAWN gate** in `on_boot` keyed to that fixture index.
  In the chrome-probe case:
  ```cpp
  if (golden::enabled() && golden::options().fixture_index == 5) {
      spawn_chrome_probe_entity(scene, entities);
  }
  ```
  The gate **never affects the default scene** — interactive runs +
  every other fixture stay byte-identical.
* **A scene-OCCLUSION suppression gate** for the same fixture index.
  Chrome-probe skips the 4 m glowing CesiumMan humanoid; future
  fixtures will skip whichever permanent entity blocks their
  subject.
* **An editor-UI gate** so the captured PNG shows the raw rendered
  scene with no ImGui draw commands. The chrome-probe path wraps
  the entire panel + overlay block in
  `if (!kHideEditorUiForGolden) { ... }`.

### 3. Test-side regression nets

For each agent-iteration fixture:

* **Lock cardinality.** A `TEST(...)` asserts the total fixture
  count and the slug at the iteration index — catches "renumbered
  the fixtures" silent breaks.
* **Lock bounds.** Assert the camera pose falls inside the
  diagnostic scene's known volume.
* **Compile-time floor for the constants the iteration was
  written to verify.** For chrome-probe this is
  `static_assert(kMaxGeomsPerInst >= kKhronosSponzaPrimCount, …)`
  in `HelloRayQuery.hpp`. A future refactor that pushes the cap
  back below the prim count fails to compile with a pointer to
  the originating ADR.

## Iteration loop in practice

```
                                     ┌─────────────────────────┐
        agent reads PNG ◄─── multimodal ◄── --golden-out path  │
              │                                                │
              ▼                                                │
        diagnose root cause                                    │
              │                                                │
              ▼                                                │
        edit shader / SSBO / scene constant                    │
              │                                                │
              ▼                                                │
        rebuild target binary  (ninja --build --preset)        │
              │                                                │
              ▼                                                │
        spawn binary with --golden-fixture N ────────────────► │
```

The user's role degenerates to *initial bug report* + occasional
"continue" / "approve" interjections. The agent's role expands to
include the *visual review* step that previously required a human.

## Consequences

### Positive

* **Tight iteration cycles.** 17 commits in one session, each cycle
  ≤ 2 minutes (build + capture + read-back). User-latency-bound
  workflows now collapse to agent-latency-bound.
* **Reusable harness.** The chrome-probe path is the template;
  future bugs (TAA ghosting, post-fx ordering, RT denoise) will
  drop new fixture entries against the same machinery.
* **Self-documenting.** The fixture set itself encodes which
  visual regimes the sample supports. A new contributor reading
  `SponzaFixtures.hpp` learns the canonical viewing angles +
  active diagnostic loops.
* **Hard guarantees against silent regression.** The
  `static_assert` + fixture-lock tests catch the *exact* refactors
  that would re-introduce the original bug class without anyone
  noticing.

### Negative

* **Fixture slot pressure.** Each new diagnostic fixture takes a
  numbered slot. The CI tier (fixtures 0..4) and the agent tier
  (fixture 5..) must stay clearly separated so a CI churn doesn't
  break the iteration loop. Today the split is implicit (fixture
  index 5 == agent slot); a future revision may bump to a tagged
  category (`Fixture::category = kCiGate | kAgentIter`).
* **Scene-occlusion skip can drift from default scene.** The
  CesiumMan skip in chrome-probe mode means the diagnostic scene
  diverges from the default. If a render-pass-order bug only fires
  when CesiumMan is present, chrome-probe won't catch it. Mitigation:
  the OTHER fixtures (0..4) keep CesiumMan; we never lose coverage.
* **PNG read-back is not free.** Each iteration cycle costs the
  agent ~1 PNG worth of context window (the image fits in the
  multimodal frame, but it's not zero). High-frequency iteration is
  fine; high-resolution (4K+) captures should downsample on write
  to control cost. Today's 1600×900 captures are well-tuned.

### Neutral / informational

* The pattern is **independent of which backend** the sample uses.
  The hello_engine implementation is Vulkan-flavoured, but a future
  hello_d3d12 implementation gets the same harness by changing only
  the swapchain-copy + PNG-write strands.
* **Determinism contract** is the same as the CI golden tier — the
  fixture must produce a byte-identical capture across runs.
  `frame_idx == capture_at_frame` is the canonical trigger.

## Rejected alternatives

1. **Use the user's interactive screenshot for every iteration.**
   What we had before this ADR. Rejected: per-cycle user latency
   bounds the fix throughput; a 3-root-cause bug took ~5 user
   round-trips. Phase 796–799 collapsed to one autonomous session
   with this pattern.
2. **Live attach to a running editor + drive the camera over an RPC.**
   Would remove the per-cycle binary launch cost. Rejected:
   cross-platform RPC pipe is non-trivial, and editor-attach adds a
   moving target (UI state, dock layout, panel side-effects) that
   defeats the determinism contract.
3. **Render via a headless rhi backend (e.g. SwiftShader / lavapipe)
   instead of the host GPU.** Would let the loop run in CI cells
   without a Vulkan device. Rejected for the iteration use case:
   software rasterisers produce visually different output (no MSAA,
   different tonemap precision) so the agent's "does it look
   right?" judgement gets confused. Useful for the CI gate
   (fixtures 0..4) when paired with a tonemap-tolerant SSIM
   threshold — already done.

## Verification

* Phase 796–799 fix chain demonstrated the loop end-to-end:
  agent → PNG → root cause → edit → rebuild → capture → next root
  cause.
* Phase 807 regression nets ensure the harness itself can't drift.
* `ctest --preset ninja-debug -R sponza_golden` runs the locked
  cardinality + bounds + scale tests.

## References

* T1.7 phase543 — original golden-image CI gate that this ADR
  extends.
* [ADR W8-BD](ADR-20260606-W8-BD-per-geom-albedo-SSBO-and-curtain-reflections.md)
  — chrome-Sponza fix chain that surfaced the agent-iteration use
  case.
* `samples/engine/hello_engine/SponzaFixtures.hpp` — fixture table
  + agent-iteration constants.
* `samples/engine/hello_engine/main.cpp:228–262` — CLI parser.
* `samples/engine/hello_engine/main.cpp:4983–5051` — fixture
  override + `manual_mode` pin.
* `samples/engine/hello_engine/main.cpp:6178–6303` — readback +
  PNG write + shutdown.
