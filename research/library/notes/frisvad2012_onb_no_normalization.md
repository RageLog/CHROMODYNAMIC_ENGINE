# Frisvad 2012 -- Building an Orthonormal Basis from a 3D Unit Vector Without Normalization

- **Bibkey**: frisvad2012onb
- **MANIFEST status**: STAGED (PDF download pending)
- **Venue**: Journal of Graphics Tools 16(3) 2012

## Why this paper

Given a unit normal N, deriving an orthonormal tangent frame (T, B, N) is a
common operation in shading (area-light tangent + bitangent, normal mapping
without UVs, BTF reconstruction). The naive cross-product with a fixed axis
plus normalize() costs an inverse-sqrt. Frisvad presents a closed-form,
branch-light derivation that avoids the sqrt entirely for the typical case
(|N.z| > 0 threshold) and a simple fallback at the singularity. Used by
practically every modern path tracer (PBRT v4, Mitsuba) for ONB construction.

## CHROMODYNAMIC code that cites this paper

- engine/render/light/include/cd/light/AreaLight.hpp --
  cd::light::area_bitangent derivation (W7 area-light tangent basis).
- samples/engine/hello_engine/shaders/prim.frag.glsl -- formerly used a
  Frisvad branch for up_v = cross(ln, right) derivation, now uses the
  uploaded user-controlled tangent (W8-N) so the Frisvad form is the
  HOST-side fallback when no user tangent is provided.
- engine/render/brdf_ltc/include/cd/brdf_ltc/Ltc.hpp -- LTC frame
  derivation uses a guarded cross-product pattern that is morally
  equivalent to a Frisvad ONB.

## Demir Kural verification checklist (when PDF arrives)

- [ ] Compute SHA-256 of downloaded PDF, write to MANIFEST.csv.
- [ ] Confirm the branch threshold (|n.z| > -0.9999999) matches what
      our code uses.
- [ ] Confirm the algebra of the no-normalize formulae (eq. 2-5 in the
      paper) matches the implementation.
- [ ] Update MANIFEST.csv + bibliography.bib note field.

## State-of-the-art successor candidates (Run 18 SOTA sweep)

- **Duff et al. 2017 "Building an Orthonormal Basis, Revisited"** -- Pixar
  derivation that removes Frisvad's branch entirely with a sign-trick.
  Generally considered SOTA today; ~3 GLSL lines. Drop-in replacement
  for cd::light::area_bitangent. HIGH-VALUE + LOW-RISK upgrade candidate.
- **Pharr et al. 2023 "PBRT v4" section 3.3** -- surveys the ONB landscape
  and endorses Duff et al. 2017.

Run 20 recommendation: Duff 2017. Behavioural change is at machine-precision
level (different ONB orientation by ~1 ULP); visual output unchanged since
LTC integrals are rotationally symmetric on the cosine distribution.
