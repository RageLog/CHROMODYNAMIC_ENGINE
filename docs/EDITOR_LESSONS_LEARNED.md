# CHROMODYNAMIC Editor — Lessons Learned

- **Source date:** 2026-05-26
- **Source sample:** `samples/hello_engine` (the mega-showcase editor
  prototype built across Faz 1.5 / 1.6 / 1.7 marathon)
- **Why this doc exists:** User explicitly asked — *"Ogrenilmis
  dersler olarak bunlar tutulsun gercek motor editoru yapildiginda
  bu bilgiler bosa gitmesin"*. When the production editor lands as
  its own library (`engine/editor_ui/` and friends), every concrete
  pain point and design call captured here should be carried over.

## 1. Pain points surfaced during the marathon

| # | Symptom                                                         | Root cause                                                                                  | Production fix                                                                  |
|---|-----------------------------------------------------------------|---------------------------------------------------------------------------------------------|---------------------------------------------------------------------------------|
| 1 | "Gizmo ile ileri geri yapinca objeler isinlaniyor"              | Per-axis drag math runs against the gizmo's *projected* origin without rebinding the anchor point when the user releases + re-grabs mid-action. Tiny screen-space deltas project to large world-space jumps when the camera is grazing the axis. | Cache `(world_anchor, screen_anchor, plane_normal)` at `begin_drag`. On each tick, reproject the mouse onto the same plane and compute `delta_world = projected - world_anchor`. Use raycast-onto-plane, not perspective-divided screen pixel math. |
| 2 | Sun direction control is unintuitive                            | xyz sliders force the user to think in Cartesian; the visible effect is azimuth + elevation. | Replace with `azimuth ∈ [0, 2π]` + `elevation ∈ [-π/2, π/2]` knobs. Optionally drag the sky-dome marker live with the mouse. |
| 3 | Spot light effect not visible                                   | The CSM path only honours the first enabled directional light; the spot's contribution lands in the prim shader's point-light slot, then ray-shadowed in Faz 1.7 — but the shader has no spot-cone math, so the spot becomes a point. | Add cone-attenuation in the FS using `cd::light::cone_attenuation` (already in the library). Push the spot's direction + inner/outer cosines via a new push-constant block. |
| 4 | "Sun ve spot light etkili gibi, gunesi silebilmek istiyorum"    | The Sun row is hard-coded at boot (`lights.push_back({"Sun (cool 6500K)", …})`) and never destructed.                            | All lights — including the sun — must be plain `Light` records in the same `lights[]` vector with the same Delete-key path. Default scene seeds the sun, but the user can erase it. |
| 5 | Lights aren't placeable like objects                            | Lights live in a separate vector + panel, not the ECS scene tree.                            | Promote each `Light` to an ECS entity. `cd::scene::Node` + `cd::light::LightComponent`. Then drag-drop / gizmo / Delete all work uniformly. |
| 6 | No drag-drop palette of objects / lights / sounds               | Single hard-coded entity seed list at boot.                                                  | Two-panel layout: left = scene tree (current `Scene` panel), right = **asset palette** (cube, sphere, primitive, point light, spot light, area light, audio source, scene preset). Drag from palette into the viewport spawns at the hit-test world position; drag onto scene tree appends as child. |
| 7 | ESC quits the app instead of cancelling the active selection / drag | The escape handler unconditionally calls `window.request_close()` when the palette isn't visible. | Priority chain: cancel-active-drag → close-palette → cancel-selection → quit. The Win32 quit confirmation moves to a `File → Quit` menu + an `Are you sure?` modal when the scene is dirty. |
| 8 | PBR shows on the 5×5 sphere grid only, not on user-spawnable assets | The PBR mesh / material are scoped to the sphere sweep loop.                                | Expose `PbrMaterial` as an entity component. Any spawned mesh that's tagged `is_pbr` picks it up. Add a "PBR sample scene" palette item that drops a Cornell-box-like scene preset (sphere + plane + 3-point lighting) ready for inspection. |
| 9 | No asset-loading path (user wants sample scenes / PBR objects)  | Today the only asset path is procedural (`cd::asset::make_*`). glTF importer exists (`cd::asset_gltf`) but is uncalled in this sample. | Wire `cd::asset_gltf::GltfLoader` behind a `Load…` palette entry. Bundle a `assets/samples/` directory in the repo with a few PBR-ready glTF scenes (DamagedHelmet, FlightHelmet — Khronos sample set, MIT-licensed) for the default loader. |
| 10 | No way to place audio sources in the world                     | Audio is a global mixer chain, not a per-entity component.                                  | Add `cd::audio::SourceComponent` on an entity. Position derives from the entity's transform via `cd::audio::positional` (already exists in `cd::audio::positional`). Spawning is the same drag-drop pipeline as for visual objects. |

## 2. Design principles for the production editor

Synthesizing the pain points above into rules that should govern the
production editor library:

### P1 — One entity model
Lights, audio sources, decals, particle emitters, and meshes are all
ECS entities with components. The editor panel binds to *components*,
not to type-specific vectors. Delete-key, undo/redo, gizmo, drag-drop,
focus-camera-on-selection all become uniform across types.

### P2 — Two-panel layout
Left = **Scene tree** (hierarchical view of every entity). Right =
**Asset palette** (drag source for new entities, including scene
presets). Center = viewport with the gizmo + selection outline.
Bottom = inspector for the selected entity (panel-per-component).

### P3 — Escape cancels active intent, never quits
The ESC chain in priority order:
1. Active drag → cancel + revert transform to drag-anchor value.
2. Active modal palette → close.
3. Multi-selection → narrow to single.
4. Single selection → clear.
5. Confirmation prompt → "Exit unsaved scene? Yes / No / Save first".

### P4 — Reversible everything
Every action goes through `cd::editor::EditHistory` (already exists).
Add `cd::editor::SpawnCommand` / `DespawnCommand` / `ComponentEdit
Command` so drag-drop spawn and Delete are undoable too.

### P5 — Numeric inputs are world-space
Sliders and drag widgets edit world-space quantities (azimuth in
degrees, range in metres, intensity in lumens / lux). Internal
storage stays in radians + linear units; the inspector does the
conversion. Tooltips show the unit.

### P6 — Asset palette is content-pack-driven
Palette entries come from a `cd::editor::PaletteRegistry` populated
by content packs. A pack is a directory with a `palette.json`
manifest listing entries (primitive type, glTF path, scene preset
JSON, audio clip, light template). Hot-reloadable.

### P7 — Live preview for asset drag
While the user drags from palette over the viewport, raycast against
the current scene to find the drop position; render a wireframe
ghost at that point so the drop is predictable.

### P8 — Save / load via existing JSON pipeline
`cd::asset_json` already round-trips entity lists. Extend with the
asset-pack manifest format from P6 so a saved scene self-describes
which assets it expects to resolve at load time.

## 3. Concrete shopping list for the production editor library

Files / classes to add when this becomes its own library
(`engine/editor_ui/`):

| Module                                | Responsibility                                                            |
|---------------------------------------|---------------------------------------------------------------------------|
| `cd::editor_ui::ScenePanel`           | Hierarchical scene tree, ESC / Delete handling, multi-select.             |
| `cd::editor_ui::AssetPalette`         | Right-side dock with drag-source items grouped by category.               |
| `cd::editor_ui::Inspector`            | Component-aware property editor. Picks panel-per-component via type-erase. |
| `cd::editor_ui::DragSpawn`            | Mouse-tracking helper that turns a palette drop into a `SpawnCommand`.   |
| `cd::editor_ui::ViewportGizmo`        | Production replacement for the in-sample axis gizmo. Plane-anchor drag math (fixes pain #1). |
| `cd::editor::SpawnCommand`            | EditHistory command for entity creation (undoable Spawn).                |
| `cd::editor::DespawnCommand`          | EditHistory command for entity deletion (undoable Despawn).              |
| `cd::editor::ComponentEditCommand<T>` | Generic templated component-value-change command.                        |
| `cd::editor::PaletteRegistry`         | Content-pack palette manifest loader.                                    |
| `cd::audio::SourceComponent`          | ECS component wrapping a positional audio clip.                          |
| `cd::light::LightComponent`           | ECS component wrapping a `cd::light::Light`.                             |

## 4. What the hello_engine sample taught us about the runtime

Insights from building the marathon prototype that should inform
the rest of the engine, not just the editor:

1. **The push-constant budget is tight.** The prim shader push struct
   grew to 208 bytes once we added per-fragment lighting. Vulkan
   guarantees 128 bytes; 256 bytes is desktop-typical but not
   guaranteed. Production should split into a per-frame UBO (lights
   array) + per-draw push (mvp + model + tint flags).

2. **MaterialDesc lacks alpha-blend state.** Faz 1.5 wanted soft
   shadows via alpha, ended up shipping hard shadows because
   `MaterialDesc::raster` has no blend fields. Add
   `BlendAttachmentState` (already in `Descriptors.hpp`) to the
   material desc + plumb to the pipeline builder.

3. **glslang requires `#version 460` for ray queries.** A `#version
   450` shader with `#extension GL_EXT_ray_query : require` silently
   drops `rayQueryEXT` as undeclared. Future shaders that touch any
   ray-query path must bump to 460. Document this in the shader
   authoring guide.

4. **TLAS recreate per frame is fine for 30-instance scenes.** No
   need to invest in "update mode" plumbing right now; defer
   per-frame TLAS optimization until profiler shows it.

5. **`#extension` lines must come right after `#version`.** Putting
   them after other layout decls fails silently in some glslang
   builds. Lint rule for shaders: enforce extension-first ordering.

## 5. Open marathon items waiting on production editor work

The remaining marathon items naturally bleed into editor work and
should be coordinated:

- **Faz 2 J — OIDN denoiser** for the path tracer (`hello_path_
  trace`) doesn't need the editor, but the result viewer probably
  does. Wire it as a `cd::editor_ui::OutputPanel` so the editor can
  preview a denoised PT frame inline.
- **Faz 3 K/L/M — ReSTIR DI/GI + NRC** are pure renderer work; they
  surface in the editor only through a quality slider in the
  inspector's render settings panel.

## 6. Sign-off

Carry this doc forward when `engine/editor_ui/` becomes its own
library. Update it from the sample's actual bug surface — the
marathon prototype is the best free user-research source we have.
