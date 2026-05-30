# Sponza Light/Shadow Bug — Investigator C (Data Integrity Angle)

Adversarial investigator focused on the per-frame data-flow chain feeding
the Sponza textured-Lit branch in `prim.frag.glsl`. The other two
investigators are looking at shader logic (A) and TLAS topology (B); this
report only flags issues where the GPU receives the WRONG BYTES or
UNDEFINED MEMORY in a binding the shader actively reads.

Scope: `samples/engine/hello_engine/` — files
`HelloTlasRebuild.hpp`, `HelloTlasCompaction.hpp`, `HelloRayQuery.hpp`,
`HelloLighting.hpp`, `HelloMeshes.hpp`, `main.cpp`, and the GLSL pair
`prim.{vert,frag}.glsl`.

---

## 1. TLAS instance push order for Sponza — VERIFIED INTACT

`compact_tlas_entity_instances` (HelloTlasCompaction.hpp:74-125) is a
pure-CPU pass that:

1. Allocates parallel scratch arrays sized `entities.size()`.
2. Runs `parallel_for` over entities; each worker thread (a) calls
   `model_for(ent)` (returns `nullopt` for ghost-shadows), (b) calls
   `blas_for_kind(kind_for(ent))` (filters unregistered kinds), and on
   success writes `ent_inst_scratch[i]` + `ent_mat_scratch[i]` and sets
   `ent_valid[i]=1`.
3. Serial compaction step copies valid entries IN ENTITY ORDER into
   `out.instances` and `out.inst_mats` (both vectors stay slot-aligned).

For the Sponza scene, `entities[0]` is Sponza (set up by
`spawn_sponza_and_cesiumman_entities` at main.cpp:1857-1874) with:

* `kind = PrimitiveKind::kSponza`,
* `tint = {1, 1, 1}` (overridden to `{0.72, 0.60, 0.48}` sandstone for
  the inst-mat by the `tint_for` lambda at main.cpp:5771-5775),
* `scale = 0.01`, `position = origin`, identity rotation,
* `model_for(ent)` returns the valid 4x4 model matrix
  (`cd::math::to_mat4(lt->value)`).

`blas_for_kind(kSponza)` at main.cpp:6657-6661 returns `meshes.blas_gltf`,
which is the multi-geometry BLAS built by `build_multi_geom_blas` at
HelloMeshes.hpp:320-323 (one geometry per `gltf_prim_range`, capped at
the 32-geom-per-build limit, see comment at HelloMeshes.hpp:310-314).

After compaction the caller appends the floor at the trailing slot
(HelloTlasRebuild.hpp:118-122) via `push_inst(blas_floor, fm, {0.5,0.5,0.5})`.

VERDICT: Sponza IS pushed to the TLAS, it sits at instance_id 0 of the
compacted entity range, and the transform is the genuine 0.01-scale model
matrix.  The instance-id-to-inst_mat alignment is preserved because both
arrays are scattered + compacted with the same predicate.

## 2. Multi-light UBO content for Sponza — STRUCTURALLY OK

`upload_multi_light_ubo` (main.cpp:2132-2155) packs every enabled
non-directional `LightRow` from `s.lights` into a `LightUboGpu` blob,
clamping to `kMaxLights = 8` (HelloLighting.hpp:76). The directional
sun is automatically skipped because `pack_light_slot` returns `false`
for `LT::kDirectional` (HelloLighting.hpp:113-115).

Sponza preset (main.cpp:4878-4906) emits:

* sun directional (skipped here, drives `PrimPush.sun_dir`),
* `Tungsten lantern (2700K)` at `(-6, 2, 0)`, range 8m, 3000W,
* `Nave spot (3200K)` at `(-4, 4, 0)` pointing `(0.8,-0.5,0)`,
  range 12m, 6000W,
* `South wall cyan (8000K)` rect-area at `(0, 3, -4)` normal `(0,0,1)`,
* `North wall magenta (25000K)` rect-area at `(0, 3, +4)` normal `(0,0,-1)`.

`pack_light_slot` (HelloLighting.hpp:110-177) sets:

* `pos_range.xyz` = light world position (matches the UI rig — INSIDE
  the atrium volume per phase455-sponza-fix),
* `pos_range.w` = range for point/spot, derived from extents for area,
* `dir_type.xyz` = direction or normal, `dir_type.w` = LightType cast,
* `color_int.xyz` = colour, `color_int.w` = the W8-Y luminous-power
  product (e.g. point: phi / (8 pi) ≈ 119.4 for 3000 W; area: phi /
  (8 pi) * 0.20 ≈ 19.9 for 2500 W).

`color_int.xyz` is non-zero for all four lights (literal `{1,1,1}` or
gel colours, no chance of accidental black). `ubo.count` reaches 4
(< kMaxLights), and the `counters.set("lights_active", 4)` line is what
the user can observe in the Counters panel.

The descriptor binding 3 -> `s.lights_ubo` is wired ONCE at boot
(main.cpp:4605-4608) on `s.prim_inst`, so the GPU sees the freshly
uploaded contents on every Sponza draw IF the descriptor set the prim
sees is `s.prim_inst` itself.  See finding #4 — for ~28 Sponza prims
the bound descriptor is NOT `s.prim_inst`.

VERDICT: The light UBO is correctly packed and uploaded.  The data
itself is fine; the BINDING DELIVERY mechanism is the problem.

## 3. Textured-Lit branch descriptor binding — CORRECT FOR SPONZA RASTER PATH ONLY

For the per-prim Sponza draw at main.cpp:4088-4141:

* Each textured `GltfPrimRange` owns its own `pr.prim_inst`
  (`cd::material::MaterialInstance`) created at HelloMeshes.hpp:238 via
  `MaterialInstance::create(device, prim_material)`.
* The shader pushes `tint.w = 4.0F` (main.cpp:4015-4017) so prim.frag
  takes the `is_gltf_prim` route at line 322 — `fx_params4 = {metal,
  rough, normal_strength, view_mode}` and the MR sample bypasses
  `cd_mr_tex` (frag line 365-371).
* `fx_params[1] = pr.has_texture ? 1.0F : 0.0F` (main.cpp:4126) and
  `fx_params[3] = pr.alpha_cutoff`.
* At main.cpp:4103 `pr.prim_inst.bind(cmd, 0)` swaps the active
  descriptor set to the per-prim one — bindings 4 (albedo), 8
  (per-prim normal), 9 (per-prim MR) are correctly populated.
* After the Sponza loop, main.cpp:4153-4183 restores bindings 4/8/9 on
  the SHARED `prim_inst` so CesiumMan + PBR sphere draws below
  re-bind clean global procedural maps.

Per-prim binding 4/8/9 wiring is correct. BUT — see finding #4 — that's
**only three bindings out of eleven** the layout demands.

## 4. **SMOKING-GUN BUG: per-prim Sponza descriptor sets have UNWRITTEN bindings 0/1/2/3/5/6/7/10**

`pr.prim_inst` is built by `MaterialInstance::create` (Material.cpp:438-460).
The implementation is two lines:

```cpp
auto r = device.allocate_descriptor_set(material.descriptor_set_layout());
inst.desc_set_ = *r;
```

It ONLY allocates a descriptor set.  Vulkan descriptor sets start in
the UNDEFINED state — every binding the shader reads must be written via
`vkUpdateDescriptorSets` before the set is bound.  No copy-from-parent,
no default initialisation.

The Sponza per-prim build at HelloMeshes.hpp:246-282 then writes ONLY:

* binding 4 (combined image sampler — albedo),
* binding 8 (combined image sampler — normal-map),
* binding 9 (combined image sampler — MR map).

Bindings the shader ACTIVELY READS but that are NEVER WRITTEN to
`pr.prim_inst`:

| Binding | Type   | What the shader does with it                       |
|---------|--------|----------------------------------------------------|
| 0       | UBO    | `cd_shadow.light_vp` — CSM sun-shadow projection   |
| 1       | sampler2D | `cd_shadow_map` — CSM depth (sample_shadow)      |
| 2       | AS     | `cd_tlas` — every `ray_visibility` + `reflection_hit*` |
| 3       | UBO    | `cd_lights` — `count` + 8 LightSlots               |
| 5       | samplerCube | `cd_ibl_spec` — Karis split-sum specular         |
| 6       | samplerCube | `cd_ibl_diff` — irradiance                       |
| 7       | sampler2D | `cd_brdf_lut` — Fdez-Aguera multi-scatter LUT    |
| 10      | SSBO   | `cd_instance_mats` — per-(instance,geom) albedos   |

For every one of Sponza's ~28 textured prim draws, the GPU samples
those eight bindings from UNINITIALISED memory.  The exact symptoms
match Vulkan's "undefined behaviour" envelope perfectly:

1. **"Non-sun lights don't visibly affect Sponza"** (the user's primary
   complaint).  `cd_lights.count` is read from garbage memory.  On most
   drivers an uninitialised UBO read returns zero (the spec allows
   either zero or undefined; NVIDIA/AMD typically zero VRAM at
   allocation), so the multi-light `for (uint li = 0; li < count; ++li)`
   loop in prim.frag (line 737) runs **zero iterations** for every
   textured Sponza prim — non-sun light contribution is identically
   zero on Sponza, no matter how the lights are positioned.  The OTHER
   entities (CesiumMan single-draw, PBR spheres, procedural prims) bind
   the SHARED `prim_inst` which DOES have binding 3 wired — those
   entities are lit correctly by non-sun lights.  Hence "lights work
   everywhere EXCEPT on Sponza walls/floor".

2. **CSM sun shadow on Sponza receivers is broken / random.**  Binding 0
   `cd_shadow.light_vp` is uninitialised, so `v_shadow_pos` (vertex
   shader) is computed against garbage matrix.  `sample_shadow` then
   either returns 1.0 (early-out on out-of-frustum at frag.glsl:250-251)
   or reads garbage texels.  Symptom: Sponza walls show inconsistent
   sun-shadow coverage relative to the rest of the scene.

3. **RT shadow rays from Sponza pixels fire into a garbage TLAS.**  Binding
   2 `cd_tlas` is undefined.  Vulkan validation may flag this (VUID-…
   RayQueryNonNullAccelerationStructure) but on release builds the
   GPU executes anyway; the result is implementation-defined.  On most
   drivers ray queries against a null/garbage AS return "no hit"
   (`vis = 1.0`) — which means even if cd_lights WERE populated, every
   non-sun light would appear UNSHADOWED on Sponza receivers (i.e. no
   shadows from CesiumMan onto Sponza floor cast by the point light).

4. **Chrome RT reflection probe on Sponza pixels reads garbage albedo.**
   Binding 10 `cd_instance_mats` is undefined.  The PBR-sphere reflection
   path that uses `reflection_hit_id` then `cd_instance_mats.data[hit_slot]`
   is UNAFFECTED (PBR spheres bind the shared `s.prim_inst`), but the
   Sponza per-prim path — IF it ever evaluates the same line — would
   read garbage.  Today's prim.frag only takes the
   `reflection_hit_id`+`cd_instance_mats` branch inside the
   `tint.w==3.0` PBR sphere block (prim.frag line 387-602), so Sponza
   prims (tint.w==4.0) don't read binding 10 at runtime.  Still, the
   binding is REQUIRED by the pipeline layout so any future shader
   change that reads it on the gltf_prim path would observe garbage.

5. **IBL on Sponza walls reads garbage cubemaps.**  Bindings 5/6/7
   feed `cd_ibl_spec / cd_ibl_diff / cd_brdf_lut`.  Sponza prims DO
   reach the IBL block at frag.glsl:903 because `fx_params.y > 0.5`
   (textured flag).  Result: indoor IBL "ambient" contribution is read
   from garbage cubemap memory — typically zero, which is one more
   reason Sponza walls look pitch-black in shadow.

### Why the comment block is wrong about its own fix

The fallback path at main.cpp:4106-4117 (taken when `pr.prim_inst` is
INVALID, i.e. failed to allocate) calls `prim_inst.update({binding=4
albedo})` on the SHARED `prim_inst` and then `prim_inst.bind(cmd, 0)`.
That path WORKS for lights — because the shared `prim_inst` has all
eleven bindings wired at boot — but produces the descriptor-aliasing
bug the per-prim sets were invented to dodge.

The phase456 per-prim refactor traded **one bug (descriptor aliasing on
texture binding)** for a **strictly worse bug (eight unwritten bindings
for every per-prim set)**.  Both bugs were observable; only the latter
silently produced the user's exact symptom set, so no one connected the
dots.

### What the fix looks like (FOR REFERENCE ONLY — DO NOT IMPLEMENT IN A READ-ONLY AUDIT)

Inside HelloMeshes.hpp around line 254, the `tw` writes vector needs to
ALSO carry bindings 0/1/2/3/5/6/7/10 with the same handles the boot-time
shared-instance setup uses (main.cpp:4597-4624).  The pr.prim_inst
descriptor set must be initialised to a COMPLETE state at creation
time; per-frame rebinding (TLAS + SSBO) must then update those bindings
on EVERY per-prim set, not just `s.prim_inst`.

Alternative architectural fix: drop the per-prim descriptor sets, write
a single per-prim push constant texture index, and use Vulkan
descriptor indexing (bindless) for the albedo array — the same set is
bound once per Sponza entity, and each prim sub-draw selects its
texture via a push-constant index.  Removes the 28-set proliferation
entirely.

## 5. cd_instance_mats SSBO upload — VERIFIED CORRECT (path it controls)

`rebuild_tlas_and_transition_depth` lines 166-254 build the per-frame
`expanded` array of size `inst_n * kMaxGeomsPerInst` (= up to 64 * 32 =
2048 slots, matching `kMaxInstMats` in HelloRayQuery.hpp:65).

Indexing math walk-through:

* Sponza is at TLAS instance 0 (entity[0], compacted to instances[0]
  with floor at trailing slot).
* Default expansion (lines 191-198) fills `expanded[0..31]` with the
  sandstone fallback `{0.72, 0.60, 0.48}` (from the `tint_for` lambda
  at main.cpp:5771-5775).
* Per-geom override (lines 204-235) re-walks `entities`, finds Sponza
  at the same compacted position (tlas_idx=0 because Sponza is the
  first entity that passes both `model_for(...)!=nullopt` and
  `blas_for_kind(...).is_valid()`), gets `geom_albs = sponza_geom_albedos`
  of size `gltf_prim_ranges.size()` (~28), clamps to `kMaxGeomsPerInst=32`,
  and overwrites `expanded[0*32 + 0 .. 0*32 + 27]` with the per-prim
  glTF base-color factors.
* Slots `expanded[0*32 + 28 .. 0*32 + 31]` keep the default sandstone
  — safe because Sponza only has 28 geometries; the shader-side clamp
  `if (g >= kMaxGeomsPerInst) g = kMaxGeomsPerInst - 1` (frag.glsl:583)
  caps any rogue `geometry_index` at slot 31, which holds the safe
  sandstone fallback.

`hit_inst * 32 + hit_geom` is therefore byte-correctly aligned for
Sponza's 28 prims AND defensive for higher geometry indices.  Bounds
guard at frag.glsl:585 catches `hit_slot >= 2048` (would only fire if
`kMaxInstances` ever grew beyond 64).

VERDICT: The SSBO upload math is correct.  The SSBO content is correct.
The DELIVERY to per-prim Sponza descriptor sets is broken (see #4).

## 6. Other minor data-integrity observations (NOT root cause)

* **Vertex shader normal transform** (`prim.vert.glsl:35-37`): uses
  `pc.model * vec4(in_normal, 0.0)` instead of inv-transpose, but
  Sponza's scale is uniform 0.01 so the simplified path is correct
  modulo a constant scale factor that `normalize()` removes.  Not a
  bug for Sponza.
* **Floor sentinel collision** is FIXED at phase455-sponza-fix
  (frag.glsl:641-654): the floor block gate now uses
  `tint.w == 2.0` exactly, so Sponza's `tint.w == 4.0` no longer
  enters that branch.
* **`PrimPush` size = 256 B** (HelloLighting.hpp:68) is within the
  vendor `maxPushConstantsSize` envelope; not a truncation risk for
  Sponza prims.
* **`InstanceMatGpu` alignment** (HelloRayQuery.hpp:51-57) is exactly
  32 B with `static_assert`; matches the GLSL `struct InstanceMat
  { vec4 albedo; vec4 emissive; }` and the SSBO layout(std430) the
  shader expects.  Not the bug.
* **`LightUboGpu` packed layout** (HelloLighting.hpp:94-101): the
  `uint pad[3]` was already replaced with three discrete `uint pad_a/_b/_c`
  in the shader (frag.glsl:38-50) to match the packed C++ blob.
  This was a real bug, fixed previously.  Not the current bug.

## 7. Hypothesis ranking

| Rank | Hypothesis                                                                                     | Confidence |
|------|------------------------------------------------------------------------------------------------|------------|
| 1    | Per-prim `pr.prim_inst` for Sponza has bindings 0/1/2/3/5/6/7/10 left UNDEFINED — all RT shadow / multi-light / CSM / IBL paths read garbage for the ~28 textured Sponza prim draws. | **VERY HIGH** — code path verified, exact match to symptoms. |
| 2    | TLAS instance-id 0 collision (Sponza shares slot with default-tinted entity).                  | RULED OUT — compaction order is deterministic.            |
| 3    | `cd_lights.count` truncation / packing drift across CPU/GPU.                                   | RULED OUT — `static_assert(sizeof(LightUboGpu)==656)` holds and the packed `uint pad_a/_b/_c` fix is in shader. |
| 4    | `sponza_geom_albedos` ordering vs `gltf_prim_ranges` ordering drift.                            | RULED OUT — same vector, same index walk.                 |
| 5    | Multi-geom BLAS geometry count exceeding `kMaxGeomsPerInst=32`.                                | RULED OUT — Sponza ships 28 ranges, code comments verified. |

## 8. Top hypothesis (single sentence)

The 28 per-Sponza-prim descriptor sets (`pr.prim_inst` at
`HelloMeshes.hpp:238`) are created via `MaterialInstance::create` —
which only ALLOCATES the descriptor set — and then have ONLY bindings
4, 8, 9 written, leaving bindings 0/1/2/3/5/6/7/10 in the Vulkan
UNDEFINED state; when the per-prim set is bound for a Sponza draw, the
GPU reads garbage (typically zero on contemporary drivers) for
`cd_lights`, `cd_tlas`, `cd_shadow` + map, `cd_ibl_*`, and
`cd_instance_mats`, so the multi-light loop runs zero iterations, CSM
sun shadow is mis-projected, RT shadow rays report no-hit, and IBL is
read as black — which is EXACTLY the observable symptom "non-sun
lights and shadows have no visible effect on Sponza receivers".

---

Investigator C — data integrity angle complete.
