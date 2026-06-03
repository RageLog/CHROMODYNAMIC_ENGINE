# cd::render_scene_ingest

**Purpose**: Layer 2 of the generic glTF load pipeline (ADR-20260530). Takes a `cd::asset::gltf::LoadedScene` (CPU-only intermediate from `cd::asset_gltf::load_scene`) and lands it on the GPU + into the ECS / Scene tree with one call. No per-asset enums, no hardcoded scale/rotation, no manual material binding.

**Namespace**: `cd::render::scene`.

**Headers**: `cd/render/scene/SceneIngest.hpp`, `cd/render/scene/RenderBucket.hpp`.

**Primary types**:
- `cd::render::scene::IngestOptions` -- caller-tunable knobs (override world transform, skip ECS create, skip texture upload).
- `cd::render::scene::IngestResult` -- handles for every GPU resource + ECS entity the ingest allocated. The caller takes ownership; call `destroy_ingest_result` at shutdown.
- `cd::render::scene::ingest_gltf_scene(device, world, scene, src, opts={})` -> `Result<IngestResult>` -- the one-shot ingestion call.
- `cd::render::scene::destroy_ingest_result(device, r)` -- releases every GPU handle owned by the IngestResult. ECS / Scene cleanup is the caller's responsibility (use `Scene::destroy_node(result.root_entity)`).

**Pipeline (single depth-first walk)**:
1. Each `LoadedTexture` -> `cd::render::GpuTexture2D` via `cd::render::upload_texture_2d_rgba8`.
2. Each `LoadedPrimitive.mesh` -> `cd::render::GpuMesh` via `cd::render::upload_mesh`.
3. Each `LoadedNode` -> `cd::ecs::Entity` + `cd::scene::LocalTransform` (TRS decomposed from the source `Mat4f` via column-length scale + Shoemake matrix-to-quat rotation).
4. Parent/child edges wired via `cd::scene::Scene::attach`. A synthetic wrapper root above all `LoadedScene::root_nodes` lets the caller manipulate the whole asset through one handle.

**Strong-exception guarantee**: any allocation failure during upload destroys every GPU resource allocated up to that point (`rollback_gpu`). The caller's `world` / `scene` / `device` are observed in the same state as before the call.

**Usage**:
```cpp
#include <cd/asset/gltf/SceneLoader.hpp>
#include <cd/render/scene/SceneIngest.hpp>

auto loaded = cd::asset::gltf::load_scene("assets/Sponza.gltf");
if (!loaded) return /* error */;

auto r = cd::render::scene::ingest_gltf_scene(device, world, scene, *loaded);
if (!r) return /* error */;

// later, at shutdown:
scene.destroy_node(r->root_entity);
cd::render::scene::destroy_ingest_result(device, *r);
```

## Two-pass alpha render order (T1.15)

`cd/render/scene/RenderBucket.hpp` exposes the engine-wide draw-order contract used by the framegraph to avoid the curtain bleed-through failure mode documented in `docs/AUDIT/learned-lessons-pbr-rt-and-curtain-alpha-2026-06-03.md`.

**Bucket contract** (framegraph issues three sequential passes in this order):

| Bucket          | Depth test | Depth write | Sort            | When                                  |
| --------------- | ---------- | ----------- | --------------- | ------------------------------------- |
| `kOpaque`       | on         | on          | front-to-back   | first — populates depth, early-Z      |
| `kAlphaMask`    | on         | on          | front-to-back   | second — discard on `alpha < cutoff`  |
| `kAlphaBlend`   | on         | **off**     | back-to-front   | last — over-blends against opaque set |

**Routing rule**: `cd::render::scene::bucket_for(const MaterialInstance&)` reads the glTF-mirrored `AlphaMode` via the T1.16 predicate accessors (`is_blend / is_mask / is_opaque`). A scalar overload accepts `cd::material::AlphaMode` directly for code paths that already pulled it out of the material. Default-constructed `MaterialInstance` is `kOpaque` (matches glTF 2.0 default alphaMode).

**Partition helper**: `partition_prims(prims, materials)` returns a `BucketedPrims` whose `opaque_prims() / alpha_mask_prims() / alpha_blend_prims()` spans the framegraph consumes one pass at a time. Input order is preserved within each bucket so a secondary front-to-back / back-to-front sort can be applied without losing primitive identity.

**Moment**: Sponza curtains stop bleeding through opaque vegetation behind them — alpha-blend prims are drawn after their occluders rather than before, so the depth test rejects them where the curtain is itself occluded.

**Test command**: `ctest --preset ninja-debug -R cd_test_scene_ingest --output-on-failure`.

**Notes**:
- Sits between `cd::asset_gltf` (LoadedScene producer) and `cd::render` (mesh / texture upload helpers); depends on `cd::ecs` + `cd::scene` for entity create.
- Texture upload uses one staging buffer per texture (released before returning from `upload_texture_2d_rgba8`). For asset packs with hundreds of textures the caller should profile and consider a future batched-staging variant.
- `decompose_mat4` does NOT preserve shear -- pragmatic for v1 (every glTF a reasonable editor accepts has shear-free node hierarchy). A future Phase 2 may swap LocalTransform to hold the raw Mat4f directly.
- `IngestOptions::create_ecs_nodes = false` skips entity creation entirely; useful for callers that drive their own scene tree.
- Default `IngestOptions::use_suggested_xform = true` applies the asset's `LoadedScene::suggested_world_xform` -- a deterministic axis-convert × `scaling(5.0 / max_bbox_edge)` so any asset lands roughly in the [-5..+5] m frame-selected range no matter what units the exporter wrote.
