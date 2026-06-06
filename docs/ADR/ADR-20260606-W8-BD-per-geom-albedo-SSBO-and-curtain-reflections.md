# ADR-20260606-W8-BD-per-geom-albedo-SSBO-and-curtain-reflections

Date: 2026-06-06

## Context

[ADR W8-BC](ADR-20260529-W8-BC-per-instance-albedo-SSBO.md) introduced a
per-frame, per-**instance** material SSBO so the chrome RT reflection
branch could colour the hit silhouette. That ADR sized the table at
`kMaxInstMats = 256` slots indexed by `rayQueryGetIntersectionInstanceIdEXT`
alone — every BLAS was implicitly single-geometry.

Phase 465 extended that contract to **per-(instance, geometry)** indexing
to support multi-geometry BLAS for the Khronos Sponza Atrium (the static-
mesh BLAS exposes one geometry per glTF primitive, so the ray-query
returns both an instance ID **and** a geometry index). The slot formula
became `slot = inst * kMaxGeomsPerInst + geom`. The 2D layout shipped
without an ADR; W8-BD backfills it together with the phase 798 cap
expansion + the phase 796 per-texture-average colour flow that finally
makes Sponza recognisable in chrome reflections.

### The chrome-Sponza disconnect

Until phase 798 the chrome PBR demo grid spheres reflected what looked
like another universe than the surrounding Sponza atrium. The
user-reported symptom — *"kureler spanza icinde degilmis gibi duruyor,
perdeleri gormem gerekiyor"* — was driven by **two compounding bugs and
one scene-layout mismatch**, all in the chrome reflection data path:

1. **Per-tex factor collapse.** Khronos Sponza authors most materials
   with `base_color_factor = (1, 1, 1)` because the real visible
   colour lives in the texture. The RT reflection SSBO cannot sample
   textures from a ray query — it only reads `cd_instance_mats.data[slot].albedo.rgb`.
   So every Sponza prim with a (1, 1, 1) factor returned **white**
   in the SSBO regardless of whether the texture was a red curtain,
   a green leaf, a blue fabric panel, or a sandstone wall. Phase 793
   patched this by collapsing the near-white factors to a single
   "representative warm-sandstone" tint — which made every prim look
   identical, killing the curtain colour separation entirely.

2. **Per-geom SSBO cap clamp.** `kMaxGeomsPerInst = 32` but the Khronos
   Sponza glTF has **103 primitives**. The fragment shader's
   `clamp(g, 0, kMaxGeomsPerInst - 1)` collapsed every geometry past
   index 31 to slot 31 — so ~70% of Sponza's prims (every column past
   the first row, every curtain past the first panel, all vegetation
   pots) read back **whatever colour happened to sit in slot 31**.
   The visible result: the chrome reflection painted a chaotic but
   spatially-uniform smear that bore no resemblance to the curtains.

3. **PBR demo grid outside the atrium.** The 4×4 metal/rough gradient
   spheres lived at world `Z = -4.5`, which is 1.5 m behind Sponza's
   `-Z` outer wall. The default camera looked through the open `+Z`
   face of the atrium past the chrome grid, so the grid's `+Z`-facing
   hemisphere reflected the IBL sky (open ceiling above) and the
   sandstone backside of the outer wall — never the curtains, which
   live on the upper-gallery edges inside the atrium.

## Decision

We commit three changes that together turn the chrome-Sponza reflection
into a recognisable image:

### 1. Per-(instance, geometry) 2D SSBO layout (W8-BC follow-on, backfilled)

```cpp
constexpr std::uint32_t kMaxGeomsPerInst = 128;  // phase798: was 32
constexpr std::uint32_t kMaxInstances    = 64;
constexpr std::uint32_t kMaxInstMats     = kMaxInstances * kMaxGeomsPerInst;
constexpr std::uint32_t kInstMatBytes    = kMaxInstMats * sizeof(InstanceMatGpu);
//                                                    = 8192 slots * 32 B
//                                                    = 256 KiB
```

Slot index in the fragment shader:

```glsl
int g        = (hit_geom < 0) ? 0 : hit_geom;
if (g >= kMaxGeomsPerInst) g = kMaxGeomsPerInst - 1;
int hit_slot = hit_inst * kMaxGeomsPerInst + g;
if (hit_slot >= kMaxInstMatSlots) hit_slot = -1;
```

Cap rationale — `128` was chosen to comfortably cover Sponza's 103
prims plus headroom for the next 2-3 multi-geometry showcase scenes
without another SSBO resize. 256 KiB is well under any consumer
GPU's storage-buffer descriptor range (typical limits ≥ 128 MiB).

### 2. Texture-average colour fold into `base_color_factor` (phase 796)

When `HelloGltf.hpp` uploads each prim's albedo texture, we now also
compute the texture's alpha-weighted average RGBA on the CPU side:

```cpp
constexpr std::uint32_t kStride = 16U;  // sparse grid — sub-ms per prim
double sum_r = 0, sum_g = 0, sum_b = 0, sum_a = 0;
for (std::uint32_t y = 0; y < gt.height; y += kStride)
{
    for (std::uint32_t x = 0; x < gt.width; x += kStride)
    {
        const std::size_t off = (y * gt.width + x) * 4U;
        const double w = static_cast<double>(gt.rgba[off + 3]) / 255.0;
        sum_r += gt.rgba[off + 0] * w;
        sum_g += gt.rgba[off + 1] * w;
        sum_b += gt.rgba[off + 2] * w;
        sum_a += w;
    }
}
if (sum_a > 0) avg = { sum_r/sum_a/255.0, sum_g/sum_a/255.0, sum_b/sum_a/255.0 };
```

The averaged colour is multiplied with the original factor and stored
back to `range.base_color_factor` — the same energy composition the
raster path uses (texture × factor). Red-curtain fabric arrives at
the SSBO as `~(0.85, 0.12, 0.08)`, green leaves as `~(0.15, 0.60, 0.10)`,
sandstone as `~(0.75, 0.60, 0.40)`, instead of every prim arriving
as `(1, 1, 1)`.

Alpha-weighting is important — cut-out textures (vegetation alpha
mask, fabric borders) would otherwise drag the mean towards black.

### 3. PBR demo grid relocated to Sponza nave centre (phase 799)

```
                phase292 (W8-AX visual lock)   phase799 (this ADR)
    z_centre    -4.5 (outside -Z wall)         0.0 (mid-court)
    y_base       0.95                          0.55
    spacing      1.70                          1.05
    scale        0.80                          0.42
```

The grid now sits at `X ∈ [-1.58, +1.58]`, `Y ∈ [0.55, 3.70]`,
`Z = 0` — comfortably under the ~5 m roof and between the column rows
at `Z ± 3`. Camera rays hit the grid spheres' `+Z`-facing hemisphere;
the chrome reflection bounces in the `-Z` direction back across the
atrium, traversing curtains, columns, vegetation. Every chrome sphere
in the default interactive view now paints recognisable Sponza
colours.

### 4. `--golden-fixture 5` iteration loop (phase 797)

A new diagnostic fixture spawns a single 0.8 m metallic=1
roughness=0.04 chrome probe at world `(4, 2.2, 0)`, pins the camera
in manual mode (the old fixture override silently lost its pose to
`scene_cam.update()` every frame), skips CesiumMan (the 4 m glowing
humanoid would occlude the probe), and suppresses the editor UI.
The captured PNG is byte-deterministic — sufficient to read back in
an agent loop for further RT iteration without human visual review.

## Consequences

### Positive

* **Chrome reflections finally show Sponza.** Curtain colours,
  vegetation greens, sandstone walls, the lion-fountain wall — all
  recognisable on every chrome sphere in the default scene.
* **SSBO scales** to 256 KiB but stays a single binding, single
  descriptor write per frame. No descriptor proliferation.
* **Per-tex avg is loader-time only.** Zero per-frame cost.
  Loader walks Sponza's 103 prims at ~0.5 ms total.
* **Iteration loop closed.** Future RT bugs in this surface area can
  be diagnosed via `--golden-fixture 5 --golden-out X.png` without
  user-visual review.
* **W8-BC backwards-compatible.** Single-geom BLAS still works —
  geoms 0..31 retain the per-instance tint by default expansion.

### Negative

* **SSBO size 8 KiB → 256 KiB.** Trivial on any modern GPU. Mobile
  Tegra at the edge could care if a tens-of-MiB instance budget is
  in play; not a concern for the current target matrix.
* **Per-tex avg loses spatial variation.** A curtain prim with a
  textured pattern (the visible damask motif on Sponza fabrics)
  reflects as a uniform colour patch — we don't sample the texture
  along the ray. Acceptable trade-off: the user can recognise
  *that* a curtain is reflected and *which* curtain colour it is.
  A higher-fidelity follow-on (sample the texture from the ray) is
  achievable via a bindless texture array + binding-N descriptor
  but adds significant complexity. Queued under L-tier (`L-rt-tex`)
  but not in scope for this ADR.
* **Grid relocation changes the visual baseline.** Any golden image
  for the PBR grid taken before phase 799 would diff against the
  new layout. We do not own such a golden today; the previous
  visual lock (W8-AX) was documented in the grid header but not in
  a CI gate. Re-baseline at the next visual snapshot cycle.

### Neutral / informational

* The `--golden-fixture 0..4` fixtures (`entrance`, `nave`, `arch`,
  `vegetation`, `floor`) authored in T1.7 phase543 were silently
  broken — every one of them looked at a wall because the camera
  override didn't pin `manual_mode`. Phase 797 fixed that for all
  six fixtures (the gate is `golden::enabled()`, not just
  `fixture_index == 5`). Pre-existing golden assets diffing those
  fixtures should be re-baselined separately.

## Rejected alternatives

1. **Sample the albedo texture from the ray hit.** Would give pixel-
   accurate curtain patterns instead of flat tints. Requires bindless
   textures or a 64-slot texture descriptor array, ray-side UV
   reconstruction from `gl_HitAttributeNV`/`rayQueryGet*BaryEXT`, and
   per-prim base-index plumbing. Deferred to L-tier — the user
   acceptance bar for the current commit is "I can see the curtains",
   not "I can read the curtain damask pattern".
2. **Drop the per-geom 2D SSBO; ship one Sponza-sized BLAS with one
   merged geometry.** Would let the original `kMaxInstMats = 256`
   stand. But it would also kill the per-prim hit-material distinction
   the whole reflection branch depends on, and break the per-prim
   alpha-test discard pattern that lets vegetation render correctly
   in the raster pass.
3. **Keep the PBR grid at Z = -4.5 and brighten the IBL fallback.**
   Would mask the symptom (white reflection → tinted reflection) but
   the chrome would still not reflect the actual Sponza geometry —
   it would just reflect a brighter sky. Rejected as a band-aid.

## Verification

* `--golden-fixture 5 --golden-out X.png`: chrome probe + 4×4 grid
  visibly reflect red / green / blue curtain panels, sandstone
  arcades, vegetation pots.
* Default interactive run (`./hello_engine.exe`): chrome spheres in
  the central grid reflect the same set, no flag required.
* `ctest --preset ninja-debug`: 254/254 PASS, including
  `cd_test_sample_sponza_golden` (Tier-1 fixtures 0..4).

## References

* [ADR W8-BA — RT reflection occlusion](ADR-20260529-W8-BA-RT-reflection-occlusion.md)
* [ADR W8-BC — per-instance albedo SSBO](ADR-20260529-W8-BC-per-instance-albedo-SSBO.md)
* `samples/engine/hello_engine/HelloRayQuery.hpp` — `kMaxGeomsPerInst`
  + `fill_inst_mat`.
* `samples/engine/hello_engine/HelloGltf.hpp` — per-prim albedo
  texture-average computation.
* `samples/engine/hello_engine/HelloTlasRebuild.hpp` — per-frame
  SSBO upload + per-geom override walk.
* `samples/engine/hello_engine/HelloPbrGrid.hpp` — grid layout
  constants (phase 799 relocation).
* `samples/engine/hello_engine/SponzaFixtures.hpp` — fixture 5
  `chrome_probe`.
* `samples/engine/hello_engine/shaders/prim.frag.glsl` — chrome
  reflection blend; `samples/engine/hello_engine/PrimShader_kPrimFS.inl`
  — the embedded mirror copy.
