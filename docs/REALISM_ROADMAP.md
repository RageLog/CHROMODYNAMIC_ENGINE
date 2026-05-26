# Realism Roadmap — From v0.99.110 to AAA Photorealism

Current state: a working Cook-Torrance PBR pipeline + CSM + inline RT
shadows + multi-light UBO + Hable tonemap + measured pixel fidelity
(no white wash, pure colour preservation across lighting states).

This document tabulates what separates 'engine that draws PBR
correctly' from 'engine that looks photorealistic'. Eight phases,
ordered by visible-impact-per-effort ratio. Each phase is independent
enough to ship in isolation but the listed prerequisites should land
in roughly this order.

## Phase R1 — True IBL (highest visible jump)

What the user sees now: metallic spheres reflect a procedural cream
sky function (sample_env). All five PBR material rows look closer to
each other than they should because the env palette is too narrow.

Industry standard: HDR cubemap (e.g., from Poly Haven or in-engine
sky capture) → split-sum prefiltered specular cubemap (Karis 2013) +
diffuse irradiance map + Schlick BRDF LUT (Karis 2D table).

Ships:
- Cubemap loader (KTX2 / HDR / EXR via stb_image)
- Compute pass: convolve HDR cubemap to mip chain (specular IBL)
- Compute pass: convolve to diffuse irradiance
- Compute pass: 2D BRDF integration LUT (one-time bake)
- StandardPbrFS replaces sample_env() with texture(spec_cube, lod) +
  texture(diff_cube, N) + texture(brdf_lut, vec2(NoV, roughness))

Reference papers:
- Karis 2013 "Real Shading in Unreal Engine 4"
- Lagarde + de Rousiers 2014 "Moving Frostbite to PBR"

Visible payoff: spheres reflect actual environment chrominance. Gold
looks gold, copper looks copper, silver looks silver — the IBL
contribution carries each metal's F0 hue authentically.

Estimated effort: 1-2 days of focused work. Cubemap shader + 3 compute
kernels + descriptor wiring.

## Phase R2 — Material texture system (closes 'plastic vs metal vs wood' gap)

What the user sees now: every surface is uniform colour with no
texture detail. A scratched-metal sphere looks identical to a polished
one beyond roughness.

Industry standard: per-material albedo + normal + metallic-roughness
+ AO + emissive textures, tangent-frame normal mapping.

Ships:
- cd::asset::PrimitiveVertex extended with tangent + bitangent
- Per-mesh tangent generation (Mikktspace)
- StandardPbrFS samples albedo + MR + normal + AO textures
- glTF loader extension to bind these texture slots (cd_asset_gltf
  already has most of this; needs the FS hookup)
- Texture compression: BC7 for albedo, BC5 for normals

Visible payoff: any glTF asset (DamagedHelmet, FlightHelmet, etc) drops
in with its full material complexity intact.

Estimated effort: 2-3 days. Mikktspace integration + descriptor
expansion + GLSL sampler stack.

## Phase R3 — Off-screen frame-graph + real post-fx (closes 'inline approximations' tech debt)

What the user sees now: bloom is a luminance halo boost (single pass),
GTAO is a curvature-derivative darken, SSR is not implemented, SMAA is
FXAA-like luma blur. Each is a stand-in.

Industry standard: HDR offscreen render target → multi-mip Karis bloom
→ XeGTAO horizon-scan AO → SSR with hierarchical depth → SMAA-2x →
TAA with jitter+history → DOF with circle-of-confusion → motion blur
with velocity buffer → ACES Filmic tonemap as proper post pass.

Ships (multiple — these compose):
- Frame-graph drives the scene render INTO an HDR RGBA16F target
  (instead of straight to swapchain)
- Bloom compute chain: prefilter → 4× downsample (Karis) → 4× upsample
- GTAO compute pass with depth + normal G-buffer
- SSR compute pass with hierarchical depth (HZB)
- SMAA-2x: edge detection + blend weight + neighborhood blend passes
- TAA: jitter projection + history reprojection + neighborhood clamp
- DOF: CoC compute → bokeh blur → composite
- Motion blur: velocity buffer + radial sample
- Final tonemap + sat boost + gamma as a post pass (move out of
  prim/PBR shaders)

Visible payoff: every visual feature the engine claims to support
becomes actually high-quality rather than a screen-space hack.

Estimated effort: 5-7 days. The frame-graph rework is the main risk.

## Phase R4 — Real-time global illumination

What the user sees now: shadowed areas go fully black. No light
bouncing. Dark corners look unnaturally dark.

Industry standard: dynamic indirect lighting via either DDGI (probe
volume), ReSTIR DI/GI (RT path tracing reservoir), or VXGI (voxel
cone tracing).

Ships:
- DDGI probe grid (8×4×8 default) with RT-trace probe updates
- ReSTIR DI initial sampling + spatial/temporal reuse passes (Bitterli
  2020)
- ReSTIR GI for one-bounce indirect
- All gated by VK_KHR_ray_tracing_pipeline + an acceleration structure
  (current TLAS path covers this)

Reference papers:
- Bitterli et al 2020 "ReSTIR DI"
- Ouyang et al 2021 "ReSTIR GI"
- Majercik et al 2019 "DDGI"

Visible payoff: indoor scenes light up naturally from bounce. The
mood/atmosphere of any scene jumps to filmic quality.

Estimated effort: 4-6 days. Requires the frame-graph (R3) for proper
descriptor lifecycle, so depends on R3.

## Phase R5 — Volumetric atmosphere + sky

What the user sees now: analytical sky gradient. No volumetric fog
beyond the cheap exp-distance haze. No sun shafts. No clouds.

Industry standard: physically-based atmospheric scattering with
multiple-scattering LUT (Hillaire 2020), froxel volumetric fog
(Bartlomiej 2018), and ray-marched volumetric clouds.

Ships:
- atmosphere::Lut2D actual transmittance + multi-scatter precompute
- volumetric_fog::FroxelGrid integrate temporal scatter
- volumetric_clouds noise sampler + ray-march
- light_shafts depth-based screen-space marching for visible god rays

Visible payoff: 'is the engine outdoor-capable' becomes obviously
'yes'. Sunset/sunrise/midday all read with real lighting.

Estimated effort: 4-5 days.

## Phase R6 — Advanced BRDFs

What the user sees now: same Cook-Torrance Lambert+GGX for everything.

Industry standard: layered materials.
- Clearcoat (car paint, varnished wood) — second GGX lobe + IOR
- Sheen (velvet, fleece) — Charlie distribution
- SSS (skin, wax, marble) — Burley separable diffusion
- Anisotropic (brushed metal, hair) — anisotropic GGX
- Transmission (glass, water) — IOR + refraction

Ships:
- StandardPbrMaterial branches on material::Flags bit
- brdf_sheen_clearcoat::charlie_distribution() inline
- brdf_sss::burley_diffusion() as a separable post-blur pass
- Anisotropic GGX via tangent-aligned roughness pair

Visible payoff: glass + skin + cloth + car paint suddenly all look
right. Game character + product viz scope opens up.

Estimated effort: 3-4 days. Requires R2 (texture system) for the
clearcoat / sheen color maps.

## Phase R7 — Camera and composition realism

What the user sees now: pinhole camera, no lens character, no aperture.

Industry standard: physically-based camera + cinematic post.
- Lens flares (anamorphic / spherical)
- Chromatic aberration (per-channel UV offset)
- Vignetting (gaussian or barrel)
- Film grain (blue-noise overlay)
- Color grading LUTs (ASC CDL or .cube file)
- Auto-exposure (eye adaptation)
- Auto white balance

Visible payoff: same scene reads as cinematic vs sterile depending on
preset. User can ship different 'looks' per camera.

Estimated effort: 2-3 days.

## Phase R8 — Display + HDR

What the user sees now: 8-bit sRGB output via SDR swapchain.

Industry standard: HDR10 / scRGB output, ST.2084 PQ EOTF, BT.2020
colour space, peak luminance metadata.

Ships:
- Swapchain format detection (VK_COLOR_SPACE_HDR10_ST2084_EXT)
- Tonemap output mode switches on display capability
- Display metadata via VK_EXT_hdr_metadata

Visible payoff: HDR displays show 1000+ nit highlights, proper
contrast on real bright pixels.

Estimated effort: 1-2 days.

## Recommended ship order (priority queue)

If user wants the BIGGEST visual jump per unit work, the prioritised
list is:

1. **R1 IBL** — single biggest visual delta, 1-2 days, unlocks every
   metal material believability
2. **R2 Material textures** — drops any glTF asset in at full fidelity,
   2-3 days
3. **R6 Advanced BRDFs** — glass / skin / cloth / clearcoat each open
   new content domains, 3-4 days
4. **R3 Frame-graph + post-fx** — closes the inline-approximation
   tech debt, 5-7 days, unlocks R4-R5
5. **R4 GI** — depends on R3, 4-6 days
6. **R5 Volumetrics** — depends on R3, 4-5 days
7. **R7 Camera/composition** — sweetener for shipping demos, 2-3 days
8. **R8 HDR display** — final mile for high-end target, 1-2 days

R1 alone would dramatically change how the engine reads. The current
analytical sky is the single biggest source of 'all materials look
slightly washed' even after the saturation fixes.

## Validation gates (per phase)

Every phase ships with:
- A measurable visual test (golden-image diff or pixel-fidelity
  sampler like the one used for v0.99.110)
- Frame-time budget (target 60fps at 1080p mid-tier 2024 GPU)
- ctest covering whatever helper code lands
- Updated COLOR_FIDELITY_REPORT.md so the user can verify after each
  ship that nothing regressed.
