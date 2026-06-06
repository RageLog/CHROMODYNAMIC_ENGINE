# cd::asset::texture_streamer

GPU-side texture streaming coordinator. Sibling library to
`cd::asset::scene_streamer` — the texture flavour of asset streaming.

## Sprints

| Sprint | Phase | Surface |
|--------|-------|---------|
| 1 | `phase599` | `TextureStreamer` — synchronous request → upload → handle flow. |
| 2 | `phase714` | `AsyncTexturePool` — opt-in async path with a worker pool. |

The async path is **additive**, not a replacement: hello_engine still
uses the sync coordinator for its boot-time procedural textures
(small, predictable, no streaming benefit). Production streaming
loaders pull the async pool.

## Public surface

```cpp
namespace cd::asset::texture_streamer {

struct StreamRequest
{
    cd::asset::AssetId          id;
    std::filesystem::path       source;        // .png / .ktx2 / .cdtex
    int32_t                     priority;      // higher = sooner
    bool                        srgb;
    bool                        generate_mips;
};

class TextureStreamer
{
public:
    explicit TextureStreamer(cd::rhi::IDevice& device);

    void                            enqueue(StreamRequest);
    void                            tick(/* up to N items per call */);

    [[nodiscard]] std::optional<cd::rhi::TextureHandle>
                                    poll_complete(cd::asset::AssetId);
};

// Sprint-2 async path
class AsyncTexturePool
{
public:
    AsyncTexturePool(cd::rhi::IDevice&, std::uint32_t worker_count);
    void                            enqueue(StreamRequest);
    void                            stop();
    [[nodiscard]] std::optional<cd::rhi::TextureHandle>
                                    poll_complete(cd::asset::AssetId);
};

}
```

## Lifecycle

```
                Sync (sprint 1)           Async (sprint 2)
  enqueue()    ⇒ push to priority queue   ⇒ push to worker queue
  tick()       ⇒ pop top-N → upload now      (no-op — workers run already)
  poll_complete ⇒ return handle if ready    ⇒ return handle if worker done
```

The sync path **guarantees** `tick()` is the only frame where an
upload happens. Suitable for the editor's boot path and any
deterministic golden-frame mode (`--golden-fixture` etc.).

The async path **decouples** upload latency from frame work but
sacrifices per-frame determinism — runtime games on PC / console use
this; deterministic captures use the sync path.

## Dependencies

* `cd::core` — Defines, expected, error infra.
* `cd::asset` — `AssetId`, `IAssetLoader` vocabulary.
* `cd::rhi` — `IDevice` + `TextureHandle` (no upload path can avoid
  the rhi).
