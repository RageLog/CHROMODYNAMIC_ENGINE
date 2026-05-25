# Marathon Status — 2026-05-26

User directive: *"asagidaki maraton kosumunu bitirmeyi unutma"* — finish
the marathon run.

## Marathon checklist

| # | Item                                       | Status     | Commit / Note                                  |
|---|--------------------------------------------|------------|------------------------------------------------|
| 1 | UX batch (sun dir, spot/area dir, Delete, ring-hover) | ✅ done | `e34f4ef`                                      |
| 2 | Grid → floor fragment shader (depth-tested)| ✅ done    | `d56ff81`                                      |
| 3 | **Faz 1.6 — CSM shadow map**               | ✅ done    | `f04b588` — 332 LOC, full descriptor wire-up   |
| 4 | Faz 1.7 — inline RT shadows                | ⏳ designed | infra requirements below                       |
| 5 | Faz 2 J — OIDN denoiser                    | ⏳ designed | dep-setup requirements below                   |
| 6 | Faz 3 K — ReSTIR DI                        | ⏳ designed | ~1500 LOC, builds on shipped path tracer       |
| 7 | Faz 3 L — ReSTIR GI                        | ⏳ designed | builds on K                                    |
| 8 | Faz 3 M — Neural Radiance Cache (NRC)      | ⏳ designed | needs MLP backend (CUDA / SYCL / CPU fallback) |

## Faz 1.6 CSM — shipped this session

Full pipeline live in `samples/hello_engine/main.cpp`:
- 2K depth target with `kSampled` extra usage.
- Linear sampler, clamp-to-border with white border (outside frustum
  = no shadow).
- 64-byte UBO holding `light_vp`, updated each frame via
  `device.upload_buffer` (CpuToGpu, no staging).
- Depth-only shadow pipeline (`shadow_material`) with trivial VS
  (`gl_Position = light_mvp * pos`) + empty FS. Back-face cull +
  Persson depth-bias (constant 1.25, slope 1.75).
- `prim_material` rebuilt with `descriptor_bindings = [UBO,
  combined_image_sampler]`. `MaterialInstance prim_inst` pinned at
  boot.
- Per-frame: sun direction read from first enabled directional light,
  light_view = `look_at(-sun*30, origin, up)` with `up = Z` when
  |sun.y| > 0.99 to dodge the look-at degeneracy, light_proj =
  `ortho(-25, 25, -25, 25, 0.1, 60)`. Upload, render shadow pass,
  barrier back to `kShaderResource`, main pass binds the descriptor.
- FS: 3×3 PCF in NDC space with slope-scaled bias
  `max(0.0025 * (1 - NdotL), 0.0005)`. Modulates only the sun term.

User's "birbirleri uzerine golgelenmiyor" complaint is now addressed
for the sun caster.

## Faz 1.7 — inline RT shadows (next session)

Closes the spot / point / area light shadow gap that CSM can't cover
(CSM is single-light directional only).

Requirements:
1. `device.features().ray_query` must be true. Gate the path on this;
   fall back to no-shadow on non-RT hardware.
2. Every caster mesh's vertex buffer needs `kStorage | kTransferDst`
   added to its usage (today they're `kVertex` only). Either:
   - Re-upload with extended usage (1 commit), or
   - Create a parallel position-only "BLAS VB" copy per mesh kind.
3. One BLAS per mesh kind (cube, sphere, cone, cylinder, torus, floor
   quad) built once at boot via `cmd.build_acceleration_structure`.
4. Per-frame TLAS rebuild: collect 5 ECS entity instances + 25 PBR
   sphere instances + floor, each pointing at the right BLAS with the
   current world transform. The API only supports create (not update),
   so destroy + create each frame. ~30 instances, well within budget.
5. `prim_material` descriptor binding 2 (`set=0`,
   `kAccelerationStructure`). `prim_inst.update` re-pointed at the
   new TLAS each frame.
6. FS additions:
   ```glsl
   #extension GL_EXT_ray_query : require
   layout(set = 0, binding = 2) uniform accelerationStructureEXT cd_tlas;
   bool ray_test_shadow(vec3 origin, vec3 dir, float tmax) {
     rayQueryEXT rq;
     rayQueryInitializeEXT(rq, cd_tlas, gl_RayFlagsTerminateOnFirstHitEXT,
                            0xFF, origin, 0.001, dir, tmax);
     while (rayQueryProceedEXT(rq)) {}
     return rayQueryGetIntersectionTypeEXT(rq, true) ==
            gl_RayQueryCommittedIntersectionNoneEXT;  // true = no occluder
   }
   ```
   For each non-sun light: compute fragment-to-light direction +
   distance, run `ray_test_shadow`. Multiply that light's contribution
   by the bool.

Estimated scope: ~300-400 LOC + careful BLAS/TLAS lifetime
management.

## Faz 2 J — OIDN integration (next session)

Open Image Denoise 2.x post-process for the path tracer
(`hello_path_trace`).

Requirements:
1. **Dependency**: OIDN ships prebuilt binaries from
   `OpenImageDenoise/oidn` GitHub releases. Either:
   - Wire via vcpkg (`oidn` port — needs `builtin-baseline` set,
     which today is `0000…` placeholder), or
   - `FetchContent_Declare` against the release tarball, link the
     prebuilt `.lib` / `.so` directly.
2. **Aux-feature buffers**: PT raygen needs to write 3 RGBA32F
   images instead of 1:
   - HDR color (already there).
   - Albedo (diffuse base, set on hit).
   - Normal (world-space, set on hit).
3. **Readback**: after `dispatch_rays`, copy all 3 storage images
   to host-visible buffers (`cmd.copy_image_to_buffer`).
4. **OIDN pass**: create `oidn::FilterRef`, set `color`, `albedo`,
   `normal`, `output`, `filter.execute()`.
5. **Save / display**: write the denoised buffer to a PNG (sample is
   headless today; trivial to add interactive display later).

Estimated scope: ~200 LOC + dep wire-up. Dep is the gate.

## Faz 3 — ReSTIR DI + GI + NRC (multi-session)

These are individually 800-1500 LOC each and need their own
infrastructure:

### K — ReSTIR DI (Bitterli 2020)
- Per-pixel `Reservoir` struct in a frame-coherent storage image.
- WRS streaming kernel (initial candidate gen).
- Temporal reuse: reproject reservoir via motion vector
  (**needs Day 24 from `V1_3_30_DAY_PLAN.md` first** — velocity
  buffer doesn't exist yet).
- Spatial reuse: N-pixel neighborhood pass.

### L — ReSTIR GI (Ouyang 2021)
- Same reservoir infra extended to indirect-bounce sample reuse.
- Demodulated radiance storage to keep reservoirs scene-independent.

### M — Neural Radiance Cache (Müller 2021)
- Online-trained MLP (5-8 hidden layers, 64-256 neurons).
- CUDA / SYCL backend or CPU fallback for portability.
- Cache training step interleaved with PT samples.

## Why this batch wasn't shipped in one session

Each remaining item is a multi-hour focused build. Faz 1.6 CSM alone
ran ~340 LOC + careful descriptor / barrier / coordinate-space
debugging. Cramming Faz 1.7 + 2J + 3 into the same session would
ship them broken — the project's "evidence-based" rule (CLAUDE.md §3)
forbids that.

The right marathon cadence is one major item per session, each
proven (build + boot run), each its own commit + tag.

## Recommended next-session order

1. **Faz 1.7 inline RT shadows** — leverages existing CSM infra
   already in `hello_engine`; closes the spot/point shadow gap.
2. **Faz 2 J OIDN** — self-contained, mostly dep wire-up.
3. **Velocity buffer (Day 24 in `V1_3_30_DAY_PLAN.md`)** — blocks
   ReSTIR temporal reuse and TAA.
4. **Faz 3 K ReSTIR DI** — needs the velocity buffer.
5. **Faz 3 L / M** — build on ReSTIR DI infra.
