// =============================================================================
// CHROMODYNAMIC — samples/hello_events
//
// Showcase sample for the Phase 6 Sprint 3 stack:
//
//   cd::events::EventBus  — typed pub/sub, deferred queue_publish + drain
//   cd::log::JsonLogger   — JSONL sink for log shippers
//   cd::time::RateLimiter — frame cap (hybrid sleep + spin)
//   cd::time::IntervalTicker — periodic boolean fire
//
// Loop:
//   * cap the tick rate to 200 Hz via RateLimiter
//   * every 200 ms (IntervalTicker), queue_publish a ChimeEvent
//   * drain the bus on the same thread → handler logs via JsonLogger
//   * runs for 1 s then prints the cumulative stats
// =============================================================================
#include <cd/events/EventBus.hpp>
#include <cd/log/JsonLogger.hpp>
#include <cd/time/RateLimiter.hpp>

#include <chrono>
#include <cstdio>
#include <source_location>

namespace
{

struct ChimeEvent
{
    int seq { 0 };
};

}  // namespace

int main()
{
    std::printf("=== hello_events — Phase 6 Sprint 3 stack demo ===\n");

    cd::events::EventBus bus;
    cd::log::JsonLogger logger { stdout, cd::log::LogLevel::Info };
    cd::time::RateLimiter limiter { 200 };
    cd::time::IntervalTicker chime { std::chrono::milliseconds { 200 } };

    int chime_count = 0;
    auto sub = bus.subscribe<ChimeEvent>([&](const ChimeEvent& e) {
        ++chime_count;
        logger.info(std::source_location::current(),
                    "chime seq={}", e.seq);
    });

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds { 1 };
    int chimes_queued = 0;
    int frames = 0;
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (chime.tick())
        {
            bus.queue_publish<ChimeEvent>(ChimeEvent { ++chimes_queued });
        }
        const auto drained = bus.drain();
        (void)drained;
        ++frames;
        limiter.await_next_frame();
    }

    std::printf("\n=== Summary ===\n");
    std::printf("  frames           = %d\n", frames);
    std::printf("  chimes_queued    = %d\n", chimes_queued);
    std::printf("  chimes_delivered = %d\n", chime_count);
    std::printf("  ticker_fires     = %llu\n",
                static_cast<unsigned long long>(chime.fire_count()));
    std::printf("[hello_events] done\n");
    return 0;
}
