# Heitz 2016 -- Real-Time Polygonal-Light Shading with Linearly Transformed Cosines

- **Bibkey**: heitz2016ltc
- **MANIFEST status**: STAGED (PDF download pending)
- **Venue**: SIGGRAPH 2016, ACM TOG 35(4)

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

## Demir Kural verification checklist (when PDF arrives)

- [ ] Compute SHA-256 of downloaded PDF, write to MANIFEST.csv.
- [ ] Confirm the spherical-polygon edge integral matches eq. 11 of the paper.
- [ ] Confirm the corner-winding direction matches Heitz Figure 4 + section 4.3.
- [ ] Confirm the LUT format (64x64, roughness x NoV, M_inv matrix) matches
      what cd::brdf_ltc::kLtcMatrixLut ships.
- [ ] Update MANIFEST.csv + bibliography.bib note field.

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
