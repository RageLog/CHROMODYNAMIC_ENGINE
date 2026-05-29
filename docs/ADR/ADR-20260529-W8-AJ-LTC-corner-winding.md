# ADR-20260529-W8-AJ-LTC-corner-winding

Date: 2026-05-29 (backfilled retroactively; original landing commit phase ~246, Marathon Run 7).

## Context

LTC (Linearly Transformed Cosines, Heitz et al. 2016) integrates the Cook-Torrance specular BRDF over an arbitrary polygon by transforming the polygon into a cosine-distribution domain and computing the spherical-polygon irradiance form-factor. The implementation in `engine/render/brdf_ltc/include/cd/brdf_ltc/Ltc.hpp` covers triangle + quad area lights.

During the W8 area-light push (phases 226-260) the rect-area light was producing visually correct mid-roughness reflections but the **rect-corner specular response was zero** when the quad oriented edge-on to the surface. This broke the canonical Heitz reference: an edge-on quad should still produce SOME specular at glancing angles, not vanish.

## Decision

The bug was in the **winding convention** between (a) the quad-vertex order the host passed in via the rect-area light's four-corner array, and (b) the winding the LTC integral expected for the spherical-polygon irradiance step. Heitz uses CCW-around-the-receiver-normal; the sample's pre-W8-AJ code passed corners in shader-friendly screen-space order (matches the GLSL fragment shader's rect-light corner iteration).

We standardised on **CCW around the outward-facing surface normal** at the LTC integration site. The shader-side corner iteration is unchanged; the host-side conversion (in `cd::light::rect_area_corners`) now emits CCW corners.

## Consequences

- The rect-area edge-on case now produces the expected glancing-angle specular ridge. Validated visually against Heitz 2016 Figure 3 (the canonical "smooth quad at glancing" reference).
- The LTC test suite added a "rect at 89-degree-edge-on" case (cd_test_brdf_ltc, phase 247 commit) so the regression is locked.
- No GPU shader changes; all the winding work is host-side. Backwards-compatible for downstream libs that already pass CCW corners.

## Rejected alternatives

- **Flip vertex order shader-side per draw.** Rejected — would require a per-light uniform "winding flag" and add 4 conditional branches per fragment.
- **Compute winding from cross-product at integration site.** Rejected — adds ~4 ops/light at fragment-shader cost; cheaper to fix once host-side.
- **Document the host-side ordering as caller-responsibility.** Rejected — the bug was a host-side mistake; documenting it as caller-responsibility just delegates the trap.

## Reference

- Heitz E., Dupuy J., Hill S., Neubelt D. (2016) "Real-Time Polygonal-Light Shading with Linearly Transformed Cosines", SIGGRAPH 2016.
- Code: `engine/render/brdf_ltc/include/cd/brdf_ltc/Ltc.hpp` (W8-AJ change visible in commit history grep "W8-AJ").
- Test: `engine/render/brdf_ltc/tests/test_brdf_ltc.cpp`, case "rect at 89-degree-edge-on".

## Demir Kural status

Heitz et al. 2016 paper IS referenced in the `cd::brdf_ltc::Ltc::integrate_polygon` source comment but the paper is NOT yet in `research/library/MANIFEST.csv` (pdf + bibtex + notes). This ADR ships with that gap acknowledged; MANIFEST entry pending the academic-researcher pipeline (queued for Run 12).

