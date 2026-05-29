# ADR-20260529-W8-AN-Karis-representative-point

Date: 2026-05-29 (backfilled retroactively; original landing commit phase ~265, Marathon Run 7).

## Context

Area-light specular highlights via the Karis (UE4 Siggraph 2013) "representative point" technique provide a cheap approximation: instead of integrating the BRDF over the full area-light surface (expensive), pick a single point on the area-light that maximises a chosen proxy (the unclamped reflection ray closest to the area centre). The Cook-Torrance BRDF is then evaluated at that single representative point with an appropriate energy correction.

The CHROMODYNAMIC rect-area implementation pre-W8-AN was sampling the area centre directly — fine for area lights with the surface normal pointing AT the receiver, broken for off-axis cases where the reflection ray would have landed on a different part of the area-light surface.

## Decision

Adopt the Karis 2013 MRP construction:

1. Compute the unclamped reflection ray R from the receiver surface point along the BRDF lobe peak.
2. Compute the closest point on the area-light surface to R (rect: clamp to UV; sphere: project radially; tube: project orthogonally onto the segment).
3. Use that closest point as the MRP for Cook-Torrance evaluation, with the Karis energy-correction factor (`alpha` modulation) to compensate for the single-point sample missing surrounding area-light contribution.

For rect-area, the implementation lives in `cd::brdf_ltc::Ltc::karis_mrp_rect` (yes, the file name is brdf_ltc.hpp; LTC is the diffuse + integrated specular path, MRP rides for the cheaper specular-only branch).

## Consequences

- Rect-area light specular highlight now properly slides along the area-light surface as the receiver normal rotates, matching the UE4 demo reference.
- Cost: 1 dot product + 2 clamps per area light per fragment. Cheaper than full numerical integration.
- Energy-correction factor adds a one-time `alpha` precompute; CPU side.
- Validated against the Karis 2013 paper Figure 13 (cross-comparison) and the UE5 LearnUnreal MRP demo.

## Rejected alternatives

- **Full numerical integration (a la LTC).** Rejected for the rough specular branch — LTC already handles low-roughness, MRP is the cheaper fallback for medium-roughness where artists already expect a slight smear.
- **Monte-Carlo importance sampling.** Rejected for real-time; reserved for offline reference.
- **Skip MRP, rely entirely on LTC.** Rejected — LTC fidelity vs cost optimum lives roughly at the W8-AN energy-correction crossover. MRP fills the polish gap.

## Reference

- Karis B. (2013) "Real Shading in Unreal Engine 4", SIGGRAPH 2013 Physically Based Shading Course notes (UE4 PBR talk).
- Code: `engine/render/brdf_ltc/include/cd/brdf_ltc/Ltc.hpp` `karis_mrp_rect` function.
- Test: `engine/render/brdf_ltc/tests/test_brdf_ltc.cpp` "Karis MRP rect" case.

## Demir Kural status

Karis 2013 IS referenced in the source comment but the paper is NOT yet in `research/library/MANIFEST.csv`. Queued for the academic-researcher pipeline (Run 12).

