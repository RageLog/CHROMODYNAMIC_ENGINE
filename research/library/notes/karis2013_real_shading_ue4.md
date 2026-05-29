# Karis 2013 -- Real Shading in Unreal Engine 4

- **Bibkey**: karis2013realshading
- **MANIFEST status**: STAGED (PDF download pending)
- **Venue**: SIGGRAPH 2013 Course "Physically Based Shading in Theory and Practice"

## Why this paper

This is the canonical SIGGRAPH-course reference for the **split-sum approximation**
to image-based lighting (IBL) used by every modern PBR engine. Karis derives:

1. The split-sum: IBL_specular = prefilter(roughness) * brdf_lut(NoV, roughness)
2. A 2-channel BRDF LUT usable by F0-tinted Fresnel as F0 * brdf_lut.x + brdf_lut.y
3. The representative-point area-light approximation (used as the specular
   complement to LTC diffuse): reflect the view ray, intersect the area-light
   plane, clamp to bounds, treat as punctual.

## CHROMODYNAMIC code that cites this paper

- samples/engine/hello_engine/shaders/prim.frag.glsl
  - "IBL split-sum (Karis 2013)" comment block.
  - "Karis 2013 rep-point" comment in the rect-area light branch.
- engine/render/ibl/include/cd/ibl/ split-sum prefilter helpers.
- docs/ADR/ADR-20260529-W8-AN-Karis-MRP.md (Karis MRP area-light ADR).

## Demir Kural verification checklist (when PDF arrives)

- [ ] Compute SHA-256 of downloaded PDF, write to MANIFEST.csv.
- [ ] Open the PDF, confirm the split-sum equation appears in the slides
      (Karis lectures it ~slide 13-17 in the published deck).
- [ ] Confirm the BRDF LUT generation pseudocode matches what
      cd::ibl::generate_brdf_lut computes.
- [ ] Confirm the rep-point construction matches what ADR-W8-AN
      describes for the rect-area light branch.
- [ ] Update MANIFEST.csv: status STAGED -> VERIFIED.
- [ ] Remove DEMIR_KURAL_PENDING_PDF note from bibliography.bib entry.

## State-of-the-art successor candidates (Run 18 SOTA sweep)

- **Fdez-Aguera 2019 "A Multiple-Scattering Microfacet Model for Real-Time
  Image-Based Lighting"** -- adds a multi-bounce term to the Karis
  approximation. Already used by Khronos's reference glTF Sample Renderer.
- **Belcour et al. 2018 "Antialiasing Physically Based Shading with LEADR
  Mapping"** -- supersedes the prefilter step for rough surfaces showing
  aliasing at distance.
- **Neural prefilter** (2024+): NeuS / instant-NGP variants used by RTX
  Remix for IBL. Unlikely drop-in for our engine but worth a survey
  paragraph in the SOTA notes.

Run 20 recommendation: Fdez-Aguera multi-scattering, ~20-line shader
patch, HIGH-VALUE + LOW-RISK.
