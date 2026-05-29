# ADR-20260529-W8-AZ-env-spec-sun-gate

Date: 2026-05-29 (backfilled retroactively; original landing commit phase278 W8-AZ, 2026-05-28).

## Context

ADR W8-AY (immediately preceding phase) introduced a composite env-spec gate:
`gate = sun_intensity * 0.6 + any_non_sun * 0.30`

The `* 0.30` floor was intentional: keep chrome hinting at "skylight ambience" even when only an area / point / spot was on. Reasonable in isolation, but inspection with the cyan area-only + sun-off configuration revealed the failure mode:

- Sun off → `sun_intensity = 0`.
- Cyan area on → `any_non_sun = 1`.
- Gate = `0 + 1 * 0.30 = 0.30`.
- Chrome PBR spheres now sample the **sun-baked sky cubemap** at 30 % strength.
- Sky cubemap was baked with the sun ON (at boot time), so the cube **still contains the analytic sky and the sun disk** even though the runtime sun is now OFF.
- Visible result: chrome reflects a faint, ghostly bright spot where the sun was when the cube was baked. Wrong — the user expected zero sky reflection when the sun is off.

The W8-AY decision optimised for "smooth UX continuity" (don't pop chrome black when the user toggles the sun off); the correct physical answer is "no sun means no sky reflection".

## Decision

**Drop the `any_non_sun * 0.30` floor entirely. Gate env-spec strictly on sun intensity.**

New formula in kPrimFS PBR branch:
```
env_spec_gate = clamp(sun_dir.w, 0, 1);   // sun on/off binary intensity
env_spec    *= env_spec_gate;
```

Same approach for env-diffuse (the IBL diffuse term reads the prefiltered irradiance cube — also sun-baked).

Area, point, spot lights continue to drive `lit_pbr` directly via the multi-light UBO + Cook-Torrance evaluation. The PBR surface stays *lit* in an area-only scene; it just doesn't *reflect the now-stale sky cube*.

## Consequences

- **Stale-cube ghost gone.** Sun off + area on → chrome reflects nothing prebaked. Direct lighting through `lit_pbr` is unaffected; area light shows up as the proper cone-shaded highlight on the metallic spheres.
- **Sharper visual contract.** Sun = sky reflection. No sun = no sky reflection. Easy to explain to a user staring at the chrome row.
- **Tradeoff:** a pure area-only scene shows the chrome row with a dark "back side" facing away from the area light. This is *correct* (no IBL fill) but reads as "more black than artists expect". W8-BD's dielectric-ambient floor mitigates the same issue on the Lit (dielectric) path; the PBR branch keeps the strict sun gate.
- **Foundation for W8-BA.** With env-spec correctly gated, RT scene reflection occlusion can attack the next architectural gap (chrome doesn't reflect its neighbours) without fighting a leaky env-spec contribution.

## Rejected alternatives

- **Keep the 0.30 floor.** Rejected for the W8-AZ-listed reason: the floor reflects a *stale* sky cube, not the runtime scene.
- **Re-bake IBL when sun toggles.** Rejected: ~7 s re-bake is incompatible with the runtime toggle UX. Sky-cube updates need to wait for either runtime sky compute or pre-baked sun-disc parameter family with interpolation.
- **Use the sun's `intensity` field as a soft gate.** Equivalent to the W8-AY formula minus the `0.30` floor; the W8-AZ form is cleaner because the soft `sun_intensity * 0.6` was always rolled in alongside the floor.
- **Replace the cube sample with a runtime sky-shader sample.** Deferred — that's an SSR-class workflow that competes with W8-BA RT reflection. Pursue both in different ADRs.

## Reference

- Code: `samples/engine/hello_engine/PrimShader.hpp` `kPrimFS` PBR branch — `env_spec_gate = clamp(sun_dir.w, 0, 1)` line.
- Commit: phase278-W8AZ-BA(sample), 2026-05-28.

## Future work

- W8-BA closest-hit RT walks the spec reflection direction so chrome can occlude the now-clean sky sample against scene geometry.
- W8-BC layers per-instance albedo SSBO so the occluded hit shades coloured (matching neighbour albedo) instead of pure black silhouette.
- Long-term: runtime sky cubemap update (compute shader rasterising the analytic sky to a render target) lets us eventually re-introduce a "sky-only with no sun" cube — at which point a soft floor like W8-AY can return, this time correctly representing the actual no-sun atmosphere.
