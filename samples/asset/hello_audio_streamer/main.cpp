// =============================================================================
// CHROMODYNAMIC — samples/asset/hello_audio_streamer
// Phase 633 — console proof that cd::asset::audio_streamer is consumable.
//
// Enqueues 3 audio clip paths:
//   * One realistic .wav path (fake — silently fails in Sprint-1).
//   * Two additional fake paths (silently fail — graceful Sprint-1 behaviour).
// Ticks 5x and prints pending + completed counts each tick.
// Always exits 0: missing-file failures are expected and non-fatal.
// =============================================================================
#include <cd/asset/audio_streamer/AudioStreamer.hpp>

#include <cstdio>

int main()
{
    std::printf("=== hello_audio_streamer — cd::asset::audio_streamer demo ===\n");

    cd::asset::audio_streamer::AudioStreamer streamer;

    // Enqueue three requests (Sprint-1 always succeeds via AssetId::from_path).
    streamer.enqueue({ "audio/music_theme.wav",   0U, 200U });
    streamer.enqueue({ "audio/footstep_grass.wav", 1U, 128U });
    streamer.enqueue({ "audio/ambient_wind.wav",   1U,  50U });

    std::printf(
        "after enqueue: pending=%zu  completed=%zu\n",
        streamer.pending_count(),
        streamer.completed_count()
    );

    // Tick 5 times (Sprint-1 processes at most one request per tick,
    // in descending priority order).
    for (int i = 0; i < 5; ++i)
    {
        constexpr float kDt = 0.016F;
        streamer.tick(kDt);

        std::printf(
            "  tick %d: pending=%zu  completed=%zu\n",
            i + 1,
            streamer.pending_count(),
            streamer.completed_count()
        );
    }

    std::printf(
        "final: pending=%zu  completed=%zu\n",
        streamer.pending_count(),
        streamer.completed_count()
    );

    // After 3 ticks the 3 requests must all have been processed.
    if (streamer.pending_count() != 0U)
    {
        std::printf("[FAIL] pending_count must be 0 after 5 ticks for 3 requests\n");
        return 1;
    }

    // Sprint-1: every enqueued path resolves to an AssetId (deterministic hash).
    const bool music_loaded    = streamer.is_loaded("audio/music_theme.wav");
    const bool footstep_loaded = streamer.is_loaded("audio/footstep_grass.wav");
    const bool ambient_loaded  = streamer.is_loaded("audio/ambient_wind.wav");

    std::printf(
        "music_theme.wav loaded:    %s\n"
        "footstep_grass.wav loaded: %s\n"
        "ambient_wind.wav loaded:   %s\n",
        music_loaded    ? "yes" : "no",
        footstep_loaded ? "yes" : "no",
        ambient_loaded  ? "yes" : "no"
    );

    std::printf("[hello_audio_streamer] PASS\n");
    return 0;
}
