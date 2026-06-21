# ADR-20260621 — cd::atmosphere LUT Scope: Sky-View Promoted, Aerial-Perspective Sealed

- **Status**: Accepted
- **Date**: 2026-06-21
- **Branch**: dev
- **Deciders**: Cemal TATLI
- **Author**: Developer (cd::atmosphere FINALIZE-to-100 pass — 3rd-LUT promotion + 4th-LUT seal + sky-view test coverage)
- **Related**:
  - `docs/ADR/ADR-20260616-band4-render-features-scope.md` §2.3 (the prior SEAL
    transmittance-LUT-v1 + 4-LUT-plan + promote-on-need gate; this ADR exercises
    that gate for LUT 3 and re-seals LUT 4 with a narrower trigger).
  - `docs/PROJECT_COMPLETION_STATUS.md` §5 (`cd::atmosphere` row 70 → 100).
  - `engine/render/atmosphere/include/cd/atmosphere/Atmosphere.hpp`
    (`bake_skyview_lut` + `kSkyViewCS`).
  - `engine/render/atmosphere/tests/test_atmosphere.cpp` (sky-view test block).
  - Hillaire 2020 — *A Scalable and Production-Ready Sky and Atmosphere
    Rendering Technique* (EGSR 2020), §5.3 (multi-scattering), §5.4 (sky-view),
    §5.5 (aerial-perspective).
- **Scope guard**: This is a scope-decision + clean-gap-implementation document.
  ONLY `engine/render/atmosphere/{include,tests,README}` + this ADR + the
  `cd::atmosphere` row of `docs/PROJECT_COMPLETION_STATUS.md` were touched. The
  sealed transmittance + multi-scatter bakers, their constants, GLSL strings,
  and the `sample_*` helpers are BYTE-IDENTICAL — every change is ADD-ONLY
  (`bake_skyview_lut` was already present from the prior session; this pass adds
  `kSkyViewCS` + the sky-view test suite). No renderer bakes the sky-view LUT,
  so the default rendered sky / chrome golden is unaffected.

---

## 1. Bağlam (Context)

`cd::atmosphere` is an INTERFACE (header-only) library carrying the Hillaire
2020 atmospheric-scattering reference math: `Parameters` (Earth profiles), the
transmittance LUT baker (`bake_transmittance_lut`, 40-step optical-depth
integral, **SEALED transmittance-v1**, golden-byte-identical), and — added in
the prior session — the multiple-scattering LUT baker (`bake_multiscatter_lut`,
§5.3 / Eq. 10 geometric series) plus a CPU sky-view LUT baker
(`bake_skyview_lut`, §5.4). The two NaN bugs the prior session fixed (the
`h >= 0` altitude clamp + the `std::isfinite` sun-transmittance guard) are KEPT
and are now load-bearing for the sky-view march as well.

Hillaire's pipeline is four stacked LUTs:

1. **transmittance** (view-zenith × altitude) — view-ray attenuation.
2. **multi-scattering** (sun-zenith × altitude) — isotropic 2nd+ bounce factor.
3. **sky-view** (azimuth × zenith) — total in-scattered luminance per look-dir.
4. **aerial-perspective** (screen-xy × depth froxels) — in-scatter vs. scene
   depth, a 3D froxel LUT.

The ADR-20260616 band-4 seal deferred LUTs 2–4 as "promote-on-need" because the
2nd LUT was judged not "small+clean+testable" at that time (transmittance
lookup + spherical integral, no reference oracle). That gate has since been
exercised: LUT 2 was implemented + tested. The remaining question for THIS pass
is LUT 3 (sky-view) and LUT 4 (aerial-perspective).

## 2. Karar (Decision)

### 2.1 LUT 3 (sky-view) — PROMOTE + TEST (clean, tractable, golden-safe)

The sky-view LUT IS tractable and golden-safe, so it is finalized for real:

- **Implementation**: `bake_skyview_lut(p, transmittance, multiscatter,
  sun_dir, view_altitude_km, width, height)` — for a fixed camera altitude +
  sun direction, a 30-step single-scattering view-ray march per (azimuth,
  zenith) texel. Each step adds the two Hillaire `RaymarchScattering`
  contributions: phased direct sunlight (`rayleigh_phase` + `henyey_greenstein`,
  attenuated by the sealed transmittance LUT) and the isotropic multiple-scatter
  term `Psi` from the sealed multi-scatter LUT. Both sealed LUTs are consumed
  **read-only** via `sample_transmittance` / `sample_multiscatter`; neither
  baker's math/constants/GLSL is touched. The same `max(0, h)` + `isfinite`
  guards as §5.3 keep planet-grazing rays finite.
- **GLSL parity**: `kSkyViewCS` mirrors the CPU baker 1:1 (binding 0 = output
  image2D, binding 1 = transmittance sampler, binding 2 = multi-scatter sampler,
  sun direction + altitude pushed per-bake). It exists for the future GPU bake
  but is not dispatched today.
- **Tests**: 8 ADD-ONLY oracles — shape, finite + non-negative, zero-scattering
  → zero LUT (negative), Rayleigh-blue-dominates-away-from-sun, brighter-toward-
  sun-than-away (phase-function isolation at a fixed zenith row), sealed-tables-
  read-only (snapshot/compare), negative-altitude-clamps-finite, GLSL contract.
- **Golden safety**: OPT-IN. No renderer calls `bake_skyview_lut`, so the
  rendered sky / chrome golden cannot change.

### 2.2 LUT 4 (aerial-perspective) — SEALED promote-on-need

The aerial-perspective LUT is **formally sealed**, not abandoned. Rationale:

- It is a 3D froxel LUT (image3D, screen-xy × depth slices), structurally a
  *second projection* of the same froxel grid `cd::volumetric` already owns. A
  correct CPU reference would duplicate that grid's parameterisation without a
  consumer to pin it against.
- Unlike LUTs 1–3, its correctness oracle is inherently a **rendered-frame**
  property: it only has meaning when applied to scene geometry at a given depth
  inside an HDR composite. A CPU table with no depth-consuming consumer has no
  falsifiable per-texel invariant beyond "finite + non-negative", which is weak.
- Its real value lands with the analytical-sky / volumetric-fog consumer wiring
  (`cd::material::AnalyticalSkyMaterial`), which requires the GPU RHI dispatch
  path (image2D/image3D upload + multi-pass CS submit + barriers) and a
  golden-image (FLIP/SSIM) harness — the same cd::ddgi / cd::restir_di
  "library = CPU-ref + GLSL; activation = consumer render-path + render-review"
  pattern.

**Promote-on-need trigger**: a renderer wiring the full sky pipeline into the
live HDR composite (depth-aware in-scatter), behind a dedicated GPU render-review
+ golden re-baseline. The public surface
(`Parameters` / `bake_transmittance_lut` / `bake_multiscatter_lut` /
`bake_skyview_lut` / phase fns / `sample_*`) is stable.

## 3. Reddedilen Alternatifler (Rejected Alternatives)

- **Bake the aerial-perspective LUT now (full 4/4)**: rejected — no depth-
  consuming consumer exists to provide a correctness oracle; a CPU table with
  only finite/non-negative invariants is busy-work that risks baking a wrong
  parameterisation that nothing pins. The seal carries an explicit trigger, so
  it is deferred-by-design, not "open/incomplete".
- **Add a real RHI `.cpp` dispatch for any LUT in this pass**: rejected —
  identical to the band-4 seal rationale. Real dispatch needs an RHI device + a
  consumer render-path + a golden-image harness; out of this library's charter
  layer (the header is the correct CPU-ref + GLSL-string layer; a `.cpp` would
  be artificial here).
- **Leave sky-view untested (header-only, as the prior session left it)**:
  rejected — an implemented-but-untested baker is not charter-complete. A baker
  with no oracle can silently regress; 8 add-only tests close that gap without
  touching the rendered path.
- **Keep the row at 70 / partial**: rejected — with LUTs 1–3 implemented +
  tested and LUT 4 formally sealed with an ADR-grade trigger, the library meets
  the honest-rule "100% = IMPLEMENTED+tested OR formally SEALED" bar.

## 4. Sonuçlar (Consequences)

- (+) 3 of 4 Hillaire LUTs are CPU-baked, GLSL-mirrored, and test-pinned; the
  4th is sealed with a narrow, falsifiable promote trigger.
- (+) Sky-view baker gains 8 add-only regression oracles (negative + edge +
  phase-function + sealed-table-read-only), fail-on-revert, deterministic
  (no `sleep_for`), runs everywhere (no device).
- (+) Transmittance + multi-scatter bakers BYTE-IDENTICAL — default rendered sky
  / chrome golden unchanged (sky-view is opt-in, no renderer bakes it).
- (+) `cd::atmosphere` row 70 → 100 (honest-rule terminal state).
- (−) This pass does NOT produce the aerial-perspective LUT, the RHI dispatch
  `.cpp`, or the GPU bake of any LUT; all carry the promote-on-need consumer
  trigger above.
- (−) `kSkyViewCS` is verified only by token-contract (it is not compiled by a
  glslang pass in this library's tests); its end-to-end correctness is pinned
  the day a GPU bake consumer + parity test land — same posture as
  `kTransmittanceCS` / `kMultiScatterCS`.
