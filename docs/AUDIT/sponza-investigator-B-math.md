# Q1-B Sponza investigator B — MATH / PRECISION angle

Independent, READ-ONLY adversarial investigation. No build, no edit.
Snapshot: tip of dev (commit before this audit), phases 446-456 in.

## 1. Methodology

Walked the chain "Sponza vertex bytes → world-space fragment → light
contribution" with focus on coordinate-frame / scale / precision.

Files inspected (read-only):
- `samples/engine/hello_engine/HelloGltf.hpp` — parse_gltf_result (vertex
  ingest path).
- `samples/engine/hello_engine/HelloMeshes.hpp` — boot_meshes (per-mesh
  GPU upload + BLAS build).
- `samples/engine/hello_engine/main.cpp` — `spawn_sponza_and_cesiumman_entities`
  (1838-1910), `draw_shadow_map_pass` (2385-2480), `upload_multi_light_ubo`
  (2126-2155), light SPONZA preset (4848-4925), per-prim ECS draw inside
  `draw_floor_and_entities` (3889-4192).
- `samples/engine/hello_engine/HelloLighting.hpp` — `pack_light_slot`
  (110-177).
- `samples/engine/hello_engine/shaders/prim.vert.glsl` — full file.
- `samples/engine/hello_engine/shaders/prim.frag.glsl` — `sample_shadow`
  (246-274), gltf_prim branch markers (322-322), Lit multi-light loop
  (725-858).
- `engine/foundation/math/include/cd/math/Transform.hpp` — `to_mat4`
  (62-74), `look_at` (78-99), `ortho` (117-128).
- `engine/foundation/math/include/cd/math/Matrix.hpp` — column-major
  storage docstring (1-10), `operator*(Mat,Vec)` (132-146).

## 2. Coordinate-frame audit

### 2.1 Where the 0.01 scale lives

`HelloGltf.hpp::parse_gltf_result` (lines 126-141) reads glTF vertex
positions VERBATIM into `cd::asset::PrimitiveVertex`:

```
pv.pos[0] = v.position.x;
pv.pos[1] = v.position.y;
pv.pos[2] = v.position.z;
```

There is NO scale, NO axis swap, NO matrix bake during ingest. The
loader also ignores the glTF NODE-level 0.008 scale (comment at
main.cpp 1865-1867). So Sponza vertices on the GPU vertex buffer
are in raw CENTIMETRES: roughly ±1250 cm in X, 0..1200 cm in Y,
±700 cm in Z.

The 0.01 conversion is applied via ENTITY LOCAL TRANSFORM:

```cpp
// main.cpp:1869-1872
scene.local(es.handle)->value.position = { 0.0F, 0.0F, 0.0F };
scene.local(es.handle)->value.scale    = { 0.01F, 0.01F, 0.01F };
scene.local(es.handle)->value.rotation = { 0.0F, 0.0F, 0.0F, 1.0F };
```

`cd::math::to_mat4(Transform)` (Transform.hpp:64-74) packs a
COLUMN-MAJOR S·R·T matrix. With identity rotation and zero translation,
the resulting `pc.model` is `diag(0.01, 0.01, 0.01, 1.0)`.

### 2.2 World-space fragment

In `prim.vert.glsl:33-34`:

```glsl
vec4 wp = pc.model * vec4(in_pos, 1.0);
v_world_pos = wp.xyz;
```

Column-major `M * v` follows GLSL/Vulkan convention (matches
`cd::math::operator*(Mat,Vec)` in `Matrix.hpp:132-146`). So for a
Sponza vertex at (1250, 600, -700) cm, after `model * v`:

```
v_world_pos = (12.5, 6.0, -7.0)  meters
```

So `v_world_pos` IS at METER scale. Same coordinate frame as cube
(±0.5 m), sphere (radius 1.0 m), CesiumMan (~1.8 m tall) and the
floor mesh.

### 2.3 Light positions

`HelloLighting.hpp::pack_light_slot` (110-130) packs world positions
AS-IS:

```cpp
s.pos_range[0] = L.position.x;
s.pos_range[1] = L.position.y;
s.pos_range[2] = L.position.z;
```

No coordinate transform, no scale rebase, no node-walk. So lights live
in the SAME world-space frame whose units are meters.

The SPONZA preset (main.cpp 4878-4906) places them at meter-scale:
(−6, 2, 0), (−4, 4, 0), (0, 3, −4), (0, 3, +4). All inside the
Sponza atrium volume (±12.5 m × 0..12 m × ±7 m). Range 8 m for
the lantern; 12 m for the spot. Both reach the far walls. So lights
and Sponza fragments DO share the same engine-unit space and the
distance math IS legitimate at meter scale.

Verdict: NO coordinate-frame mismatch between v_world_pos and
light_pos. Hypothesis #2 ("light positions live in different frame")
is REFUTED.

## 3. Matrix construction audit

`cd::math::Mat<T,4>` is column-major (Matrix.hpp:5-7 docstring is
explicit, GLSL/SPIR-V/Metal convention). `to_mat4(Transform)` writes:

```
m[0] = (scale.x * r3.col0, 0)
m[1] = (scale.y * r3.col1, 0)
m[2] = (scale.z * r3.col2, 0)
m[3] = (position, 1)
```

— which IS the correct column layout of T·R·S. With identity rotation
this collapses to the canonical scaling+translation matrix.

`upload_buffer(shadow_ubo, &light_vp)` at main.cpp:2428 writes
`light_vp` bytes raw. `cd::math::Mat<T,4>::cols[4]` storage is
contiguous `Vec<T,4>` columns, identical to GLSL `mat4` std140 layout.
No transpose required, no row-vs-column mismatch.

`push_constants(..., &sub_pp)` likewise blits `PrimPush.mvp` and
`PrimPush.model` byte-for-byte into the `pc` block. The GLSL push
block declares them as `mat4 mvp; mat4 model;` — column-major reads
match the C++ column-major writes.

Verdict: NO matrix layout/transpose bug. Hypothesis #4 (axis-swap or
row-vs-column-major mismatch) is REFUTED. Sponza receives the SAME
mvp/model assembly path as every other entity (`draw_floor_and_entities`
parallel prep at main.cpp 3978-4017, no Sponza-specific branch).

## 4. Shadow-map precision audit

This is the most precision-sensitive path and a credible failure
candidate. Details:

### 4.1 Shadow ortho frustum (phase 451)

`draw_shadow_map_pass` (main.cpp 2406-2428):

```cpp
const cd::math::Vec3f eye { -sd.x * 25.0F, -sd.y * 25.0F, -sd.z * 25.0F };
const cd::math::Vec3f tgt { 0.0F, 0.0F, 0.0F };
const auto light_view = cd::math::look_at(eye, tgt, up);
const auto light_proj = cd::math::ortho(-12.5F, 12.5F, -12.5F, 12.5F, 0.1F, 60.0F);
```

- Light eye 25 m from origin, looking AT origin.
- Ortho L/R/B/T = ±12.5 m. Depth range 0.1..60 m. (Vulkan depth [0,1].)
- Target is FIXED at origin (NOT camera-following) — comment at line
  6730+ says "ortho frustum is sun-direction–driven and remains scene-
  centred".

### 4.2 The Sponza fits-the-frustum boundary problem

Sponza after 0.01 scale extends approximately X∈[-12.5, +12.5],
Y∈[0, +12], Z∈[-7, +7] (raw glTF AABB / 100). The shadow ortho window
projects onto a PLANE perpendicular to the sun direction (not aligned
with world XZ). Once the sun direction tilts (e.g. default sun_dir =
(-0.35, -0.65, -0.7) normalized), the projected Sponza footprint on
the light's view plane is LARGER than ±12.5 m. Specifically the depth
of the atrium (24 m long-axis) projected obliquely onto the light's
right-axis exceeds the ortho extent.

Practical impact: for any Sponza fragment whose light-view (x,y)
falls outside [-12.5, 12.5], the shader's `sample_shadow` early-out
at prim.frag.glsl:250-251 returns 1.0 (FULLY LIT):

```glsl
if (p.x < -1.0 || p.x > 1.0 || p.y < -1.0 || p.y > 1.0 ||
    p.z < 0.0 || p.z > 1.0) return 1.0;
```

So fragments OUTSIDE the frustum are unconditionally lit by the sun
(`lit += albedo * sun_color * (sun_intensity * ndl * 1.0)`). This
EXPLAINS visible sun light on Sponza, and it also means non-sun
contribution is NOT blocked by the shadow path. Sun shadows from
non-Sponza casters that AIM outside the truncated frustum simply
disappear without artefact (no false acne) — but they ALSO don't
cast onto far Sponza receivers. Mostly cosmetic, not the root cause
of the report.

### 4.3 Sun-only shadow + bias precision (suspicious)

`sample_shadow` bias (prim.frag.glsl:264):

```glsl
float bias = max(0.0003 * (1.0 - max(dot(N, L), 0.0)), 0.00005);
float ref  = p.z - bias;
```

`p.z` is in NDC [0, 1] after the ortho divides by w=1 (depth range 60 m).
Bias floor 0.00005 NDC × 60 m = **3 mm world-space bias**. Phase451
chose this for Sponza thin walls. Sponza wall thickness ~5 cm, so the
3 mm bias is safely below wall thickness. No acne, no light leak.

For a Sponza floor fragment receiving a SUN shadow from a CesiumMan
caster, this works. ✓

### 4.4 Non-sun lights don't use the shadow map

CRITICAL OBSERVATION. `sample_shadow` is called EXACTLY ONCE per
fragment, with `Ld = -sun_dir` only (prim.frag.glsl:728). Non-sun
lights NEVER consult the shadow map — they use the inline ray-query
(line 851 et seq.). So the shadow-map precision story is IRRELEVANT
to the bug report's "non-sun lights don't react to Sponza" claim.

What gates non-sun light contribution on a Sponza fragment is:
1. `ndl = max(dot(N, Lp), 0.0)` — geometric N·L term.
2. `atten = distance_atten(d, rng)` — windowed inverse-square.
3. `cone` (spot only) — smoothstep.
4. `vis` — inline ray query against TLAS.

(1)(2)(3) are pure scalar math. (4) is the geometry-dependent visibility
test that the SPONZA preset (phase 455) explicitly fixed by moving
light positions INSIDE the atrium walls.

## 5. Why Sponza's lit path could still look dead

Even with everything above CORRECT, Sponza receivers can show
negligible non-sun contribution because:

### 5.1 N·L sign hazard from raw-vertex normals

`prim.vert.glsl:37`:

```glsl
v_world_normal = normalize((pc.model * vec4(in_normal, 0.0)).xyz);
```

With `pc.model = diag(0.01,0.01,0.01,1)`, the normal is scaled by 0.01
(uniformly across all components), then `normalize` recovers the unit
vector. Fine. ✓

HOWEVER — Khronos Sponza's INTERIOR-facing wall normals (the side a
nave-centred light should illuminate) point INWARD: +X wall normals
point in −X, etc. Combined with the light at (−6, 2, 0) and an
interior +X wall receiver at (10, 4, 0): `L_p = (−6 − 10, 2 − 4, 0) /
|..|` ≈ (−0.99, −0.12, 0) and `N` ≈ (−1, 0, 0). `dot(N, L_p) ≈ +0.99`.
Lights wall ✓.

But for the −X interior wall at (−10, 4, 0) with N≈(+1,0,0) and L_p
to lantern at (−6,2,0): `L_p ≈ (+0.97, −0.24, 0)`, dot ≈ +0.97. ✓ too.

So N·L is positive on the lit side. No flip needed.

### 5.2 Distance attenuation crushes at meter scale

`pack_light_slot` packs `s.pos_range.w = L.range` (e.g. 8 m for the
lantern). Inside `prim.frag.glsl::distance_atten` (line 134-136):

```glsl
float distance_atten(float d, float rng) {
  float w = saturate(1.0 - pow(d / rng, 4.0));
  return (w * w) / (d * d + 0.01);
}
```

A Sponza fragment 7 m away from the lantern (at the opposite end of
the nave): `d = 7`, `rng = 8`. `(d/rng)^4 = 0.586`. `w = 0.414`,
`w² = 0.172`. `/(49 + 0.01) ≈ 3.5e-3`. With light intensity
`L.intensity = 3000 lumen`, `ki = 3000 / (8π) ≈ 119.4`. Per-pixel
contribution = `albedo * color * ki * ndl * atten * vis * cone` ≈
`0.7 * 1.0 * 119.4 * 0.5 * 3.5e-3 * 1.0 * 1.0` ≈ **0.146 linear**.

That IS visible after tonemap (mid-grey ≈ 0.18). Lantern SHOULD show
at the far end.

For a fragment 1 m from the lantern: `d=1`, `(1/8)^4 = 2.4e-4`,
`w = 0.9998`, `w² ≈ 1`, `/(1 + 0.01) ≈ 0.99`. Contribution ≈
`0.7 * 1.0 * 119.4 * 0.5 * 0.99 * 1.0 * 1.0` ≈ **41 linear** — very
hot.

So the math says non-sun lights SHOULD register. The visual symptom
must be either (a) `vis = 0` (ray query rejects) OR (b) the
visibility/occlusion of the camera viewpoint relative to the lit
fragment hides it from the user.

### 5.3 Inline RT bias on Sponza receivers

`prim.frag.glsl:807` (area) and `prim.frag.glsl:851` (point/spot):

```glsl
rayQueryInitializeEXT(rq, cd_tlas, ..., 0xFFu,
                      v_world_pos + N * 0.01, 0.01, Lp, ray_tmax);
```

Origin offset = `N * 0.01` = 1 cm along normal. tmin = 0.01 = 1 cm.
For Sponza walls ~5 cm thick, the 1 cm bias keeps the origin INSIDE
the wall's half-space (good). But if N is the GEOMETRIC vertex normal
(no normal-map perturbation for glTF prims — `sample_normal_map`
gated FALSE at line 712-713 for `is_gltf_prim` unless
`fx_params4.z > 0.5`, which is normal_strength and IS > 0 when
phase452+ uploaded a normal map), the cm-bias is correct.

Edge case: TBN flip on a Sponza prim could send N "backward" by ±90°,
pushing the ray origin THROUGH the wall, with the back-side wall as
the first occluder → `vis = 0`. Sponza's per-prim normal map (phase
456) introduces this risk: glTF tangent-space normals decode to
`vec3 nm = texture(...).xyz * 2 - 1`, but `cotangent_frame(N,
v_world_pos, v_uv)` at line 716 uses SCREEN-SPACE derivatives. If
adjacent fragments span a triangle edge with mirrored UVs (very
common on Sponza columns), TBN.z can FLIP sign across the edge, the
perturbed N gets inverted, and `v_world_pos + N*0.01` becomes
`v_world_pos − N*0.01`. That's a wall-thickness fraction inside the
wall on the OCCLUDER side, immediately failing every ray.

This is a credible non-sun-shadow failure mode for Sponza
specifically (mirrored UVs on stone columns + screen-derivative TBN).

## 6. Why cube / character / spheres "work" and Sponza doesn't

- **Cube / sphere / cone / torus**: procedural primitives with
  CONTIGUOUS UVs (no mirror seams) and ABSENT normal maps for the
  Lit (tint.w==1) path → `sample_normal_map` returns FALSE because
  `pc.fx_params.y < 0.5` (texture-path flag) → TBN never built → N
  stays geometric → bias direction stable.
- **PBR demo spheres** (tint.w==3): early-exit into the W8-AQ branch
  (line 387), uses Cook-Torrance with the GEOMETRIC `safe_N` —
  identical robustness to procedural primitives.
- **CesiumMan** (tint.w==4, kGltf): single-draw glTF with `normal_strength`
  from glTF NormalTextureInfo.scale. CesiumMan ships with consistent
  UVs (no mirror seams on the body) → TBN sign stable → N perturbation
  stays within ±1° → ray-bias still hits the lit side.
- **Sponza** (tint.w==4, kSponza): per-prim normal maps (phase 456),
  ~28 prims each with potentially-mirrored UVs (columns, arches,
  curtains all use mirrored stone textures), `cotangent_frame`
  derivative discontinuity at edge → TBN flip → ray origin pushed
  through wall → `vis = 0` → non-sun light kills contribution.

## 7. Top hypothesis (Investigator B — math angle)

**Phase 456 per-prim normal-map upload, combined with the
screen-space-derivative TBN in `cotangent_frame()` at
prim.frag.glsl:716, is producing UV-mirror-induced normal flips on
Sponza columns/arches. The flipped perturbed normal feeds into the
ray-query origin offset (`v_world_pos + N * 0.01`) at line 807 and
851, placing the shadow-ray origin INSIDE Sponza wall geometry, so
every non-sun visibility ray returns vis=0.**

The N·L term and the distance attenuation are mathematically fine
at meter scale. The shadow-map ortho frustum and bias are fine. The
matrix layout and column-major / GLSL conventions are fine. The light
positions are in the SAME world frame as v_world_pos. None of the
purely-numerical / coordinate-frame issues hypothesised in items 1-4
of the brief actually fire.

The PRECISION-adjacent issue that DOES fire is the screen-derivative
TBN under mirrored-UV Sponza textures — which only became active in
phase 456 when per-prim normal maps started landing in the
descriptor. Pre-phase-456 Sponza used the (wrong-for-stone) global
Earth normal map but with the gate forced FALSE for `is_gltf_prim`
(line 712-713 prior to fx_params4.z>0.5 dispatch), so the bug was
masked. Phase 456 wired fx_params4.z to the glTF normal_scale,
unmasking the issue.

## 8. Falsifiable predictions

If hypothesis 7 is the root cause:

1. Forcing `normal_strength = 0` on every Sponza prim (setting
   `range.normal_strength = 0.0F` unconditionally at HelloGltf.hpp:241)
   should restore visible non-sun illumination on Sponza columns
   immediately, with NO other change.

2. The defect should be MORE severe on column / arch fragments and
   LESS severe on the flat atrium floor (floor UVs are non-mirrored).

3. Replacing the `N * 0.01` offset with the GEOMETRIC `safe_N * 0.01`
   (pre-normal-map normal, pulled out of the perturbation block) at
   the ray-query origin sites (line 807 / 851) should also restore
   non-sun light without disabling normal-map shading.

## 9. What I did NOT investigate (would-need-separate-pass)

- Whether the TLAS build (multi-geom BLAS, phase 465) places
  Sponza geometry at the same world-space transform that the
  raster fragment uses. (Investigator A's TLAS-instance audit
  covers this.)
- Whether descriptor-set aliasing in the per-prim path actually
  binds the right normal map per draw (Investigator C / GPU-state).
- Whether the EV / exposure / tonemap state suppresses the
  illumination after the BRDF correctly accumulates it (post-fx
  pipeline audit).
- Cross-platform behaviour (D3D12 SPIRV-Cross-translated path may
  fold the derivative differently). Vulkan-only audit.
