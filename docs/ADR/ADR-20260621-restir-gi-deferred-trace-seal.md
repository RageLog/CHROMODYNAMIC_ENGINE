# ADR-20260621 — cd::restir_gi DEFERRED-TRACE GI-gather Kapsam Mührü (focused seal)

- **Status**: Accepted
- **Date**: 2026-06-21
- **Branch**: dev
- **Deciders**: Cemal TATLI
- **Author**: Developer (cd::restir_gi 70 → 100 close-out: comprehensive host-math
  coverage + FORMALLY SEAL the deferred-trace GI sample CS with Iglberger-format
  rationale — same seal class as cd::restir_di Sprint-7)
- **Related**:
  - `docs/ADR/ADR-20260616-band7-scope.md` §1 (the original BAND-7 honest seal of
    cd::restir_gi: reservoir-math-v1 SEALED + GI-trace promote-on-need). This ADR
    is the **focused, single-topic** restatement of that seal at charter-complete
    (100%), promoted out of the shared two-library band document so the GI-trace
    seal has its own findable, Iglberger-format home — mirroring how
    cd::restir_di's Sprint-7 G-buffer-seam seal is recorded as a standalone
    decision rather than only as a status-row footnote.
  - `docs/PROJECT_COMPLETION_STATUS.md` §5 (cd::restir_gi row).
  - `engine/render/restir_gi/include/cd/restir_gi/GiReservoir.hpp` (the sealed
    header — reservoir-math-v1 + the DEFERRED-TRACE block in `kRestirGiSampleCS`).
  - `engine/render/restir_di/include/cd/restir_di/Reservoir.hpp` +
    `engine/render/restir_di/include/cd/restir_di/DispatchPass.hpp` +
    `engine/render/restir_di/src/DispatchPass.cpp` (cd::restir_di, 100% — the
    proven ray-query + 4-reservoir-SSBO + 3-compute-pipeline dispatch pattern the
    promoted GI trace would reuse; even it defers the textured G-buffer-seam
    bindings, its own Sprint-7 seal).
- **Scope guard**: This ADR is a scope-decision document only. The sealed item is
  "deferred-by-design" — it no longer counts as an open gap; it carries a precise
  "promote-on-need" exit gate. The close-out pass that accompanies this ADR is
  CONFINED to `engine/render/restir_gi/tests/` (ADD-ONLY host-math tests) +
  `docs/PROJECT_COMPLETION_STATUS.md` (the cd::restir_gi row %) + this ADR file.
  **No production header/src/GLSL byte is changed** — the reservoir math and all
  three embedded GLSL kernel strings stay BYTE-IDENTICAL → public API/ABI fixed,
  hello_engine render path untouched → golden byte-identical. No out-of-scope
  library is touched (NOT cd::restir_di, NOT rhi/, NOT samples/, NOT hello_*).

---

## 1. Bağlam (Context)

`cd::restir_gi` is a header-only (INTERFACE) library — the
**reservoir-resampling-math-v1** core of ReSTIR GI (Ouyang et al. 2021). Its CPU
reference is REAL and correct and is byte-for-byte the same algebra as
`cd::restir_di` (100%, production):

- `update` — WRS streaming update (weight-sum accumulation, M count, swap rule);
- `combine` — the cross-pixel RIS estimator with a caller-supplied target-PDF
  re-evaluation functor (the M-cancellation `w = p_hat · weight_sum / pdf`
  identity, additive M growth, visibility re-test flip);
- `clamp_history` — proportional history cap that preserves `final_weight`;
- `temporal_blend` + `age` — the temporal smoothing heuristic;
- `final_weight` — the unbiased RIS denominator `weight_sum / (M · target_pdf)`.

Three matching GLSL compute-kernel strings (sample / temporal-reuse /
spatial-reuse) embed the same algebra so a render-graph consumer can compile them
unchanged.

**The gap.** The sample compute shader (`kRestirGiSampleCS`) fills each candidate
with an explicit, clearly-marked **DEFERRED-TRACE** hit — a synthesised bounce
point (`pc.camera_pos.xyz + dir`) plus a constant cached radiance (`vec3(0.1)`) —
instead of issuing a ray query against `u_TLAS`. The WRS selection below it is
exercised end-to-end, but **GI is NOT actually traced**: there is no real
secondary hit, no real incoming radiance, no radiance cache. This is documented in
the header banner, the README, and is locked by three test markers
(`DEFERRED TRACE`, `deferred-trace hit point`, `deferred-trace cached radiance`).

At the close-out baseline the reservoir math is deeply host-tested (37 cases:
5 happy-path + 32 ADD-ONLY edge/negative/invariant), but the row sat at 70 pending
(a) a final comprehensive-parity host-math sweep vs cd::restir_di's reservoir-math
test depth, and (b) a **focused, Iglberger-format** seal of the deferred trace
(the band-7 ADR sealed it inside a shared two-library document; the GI trace
deserves its own findable decision, the same way the cd::restir_di Sprint-7 seam
is a recorded decision).

## 2. Karar (Decision)

**reservoir-math-v1 SEALED + comprehensively host-tested; the real GI gather is
promote-on-need; this ADR is its focused seal.**

- **reservoir-math-v1 is the charter.** The WRS update + RIS combine + proportional
  clamp + temporal age/blend + final_weight is the genuine, reusable, load-bearing
  core. It is correct (verified vs Ouyang 2021 / Bitterli 2020) and is now
  COMPREHENSIVELY tested at every boundary — full parity with (and beyond)
  cd::restir_di's `test_restir_di_reservoir_math.cpp` depth: the combine
  positive-weight-rand-above-ratio "grow-but-keep" path, the combine
  negative-running-weight-sum inline guard (GI `update`/`combine` guard the swap
  inline with `weight_sum > 0` rather than DI `update`'s early `<= 0` return),
  multi-stream WRS determinism, non-half temporal_blend alpha lerp + truncation,
  the `0.5+ε → previous` survivor tie boundary, the `final_weight` M==1
  smallest-nonzero case, and the `Sample` member-init defaults (normal `{0,0,1}`,
  valid `1`). All deterministic (no sleep_for, no RNG), CPU-only (no Vulkan ICD).

- **The real GI gather is SEALED (promote-on-need).** Wiring a real GI gather is a
  substantial RHI-dispatch subsystem — the cd::restir_di::DispatchPass pattern:
  4 reservoir SSBOs (current / previous / temporal / spatial), 3 compute pipelines
  each with a descriptor-set + pipeline layout, per-frame push constants, a
  scene-TLAS binding for the ray query, motion-vector + G-buffer-normal texture
  bindings, a **second-bounce radiance cache** (which DI does not need at all), and
  a render-graph consumer to ping-pong + barrier the buffers and feed the final
  reservoir into the lighting integrator. cd::restir_di reached production WITH that
  whole subsystem built and STILL defers the textured G-buffer-seam bindings
  (its Sprint-7 seal); replicating it for GI plus the radiance cache is a
  multi-week feature build with **no current indirect-lighting consumer**. Building
  it now would be dead-code / over-engineering and would alter rendered output
  (an indirect bounce the golden baseline does not contain).

- **This is the focused seal.** Per the project's honest 100% rule (a charter-
  complete library is 100% when it is IMPLEMENTED + tested OR formally SEALED by a
  one-decision ADR with a precise promote-on-need gate), `cd::restir_gi` is
  charter-complete at 100%: reservoir-math-v1 implemented + comprehensively tested,
  GI-trace sealed here with its exit gate.

## 3. Reddedilen alternatifler (Rejected alternatives)

- **Implement a real `cd::restir_gi::DispatchPass` GI trace now.** Needs 4 reservoir
  SSBOs + 3 compute pipelines + descriptor/pipeline layouts + a scene-TLAS
  ray-query binding + motion-vector/G-buffer-normal textures + a second-bounce
  radiance cache + a render-graph consumer — the entire subsystem cd::restir_di
  built to reach production (and even that defers the textured G-buffer bindings).
  No consumer exists; the result would be dead code and would change rendered
  output. REJECTED — reservoir-math-v1 SEALED, GI-trace promote-on-need.
- **Synthesise a "good enough" pseudo-trace** (e.g. screen-space probe sampling)
  to claim a traced GI. That is a different, lower-quality algorithm masquerading
  as ReSTIR GI; it would still alter rendered output and would not be the Ouyang
  path. REJECTED — the honest DEFERRED-TRACE label is correct; a half-trace would
  re-introduce the misleading banner the band-7 pass removed.
- **Leave the seal only as a row footnote / shared band-7 ADR.** The GI trace is a
  named, multi-week subsystem and a recurring "why isn't GI traced?" question; it
  deserves a focused, findable Iglberger-format decision (parity with how
  cd::restir_di's Sprint-7 seam seal is recorded). REJECTED — this ADR is that
  focused home.
- **Change any production header/src/GLSL behaviour in this pass.** The whole point
  is golden byte-identical. REJECTED — this pass is ADD-ONLY tests + the row % +
  this ADR.

## 4. Sonuçlar (Consequences)

- (+) `cd::restir_gi` reaches the honest terminal 100%: reservoir-math-v1
  implemented + COMPREHENSIVELY host-tested (full parity with + beyond
  cd::restir_di's reservoir-math depth), GI-trace formally sealed here with a
  precise promote-on-need gate.
- (+) The GI-trace seal now has a focused, single-topic, Iglberger-format home
  (Context / Decision / Rejected-alternatives / Consequences) — the same seal
  CLASS as cd::restir_di Sprint-7 — rather than only a status-row footnote inside a
  shared two-library band document.
- (+) New ADD-ONLY tests pin the remaining ACTUAL host-math boundaries
  (combine grow-but-keep + inline negative-weight-sum guard + multi-stream WRS
  determinism + non-half temporal lerp/truncation + the `0.5+ε` survivor boundary +
  final_weight M==1 + Sample member-init defaults) as fail-on-revert. They assert
  NOTHING about the traced radiance — only the reservoir math + the seal markers.
- (+) Production header / src / all three GLSL kernel strings are BYTE-IDENTICAL →
  public API/ABI fixed, hello_engine render path untouched → golden byte-identical.
  No out-of-scope library touched.
- (−) This pass does NOT produce the real GI dispatch subsystem; it arrives with a
  real indirect-lighting consumer + a separate promote ADR. Accepted: the GI trace
  is genuine greenfield (multi-week RHI-dispatch + ray-query + radiance cache),
  and the honest terminal state for a charter-complete header is "seal the
  functional-v1 + deepen the real core + keep the honest DEFERRED-TRACE banner."

## 5. Promote-on-need (exit gate)

A real GI gather promotes when a real indirect-lighting consumer exists, via a
separate ADR. It is a `cd::restir_gi::DispatchPass` mirroring
`cd::restir_di::DispatchPass`:

- 4 reservoir SSBOs (current / previous / temporal / spatial) + ping-pong;
- 3 compute pipelines (sample / temporal-reuse / spatial-reuse) with descriptor-set
  + pipeline layouts + per-frame push constants;
- a scene-TLAS ray-query binding (replace the DEFERRED-TRACE block with a real
  `rayQueryEXT` gather of the secondary hit + its incoming radiance);
- motion-vector + G-buffer-normal texture bindings (the temporal-reuse + spatial
  passes already declare them);
- a **second-bounce radiance cache** (the GI-specific addition over DI);
- a render-graph consumer that barriers the buffers and feeds the final reservoir
  into the lighting integrator.

The `Sample` / `Reservoir` / `update` / `combine` / `clamp_history` /
`temporal_blend` API and the GLSL struct layout stay FIXED so the promoted trace
drops in without a re-layout.

---

## Varsayımlar

- "100% per lib" interpretation: a charter-complete library is 100% when its
  functional-v1 is IMPLEMENTED + tested OR formally SEALED by a one-decision ADR
  with a precise promote-on-need gate (the project honest rule, ADR-20260616-band7
  §"What 100% MEANS"). cd::restir_gi's functional charter is the reservoir math
  (implemented + now comprehensively tested); the real GI trace is genuine
  multi-week greenfield with no consumer → SEAL is the correct terminal call, not
  padding code.
- The close-out test pass is ADD-ONLY in
  `engine/render/restir_gi/tests/test_restir_gi_reservoir_math.cpp` (the existing
  ADD-ONLY math file). Target `cd_test_restir_gi_reservoir_math` (INTERFACE lib,
  CPU-only, runs everywhere without a Vulkan ICD).
- Golden byte-identical: this pass changes no production header/src/GLSL byte —
  only ADD-ONLY tests + the status row % + this ADR. The reservoir math and all
  three embedded GLSL kernel strings are untouched → hello_engine render path
  unaffected.
- Scope exclusion honoured: all changes under `engine/render/restir_gi/tests/` +
  `docs/PROJECT_COMPLETION_STATUS.md` (one row) + this `docs/ADR/` file. cd::restir_di
  and every other library are untouched.

## Sonraki

- Promote-on-need only: the real GI gather (`cd::restir_gi::DispatchPass` +
  radiance cache + render-graph consumer, the cd::restir_di pattern) arrives with a
  real indirect-lighting consumer + a separate promote ADR.
