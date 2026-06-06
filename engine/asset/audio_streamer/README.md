# cd::asset::audio_streamer

Audio clip streaming coordinator. Member of the asset-streaming
family alongside `cd::asset::scene_streamer` (Phase 586) and
`cd::asset::texture_streamer` (Phase 599 + 714).

## Sprints

| Sprint | Phase | Surface |
|--------|-------|---------|
| 1 | `phase619` | `AudioStreamer` — synchronous request → decode → upload flow. |
| 2 | `phase756` | `AsyncAudioPool` — opt-in async worker-pool path. |

Pattern matches the scene/texture streamers — sprint-1 sync API +
sprint-2 async pool added without breaking existing call sites.

## Public surface

```cpp
namespace cd::asset::audio_streamer {

struct StreamRequest
{
    cd::asset::AssetId          id;
    std::filesystem::path       source;        // .wav / .ogg
    int32_t                     priority;
};

struct StreamedClip
{
    uint32_t                    sample_rate;
    uint8_t                     channel_count;
    std::vector<int16_t>        samples_interleaved;
};

class AudioStreamer                          // sprint 1
{
public:
    void                                          enqueue(StreamRequest);
    void                                          tick(/* up to N per call */);
    [[nodiscard]] std::optional<StreamedClip>     poll_complete(cd::asset::AssetId);
};

class AsyncAudioPool                         // sprint 2
{
public:
    AsyncAudioPool(uint32_t worker_count);
    void                                          enqueue(StreamRequest);
    void                                          stop();
    [[nodiscard]] std::optional<StreamedClip>     poll_complete(cd::asset::AssetId);
};

}
```

## Format & PCM layout

The streamer normalises every clip to **interleaved 16-bit PCM**:

```
mono:    [s0 s1 s2 s3 ...]                          1 sample per frame
stereo:  [L0 R0 L1 R1 L2 R2 ...]                    2 samples per frame
5.1:     [FL FR FC LFE BL BR | FL FR FC LFE BL BR | ...]
```

The decoder layer (`cd::asset_wav` / `cd::asset_ogg`) handles the raw
format; the streamer interface presents a single uniform PCM type so
downstream audio (`cd::audio`) does not need to switch on format at
mix time.

## Sync vs async determinism

The trade-off mirrors `cd::asset::texture_streamer`:

* `AudioStreamer` (sync) — `tick()` is the **only** call where a
  decode completes. Suitable for the editor / deterministic golden
  runs.
* `AsyncAudioPool` (async) — workers decode in parallel, the main
  thread polls completion. Suitable for runtime games where decode
  latency must not steal frame budget.

## Dependencies

* `cd::core` — Defines, expected, error infra.
* `cd::asset` — `AssetId`, `IAssetLoader` vocabulary.

Does not link to `cd::audio` — the streamer produces PCM blobs; the
audio mixer is wired by the caller.
