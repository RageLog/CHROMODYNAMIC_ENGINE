# ADR-20260529-W8-AY-bake-budget-revert

Date: 2026-05-29 (backfilled retroactively; original landing commit phase277 W8-AY, 2026-05-28).

## Context

Phase 276 W8-AX pushed the IBL bake budget to env 256 / spec base 256 / 6 mips / 64 samples / diff 32 / 32 samples in an effort to chase higher-frequency reflection detail on the PBR sphere grid. Boot delay jumped from ~7 s to ~30 s. User flagged it explicitly: "acilis uzadi" (boot got longer) AND "gorsel degismedi" (visual didn't change).

The hello_engine scene's IBL source is the **analytic procedural sky** baked once at boot. That sky has zero high-frequency content — it's a smooth Hosek-Wilkie analytic gradient plus a sun disk. Quadrupling the texel count and doubling the importance-sampling count produced no visible quality gain on this input.

Separately, the chrome PBR spheres reflected the sky cube even when the sun was OFF, because env-spec sampling ran ungated against the prebaked cubemap. User flagged this as wrong: "gunes olmadigi yerde gokyuzu yansitiyolar" (chrome reflects sky in places where there's no sun).

## Decision

**Two-part revert + first gate:**

1. **Bake budget reverted to W8-AV-like values** — env 128 / spec base 128 / 32 samples / diff 16 / 16 samples. Boot delay returns to ~7 s. Visual quality preserved because the analytic-sky source had nothing higher-frequency to encode.

2. **First env-spec gate** — both env-spec and env-diffuse now share one composite gate:
   `gate = sun_intensity * 0.6 + any_non_sun * 0.30`
   - Sun on → full IBL contribution.
   - Sun off + area only → 30 % IBL floor so chrome still hints at its surroundings ("you can still tell there's a sky").
   - Everything off → dark.

**Acknowledged not-fixed** in this phase: the architectural issue that "spheres do not reflect the character or any object in front of them" remains. Single-bounce IBL only samples the prebaked cube and never sees the scene. Real engines layer SSR + RT reflections + prebaked probes. The decision to defer to a separate phase is intentional — see ADR W8-BA for the RT reflection occlusion follow-up.

## Consequences

- Boot time returns to W8-AV baseline (~7 s).
- Visual quality unchanged versus W8-AX (the high-frequency budget was wasted on a smooth analytic source).
- Chrome no longer reflects sky when sun is fully off; 30 % floor preserves "ambient skylight" semantics for the area-light-only configuration.
- Establishes the env-spec gate as a first-class concept that subsequent phases (W8-AZ, W8-BA) refine.

## Rejected alternatives

- **Keep the W8-AX 256² budget.** Rejected: no measurable visual gain, 4× boot cost.
- **Disable env-spec entirely when sun is off.** Rejected at this phase: too aggressive — area-only configurations completely lose chrome reflection cue. W8-AZ later does adopt the strict sun-only gate; see that ADR for the rationale flip.
- **Hard-cap env mip count instead of texel size.** Rejected: mip count drives spec lobe roughness convolution; capping breaks the chrome roughness sweep.

## Reference

- Code: `samples/engine/hello_engine/HelloIbl.hpp` `bake_ibl_cpu` parameter list (env 128 / spec base 128 / diff 16).
- Code: `samples/engine/hello_engine/PrimShader.hpp` `kPrimFS` env_spec_gate formula (W8-AY through W8-AZ revision).
- Commit: phase277-W8AY(sample), 2026-05-28.

## Future work

- W8-AZ tightens the gate to sun-only (drops the 0.30 floor).
- W8-BA adds RT scene reflection occlusion for the "chrome reflects neighbors" architectural gap.
- Eventual move to PMREM-style filtered importance sampling with adaptive sample counts based on per-mip variance would let us scale the budget by content; analytic-sky case would auto-detect zero high-freq content.
