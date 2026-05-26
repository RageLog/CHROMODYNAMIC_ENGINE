# CHROMODYNAMIC Engine — Roadmap v1.4 → v2.0

- **Plan date:** 2026-05-26
- **Current tag:** `v0.99.93` (+ ~62 uncut patches since)
- **Branch:** `dev`

This is the forward-looking plan. It opens with **Section A —
priority gap closure** (the visible-now bugs + missing features the
sample exposes) and then lays out the canonical version ladder from
v1.4 to v2.0.

---

## Section A — Priority gaps (close these FIRST)

Each gap is something the sample currently shows as broken or
missing. Order is by user pain × ship size.

| # | Gap                                                | Pain    | Effort | Where                                |
|--:|----------------------------------------------------|---------|--------|--------------------------------------|
| 1 | **glTF texture sampling (PBR + baseColor)**        | high    | M      | prim shader descriptor refactor + cd::asset_gltf textures wire |
| 2 | **Multi-light contribution in prim FS**            | high    | M      | UBO array of `cd::light::Light`, FS loop |
| 3 | **LTC area-light shading (proper, not point-fake)** | high    | M      | cd::brdf_ltc → prim FS                |
| 4 | **Renk hâlâ pastel/yıkanık** — wire `cd::post_tonemap` AGX/Hill operator switcher to a palette command | mid | S | hello_engine + palette command |
| 5 | **Gizmo teleport bug**                             | mid     | S      | hello_engine gizmo drag math (plane-anchor world delta) |
| 6 | **Lights ECS entity model** — drag-drop, Delete, gizmo uniformly | high    | L      | cd::editor_ui + cd::scene integration |
| 7 | **Asset palette (drag-drop)**                      | high    | L      | cd::editor_ui::AssetPalette already shipped; wire to viewport drop site |
| 8 | **Bloom + post stack in hello_engine viewport**    | mid     | M      | cd::post_bloom + cd::post_taa + cd::post_gtao wire |
| 9 | **Sun deletable / configurable from scratch**      | mid     | S      | lights[] default seed via palette, no hard-codes |
| 10 | **Per-spot/area gizmo drag direction**            | mid     | S      | viewport overlay for non-translate gizmo on light vector |
| 11 | **Color anomaly investigation (root cause)** — AGX shipped but other issues may remain | mid     | S      | StandardPbrPush sun-tint double-apply suspect |
| 12 | **OpenGL/D3D12 backends do not run shadows / RT** | mid     | L      | rhi_d3d12 / rhi_opengl parity vs vulkan |
| 13 | **glTF texture support for prim shader**          | high    | M      | (same as #1, dup for visibility)      |
| 14 | **Sound positional placement (world entity)**      | mid     | M      | cd::audio::SourceComponent + ECS     |
| 15 | **Scene save / load with new entities + lights**  | low-mid | S      | extend cd::asset_json hello_engine.cdscene.json |

S = ≤ 1 day, M = 1-3 days, L = ≥ 1 week, XL = ≥ 1 month.

---

## v1.4 — Integration & Visible Polish (target: 2-3 weeks)

**Theme:** "wire what we shipped overnight into the actual sample".
Most of v1.3's libraries exist in isolation; v1.4 makes them
visible.

| Day | Library wired in            | Result the user sees                                  |
|----:|-----------------------------|-------------------------------------------------------|
|  1  | cd::post_tonemap            | Palette command "Tonemap: AGX/Hill/Hable/Narkowicz" — runtime switch |
|  2  | cd::post_bloom              | HDR bloom on bright pixels (lights, sun)              |
|  3  | cd::post_gtao               | Real AO darkening creases between objects             |
|  4  | cd::post_ssr                | Reflective floor on glossy fragments                  |
|  5  | cd::post_motion_blur        | Spinning entity motion-blurs                          |
|  6  | cd::post_taa                | Aliasing gone on edges                                |
|  7  | cd::post_smaa fallback      | Alt to TAA when motion vectors aren't reliable        |
|  8  | cd::post_dof                | DOF picker per focus distance + aperture              |
|  9  | cd::hdr_display             | HDR10 / scRGB swapchain (when monitor reports support)|
| 10  | cd::atmosphere              | Hillaire 2020 sky replaces analytical                 |
| 11  | cd::volumetric_fog          | Sun shafts through fog                                |
| 12  | cd::volumetric_clouds       | Procedural clouds in the sky                          |
| 13  | cd::light_shafts            | Sun god-rays after fog                                |
| 14  | cd::brdf_ltc                | Rect/disk area lights shade correctly                 |
| 15  | cd::brdf_sss                | Subsurface scattering material example                |
| 16  | cd::brdf_sheen_clearcoat    | Cloth + car-paint material samples                    |
| 17  | cd::decal                   | Drop decals on geometry                               |
| 18  | cd::gpu_particles           | Particle emitter at light position                    |
| 19  | cd::velocity                | Velocity buffer (Day 6/7/11 prereq)                   |
| 20  | cd::mesh_shader             | Bench against vertex pipeline                         |
| 21  | cd::denoise                 | Wire into hello_path_trace                            |

Tag bumps: one v0.99.x patch per wire-in. End of v1.4 = v1.3.0 cut
(implementer flags ready; user pulls the trigger).

---

## v1.5 — GI Track (4-6 weeks)

**Theme:** "make the indirect bounce real".

- **cd::restir_di** wire-up: integrate the reservoir per-pixel pass
  with the path tracer + add a temporal-reuse motion-vector source.
- **cd::restir_gi** wire-up: same for indirect bounces.
- **cd::ddgi** runtime: probe grid placed once at scene-load; per-
  frame probe update via ray query (RT) or precomputed (raster).
- **cd::nrc** real backend: TinyCudaNN binding behind
  `CD_NRC_BACKEND=tinycudann`. Online MLP training during PT
  warm-up.

Sample: `hello_path_trace_restir` — the path tracer with
DI + GI + NRC enabled; should converge in seconds instead of
hundreds of samples.

---

## v1.6 — Editor v1 (6-8 weeks)

**Theme:** "the sample becomes a real editor".

Implements every recommendation in `docs/EDITOR_LESSONS_LEARNED.md`:

- **cd::editor_ui::ScenePanel** — hierarchical scene tree, multi-
  select, drag-reparent.
- **cd::editor_ui::AssetPalette** UI wire — right dock, drag from
  palette into viewport spawns at raycast-hit position with a
  wireframe ghost preview.
- **cd::editor_ui::Inspector** — type-erase, panel-per-component
  (Transform, MeshRenderer, Light, AudioSource).
- **cd::editor::SpawnCommand / DespawnCommand / ComponentEdit
  Command** — fully undoable via EditHistory.
- **cd::audio::SourceComponent** — positional audio as an entity.
- **cd::light::LightComponent** — every light an entity; Sun
  becomes deletable.
- **cd::editor::PaletteRegistry** content-pack manifest format.
- **cd::editor_ui::ViewportGizmo** — plane-anchor world-delta math
  to fix the gizmo-teleport bug.
- **Save/load with scene presets** — Cornell Box, Sponza, custom
  user scenes.

---

## v1.7 — Geometry & Streaming (4-6 weeks)

- **cd::virtual_geometry**: real offline meshlet builder
  (simplification + clustering + DAG export). Replaces the
  pick-only library skeleton with a working builder.
- **cd::virtual_textures**: actual streaming page atlas with
  feedback-buffer consumer (CPU-side decode + upload + page-table
  update).
- **cd::asset_streaming** integration: every mesh + texture import
  goes through the scheduler. Memory budget UI in inspector.
- **Render thread + frame-graph rework**: cd::framegraph already
  exists but isn't load-bearing yet. v1.7 makes every render pass
  declared in the frame graph.
- **Multi-queue submit**: async compute for the heavy ones
  (atmosphere LUT bakes, particle simulation, DDGI probe update).

---

## v1.8 — Cross-platform parity (6-8 weeks)

- **D3D12 backend** — feature parity with Vulkan: DXR pipeline (the
  v1.2-deferred steps 4+5), mesh shaders, ray-query,
  acceleration-structure update mode.
- **OpenGL backend** — depth-only / shadow pipeline, then fall back
  forever (GL doesn't have RT). Useful for "runs anywhere" path.
- **Metal backend** — hits the same bar as Vulkan on macOS
  hardware. Apple Silicon argument-buffer + indirect-command path.
- **WebGPU backend** — browser deployment. Probably opt-in CMake
  option (`CD_ENABLE_WEBGPU`) so the engine compiles without it.

Sample: hello_path_trace runs identically on Vulkan + D3D12 + Metal.

---

## v1.9 — Physics & Gameplay (8-10 weeks)

- **cd::physics**: integrate Jolt under
  `engine/world/physics/`. Today there's a stub.
- **Continuous Collision Detection** for fast-moving entities.
- **Cloth + soft-body** via Jolt's `SoftBody`.
- **Character controller** — capsule + jump + step-up.
- **Animation retargeting** — `cd::anim` already has the skeleton +
  pose blend; add retargeting + IK (FABRIK).
- **Behaviour trees / state machines** via `cd::ai` (new).
- **Scripting** — `cd::script` is a stub today; pick a lang
  (Lua via sol2 / WASM via wasm3) and wire it.

---

## v2.0 — Production milestone (10-12 weeks)

**Theme:** "ship-it-quality" — the user can take this engine to a
shipping game studio.

- **Tooling**:
  - Asset pipeline + Asset Browser (Filesystem + cd::asset_pak
    integration + thumbnail bake).
  - Profiler (in-game frame graph + per-pass GPU timing + memory).
  - Crash reporter (minidump on Windows, breakpad on Linux/macOS).
- **Networking** — rollback + state-sync foundation already exists;
  add netcode samples (lockstep + client-prediction + delta-
  compression already shipped, but missing UI layer).
- **Audio** — convolution reverb + HRTF + ambisonics. Today only
  has positional mono + DSP chain.
- **Localization** — string table + RTL support.
- **Accessibility** — colour-blind modes, subtitle system, input
  remapping API.
- **Hot-reload** for shaders, scripts, audio, textures. Some pieces
  exist (cd::watch is there).
- **Stability** — 24-hour stress harness on the path tracer +
  editor. Zero leaks, no GPU hang.

**v2.0 ship gate:** can a small indie studio start a new game
project with CHROMODYNAMIC and not need a parallel engine.

---

## Cross-cutting / always-on

These run continuously alongside the version milestones:

- **ADR per major decision** — `docs/ADR/ADR-YYYYMMDD-*.md`.
- **Q1-grade SOTA literature trail** — `research/library/MANIFEST.
  csv` for every academic citation, BibTeX + PDF.
- **Test count never drops**. Currently 87 binaries / 100% pass.
- **No `--no-verify`, no `catch(...) {}`, no raw `new`** — the
  CLAUDE.md zero-tolerance rules.
- **Multi-platform compiler matrix** — MSVC + Clang-cl + Clang +
  GCC; tested in CI.
- **Sanitizer matrix** — ASAN + UBSAN + TSAN on the debug preset
  rotation.

---

## Version-gate policy (carried from v1.0)

- Every library / sample ship = one `v0.99.x` patch tag.
- Every `v1.x.0` minor tag = user pulls the trigger after the
  implementer flags "ready". Implementer does **not** auto-bump
  majors. (Per
  `feedback_long_autonomous_marathon` in memory.)
- `v2.0.0` = user-only too. Requires the v2.0 ship-gate criteria
  above + a clean 24h stress run.

---

## Open questions for the user

These are choices the implementer should not make autonomously:

1. **Editor name**: production editor library is currently
   `cd::editor_ui`. Production-grade name? Keep or rename?
2. **Scripting language pick** (v1.9): Lua via sol2, or WASM via
   wasm3, or AngelScript? Personal preference.
3. **Physics**: Jolt is the obvious pick. Sticking with it, or
   PhysX/Bullet?
4. **Asset pipeline UI** (v2.0): Standalone window or in-editor?
5. **Networking transport** (v2.0): GameNetworkingSockets (Valve),
   ENet, or raw UDP custom?
6. **License + open-source target** (pre-v2.0): MIT? Apache 2?
   Source-available?

---

## "Right now" recommendation

Next 5 days of work — assuming user OK'd v1.4 entry:

1. Wire `cd::post_tonemap` as a palette runtime switcher.
2. Wire `cd::post_bloom` (visible HDR halo).
3. Wire `cd::post_gtao` (creased shadows in cube corners).
4. Wire `cd::brdf_ltc` (area light shades right).
5. Texture sampling in prim shader → glTF baseColor map.

Each is a single commit + sample screenshot. By end of week the
sample looks visually distinct from where it was this morning.
