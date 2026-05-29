# Wronski 2014 -- Volumetric Fog

- **Bibkey**: wronski2014volfog
- **MANIFEST status**: STAGED (PDF download pending)
- **Venue**: SIGGRAPH 2014 Course "Advances in Real-Time Rendering in Games"
- **Original implementation**: Assassin's Creed 4 / Frostbite Engine

## Why this paper

Wronski's froxel-based volumetric fog is the SOTA-as-of-2014 technique that
most AAA engines adopted (Frostbite, Unreal, Decima). Key idea:

1. Voxelise the view frustum into a 3D texture (typically 160x90x64 froxels).
2. For each froxel, compute participating-media density + in-scattering
   from a per-light pass (point / spot / sun shadows applied via the
   shadow maps).
3. Ray-march the front-to-back integration along the camera Z axis in a
   second compute pass.
4. Sample the resulting 3D texture in a screen-space resolve to apply fog
   to the opaque scene + transparency.

## CHROMODYNAMIC code that cites this paper

- engine/render/volumetric/include/cd/volumetric/Fog.hpp -- the API
  surface for froxel allocation + density integration.
- samples/engine/hello_engine/shaders/prim.frag.glsl -- currently uses an
  **inline exponential-height-fog approximation** (cheap stand-in, NOT
  Wronski's full froxel solution). The proper volumetric pass is gated on
  pc.fx_params3.x > 0.
- The full compute shader (cd::volumetric::kVolumetricFogCS) ships but
  is wired only behind a debug toggle pending the v1.7 frame-graph rework.

## Demir Kural verification checklist (when PDF arrives)

- [ ] Compute SHA-256 of downloaded PDF, write to MANIFEST.csv.
- [ ] Confirm the froxel layout (front-to-back exponential Z, view-frustum
      slicing) matches what we describe in Fog.hpp comments.
- [ ] Confirm the in-scattering equation (single-scattering Henyey-Greenstein
      phase function) matches our compute-shader impl.
- [ ] Confirm the temporal-reprojection step -- Wronski's 2014 paper does
      NOT include temporal; Hillaire 2018 does (see SOTA candidates below).
- [ ] Update MANIFEST.csv + bibliography.bib note field.

## State-of-the-art successor candidates (Run 18 SOTA sweep)

The Wronski 2014 baseline has been substantially improved by:

- **Hillaire 2016 "A Scalable and Production Ready Sky and Atmosphere
  Rendering Technique"** -- replaces the analytical-sky + simple fog with
  a full atmospheric LUT system (transmittance, multiple-scattering,
  sky-view LUT). Implemented in Unreal Engine 4.26+ as the default sky.
  HIGH-VALUE for a "general-purpose engine"; medium implementation cost
  (~3 compute shaders + 4 LUT textures).
- **Hillaire 2018 "Physically Based and Unified Volumetric Rendering in
  Frostbite"** -- adds temporal reprojection to Wronski's froxel fog,
  fixing the noise-stability issue. Drop-in atop our existing pipeline.
- **Bauer & Wronski 2020 "Improved Sampling for Volumetric Light Shafts"** --
  improvement to the in-scattering integration for thin light shafts.
- **Bitterli et al. 2023 "Hashed Volumetric Caches for Path-Traced
  Volumetrics"** -- research-track; out of scope for a real-time pass.

Run 20 priority recommendation: Hillaire 2018 temporal reprojection. Adds
1 history-3D-texture + ~30 GLSL lines in the resolve pass. Behavioural
change: fog stops shimmering. Risk LOW.
