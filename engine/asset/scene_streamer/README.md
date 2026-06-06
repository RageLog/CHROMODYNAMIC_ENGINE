# cd::asset::scene_streamer

Scene-graph streaming coordinator. Wraps `cd::asset::gltf::load_scene`
in a priority queue + (optional) worker pool so glTF loading can run
off the frame critical path. Sibling to `cd::asset::texture_streamer`.

## Sprints

| Sprint | Phase | Surface |
|--------|-------|---------|
| 1 | `phase586` | `SceneStreamer` — synchronous request → glTF load → `LoadedScene` flow. |
| 2 | `phase755` | `AsyncScenePool` — opt-in async path with a worker pool. |

The async path is **additive**. hello_engine still uses the sync
coordinator for boot-time `Sponza.gltf` + `CesiumMan.glb` because
the load happens once and the editor benefits from `boot ⇒ frame 0
ready`. Production streaming worlds chunk glTF scenes per cell and
push the async pool.

## Public surface

```cpp
namespace cd::asset::scene_streamer {

struct StreamRequest
{
    cd::asset::AssetId             id;
    std::filesystem::path          source;        // .gltf / .glb
    int32_t                        priority;      // higher = sooner
};

class SceneStreamer
{
public:
    void                                       enqueue(StreamRequest);
    void                                       tick(/* up to N per call */);
    [[nodiscard]] std::optional<cd::asset::gltf::LoadedScene>
                                               poll_complete(cd::asset::AssetId);
};

class AsyncScenePool                        // sprint 2
{
public:
    AsyncScenePool(uint32_t worker_count);
    void                                       enqueue(StreamRequest);
    void                                       stop();
    [[nodiscard]] std::optional<cd::asset::gltf::LoadedScene>
                                               poll_complete(cd::asset::AssetId);
};

}
```

## Lifecycle

```
                Sync (sprint 1)               Async (sprint 2)
  enqueue()    ⇒ push to priority queue       ⇒ push to worker queue
  tick()       ⇒ pop top-N → load_scene()        (no-op — workers run)
  poll_complete ⇒ return LoadedScene if ready  ⇒ return when worker done
```

`LoadedScene` is the same struct returned by `cd::asset::gltf::load_scene` —
nodes + meshes + textures + materials in glTF order. Caller is
responsible for the ECS ingest step (`cd::render::scene_ingest`).

## ECS ingest

`scene_streamer` produces `LoadedScene` but **does not** push it into
the ECS. The handoff lives at the game / hello_engine layer because
the same glTF can land in different ECS configurations (e.g. Sponza
as a single static-mesh entity vs. one-entity-per-prim).
`cd::render::scene_ingest` is the canonical adapter.

## Dependencies

* `cd::core` — Defines, expected, error infra.
* `cd::asset` — `AssetId`, `IAssetLoader` vocabulary.
* `cd::asset_gltf` — `load_scene()` + `LoadedScene`.
* `cd::scene` — for future ECS ingest hooks (today only the type
  vocabulary is consumed; no scene mutation happens here).
