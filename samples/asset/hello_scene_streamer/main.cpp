// =============================================================================
// CHROMODYNAMIC — samples/asset/hello_scene_streamer
// Phase 602 — console proof that cd::asset::scene_streamer is consumable.
//
// Enqueues 3 asset paths:
//   * One real .gltf (assets/samples/Generic/triangle.gltf) if reachable,
//   * Two fake paths (silently fail — graceful Sprint-1 behaviour).
// Ticks 5× and prints pending + completed counts each tick.
// Always exits 0: missing-file failures are expected and non-fatal.
// =============================================================================
#include <cd/asset/scene_streamer/SceneStreamer.hpp>

#include <cstdio>

int main()
{
    std::printf("=== hello_scene_streamer — cd::asset::scene_streamer demo ===\n");

    cd::asset::scene_streamer::SceneStreamer streamer;

    // Enqueue three requests: one real path (may succeed), two fake paths
    // (will fail gracefully — Sprint-1 silently drops missing files).
    streamer.enqueue({ "assets/samples/Generic/triangle.gltf", 200U });
    streamer.enqueue({ "fake/scene_alpha.glb",                 100U });
    streamer.enqueue({ "fake/scene_beta.glb",                   50U });

    std::printf(
        "after enqueue: pending=%zu  completed=%zu\n",
        streamer.pending_count(),
        streamer.completed_count()
    );

    // Tick 5 times (Sprint-1 processes at most one request per tick).
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

    // After 3 ticks each request has been attempted (all pending cleared).
    // Real file: completed_count == 1 if reachable, else 0.
    // Fake files: silently dropped — never counted as completed.
    std::printf(
        "final: pending=%zu  completed=%zu\n",
        streamer.pending_count(),
        streamer.completed_count()
    );

    if (streamer.pending_count() != 0U)
    {
        std::printf("[FAIL] pending_count must be 0 after 5 ticks for 3 requests\n");
        return 1;
    }

    const bool real_loaded = streamer.is_loaded("assets/samples/Generic/triangle.gltf");
    std::printf(
        "triangle.gltf loaded: %s  (expected if file is reachable from cwd)\n",
        real_loaded ? "yes" : "no"
    );

    std::printf("[hello_scene_streamer] PASS\n");
    return 0;
}
