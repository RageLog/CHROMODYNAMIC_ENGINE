// =============================================================================
// CHROMODYNAMIC — samples/asset/hello_texture_streamer
// Phase 614 — console proof that cd::asset::texture_streamer is consumable.
//
// Enqueues 3 texture paths (fake), ticks 5x with a NullDevice, and prints
// pending/completed counts each tick.
//
// NullDevice is used so the sample runs fully headless (no Vulkan ICD required).
// Each tick() processes the highest-priority pending request; after 3 ticks all
// requests are consumed.  Ticks 4 and 5 are no-ops (empty pending queue).
//
// Always exits 0.
// =============================================================================
#include <cd/asset/texture_streamer/TextureStreamer.hpp>

#include <cd/rhi/NullDevice.hpp>

#include <cstdio>

int main()
{
    std::printf("=== hello_texture_streamer — cd::asset::texture_streamer demo ===\n");

    // NullDevice: headless GPU backend — no Vulkan ICD required.
    cd::rhi::NullDevice device;

    cd::asset::texture_streamer::TextureStreamer streamer;

    // Enqueue 3 fake texture paths at different priorities.
    streamer.enqueue({ "fake/albedo.cdtex",   0U, 200U });
    streamer.enqueue({ "fake/normal.cdtex",   0U, 150U });
    streamer.enqueue({ "fake/roughness.cdtex", 0U,  80U });

    std::printf(
        "after enqueue: pending=%zu  completed=%zu\n",
        streamer.pending_count(),
        streamer.completed_count()
    );

    // Tick 5x — Sprint-1 processes at most one request per tick.
    // Ticks 1-3 drain the queue; ticks 4-5 are no-ops.
    for (int i = 0; i < 5; ++i)
    {
        constexpr float kDt = 0.016F;
        streamer.tick(kDt, device);

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

    if (streamer.pending_count() != 0U)
    {
        std::printf("[FAIL] pending_count must be 0 after 5 ticks for 3 requests\n");
        return 1;
    }

    if (streamer.completed_count() != 3U)
    {
        std::printf("[FAIL] completed_count must be 3 (NullDevice always succeeds)\n");
        return 1;
    }

    std::printf("[hello_texture_streamer] PASS\n");
    return 0;
}
