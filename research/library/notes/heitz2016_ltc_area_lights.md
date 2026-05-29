# Heitz 2016 -- Real-Time Polygonal-Light Shading with Linearly Transformed Cosines

- **Bibkey**: heitz2016ltc
- **MANIFEST status**: VERIFIED — `pdf/heitz2016_ltc_area_lights.pdf`
- **SHA-256**: `d89ed57d4b55f5ca4029970c07cc33d81769a14b5c3321d5c3e117abe76b9511`
- **Venue**: SIGGRAPH 2016 Technical Paper, ACM TOG 35(4)
- **DOI**: 10.1145/2897824.2925895
- **Pages**: 8 (letter, LaTeX acmsiggraph.cls, PDF v1.5)
- **Downloaded**: 2026-05-29 via Google Drive author-hosted link from eheitzresearch.wordpress.com/415-2/

## Why this paper

THE reference paper for real-time area-light shading. LTC reformulates the
Cook-Torrance specular BRDF as a cosine distribution on the sphere that
can be linearly transformed (via a 3x3 matrix indexed by roughness + NoV)
to approximate the original lobe. The polygon irradiance form factor
(spherical-polygon edge integral) then has a closed-form solution that
gives the diffuse contribution; specular plugs through the linear
transform of the same closed form.

## CHROMODYNAMIC code that cites this paper

- engine/render/brdf_ltc/include/cd/brdf_ltc/Ltc.hpp -- full implementation
  of cd::brdf_ltc::Ltc::integrate_polygon, triangle + quad area lights.
- samples/engine/hello_engine/shaders/prim.frag.glsl --
  cd_ltc_polygon_irradiance function (LTC Lambert-fit; identity inverse
  matrix -- TODO upgrade to full 64x64 LUT-driven specular per Heitz section 5.2).
- docs/ADR/ADR-20260529-W8-AJ-LTC-corner-winding.md -- winding convention
  fix for the LTC edge integral (CCW around outward normal).

## Abstract (from paper, p. 1)

"In this paper, we show that applying a linear transformation — represented by a 3x3 matrix —
to the direction vectors of a spherical distribution yields another spherical distribution, for
which we derive a closed-form expression. With this idea, we can use any spherical distribution
as a base shape to create a new family of spherical distributions with parametric roughness,
elliptic anisotropy and skewness. If the original distribution has an analytic expression,
normalization, integration over spherical polygons, and importance sampling, then these properties
are inherited by the linearly transformed distributions. By choosing a clamped cosine for the
original distribution we obtain a family of distributions, which we call Linearly Transformed
Cosines (LTCs), that provide a good approximation to physically based BRDFs and that can be
analytically integrated over arbitrary spherical polygons."

## Key technical content

- Section 3: Linearly Transformed Spherical Distributions — matrix M parametrises roughness,
  anisotropy, skewness of any base distribution.
- Section 4: LTC fit to GGX BRDF, offline BRDF fitting minimising L3 norm; 64x64 LUT indexed
  by roughness and NoV stores M_inv matrices.
- Section 5: Real-time shading application — polygon irradiance form factor, edge-integral
  formula (Eq. 11), inverse-transform the polygon, integrate clamped cosine analytically.
  Cost: O(n vertices), ~2.4 ms for 1920x1080 on GTX 980.
- Figure 4: winding convention — vertices ordered CCW when viewed from the light normal side.

## Demir Kural verification checklist

- [x] PDF downloaded: `research/library/pdf/heitz2016_ltc_area_lights.pdf`
- [x] SHA-256 computed and written to MANIFEST.csv.
- [x] Title on page 1 matches: "Real-Time Polygonal-Light Shading with Linearly Transformed Cosines"
- [x] All 4 authors confirmed on page 1: Eric Heitz, Jonathan Dupuy, Stephen Hill, David Neubelt.
- [x] DOI confirmed in paper footer: 10.1145/2897824.2925895 (SIGGRAPH '16 Technical Paper).
- [x] bibliography.bib PENDING_PDF note removed.
- [ ] Confirm the spherical-polygon edge integral in cd::brdf_ltc matches Eq. 11 (next code review).
- [ ] Confirm corner-winding convention matches Figure 4 + section 4.3 (ADR-W8-AJ covers this).
- [ ] Ship full 64x64 LUT-driven specular LTC (Run 20 target).

## State-of-the-art successor candidates (Run 18 SOTA sweep)

- **Heitz et al. 2017 "Combining Analytic Direct Illumination and Stochastic
  Shadows"** -- extends LTC with screen-space stochastic shadows.
- **Heitz et al. 2018 "Stratified Sampling of Spherical Triangles"** -- improves
  the sampling story for triangle area lights specifically.
- **Hill & Heitz 2018 "LTC Beyond" follow-up presentations** -- disk-light
  closed forms, sphere-light extension.
- **Heitz & Belcour 2019 "Distributing Monte Carlo Errors as a Blue Noise in
  Screen Space"** -- pairs well with LTC for stochastic supersampling.
- **Hart et al. 2024 "Real-time area lights with extended LTC"** -- survey
  this for any 2024-2026 drop-in upgrades.

Run 20 recommendation: ship the full 64x64 LUT-driven specular LTC.
Cost: ~30 KB GPU texture, ~15 GLSL lines. Risk: LOW.
