# cd::asset::streamer_pool

Unified coordinator for the four asset streamers. A single
`tick()` round-robins through every domain (scene / texture / audio /
shader-cache stats) with **priority-weighted dispatch** so the
caller's per-frame streaming budget is divided proportionally to
each streamer's outstanding-queue urgency.

## Sprints

| Sprint | Phase | Surface |
|--------|-------|---------|
| 1 | `phase705` | Priority-weighted round-robin in a single `tick()`. |
| 2 | queued     | Async thread-pool with inflight-job cap. |

## Public surface

```cpp
namespace cd::asset::streamer_pool {

struct PoolConfig
{
    uint32_t   scene_quota_per_tick     { 1 };
    uint32_t   texture_quota_per_tick   { 3 };
    uint32_t   audio_quota_per_tick     { 2 };
    bool       collect_shader_stats     { true };
};

class StreamerPool
{
public:
    StreamerPool(cd::asset::scene_streamer::SceneStreamer&,
                 cd::asset::texture_streamer::TextureStreamer&,
                 cd::asset::audio_streamer::AudioStreamer&,
                 const cd::asset::shader_cache::ShaderCache&,
                 PoolConfig);

    // Round-robin tick across every streamer.
    void                          tick();

    // Snapshot of every streamer's pending-queue depth.
    struct PendingCounts
    {
        uint32_t scene, texture, audio;
        std::size_t shader_cache_entries;
    };
    [[nodiscard]] PendingCounts   pending() const;
};

}
```

## Round-robin dispatch

Each `tick()` walks the streamers in fixed order and dispatches up to
the per-streamer quota:

```
  for streamer in [scene, texture, audio]:
      streamer.tick(up_to=streamer.quota_per_tick)
  if config.collect_shader_stats:
      pending_counts.shader_cache_entries = shader_cache.entry_count()
```

The shader cache is **read-only** here — the pool only reports its
entry count for the editor's streaming overlay. The cache itself
populates from the compile path elsewhere.

## Quota authoring

Default quotas (`1 / 3 / 2`) favour textures because typical 60-fps
games stream a steady ~3 textures/frame during world traversal, with
occasional cell-boundary scene loads (≤ 1 / frame) and constant
ambient audio updates (≤ 2 / frame). Override `PoolConfig` per game
profile (open-world chases higher texture quota; arena shooter lower).

## Dependencies

* `cd::core` — Defines.
* `cd::asset` — `AssetId` vocabulary.
* `cd::asset_scene_streamer` (Phase 586)
* `cd::asset_texture_streamer` (Phase 599) — also brings `cd::rhi`
  PUBLIC for `IDevice`.
* `cd::asset_audio_streamer` (Phase 619)
* `cd::asset_shader_cache` (Phase 609) — read-only.

## See also

* `cd::asset::scene_streamer`
* `cd::asset::texture_streamer`
* `cd::asset::audio_streamer`
* `cd::asset::shader_cache`
