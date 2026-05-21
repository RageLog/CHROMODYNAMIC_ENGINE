// =============================================================================
// CHROMODYNAMIC — samples/hello_foundation
// Sprint S2.1.f — exercises every foundation library in a single demo:
//   cd::core (defines, version, handle, source-location, result)
//   cd::mem  (linear arena, pool, tracking)
//   cd::concurrency (ring buffer SPSC, spin lock)
//   cd::time (steady clock, hi-res clock, sim clock, frame pacer)
//   cd::platform (signal handler scope)
//   cd::diag (crash reporter scope, non-fatal capture)
// =============================================================================
#include <cd/concurrency/RingBuffer.hpp>
#include <cd/concurrency/SpinLock.hpp>
#include <cd/core/Defines.hpp>
#include <cd/core/Handle.hpp>
#include <cd/core/HandleStore.hpp>
#include <cd/core/SourceLocation.hpp>
#include <cd/core/Version.hpp>
#include <cd/diag/CrashReporter.hpp>
#include <cd/mem/LinearAllocator.hpp>
#include <cd/mem/PoolAllocator.hpp>
#include <cd/mem/TrackingAllocator.hpp>
#include <cd/platform/SignalHandler.hpp>
#include <cd/time/FramePacer.hpp>
#include <cd/time/HiResClock.hpp>
#include <cd/time/SimClock.hpp>
#include <cd/time/SteadyClock.hpp>

#include <atomic>
#include <chrono>
#include <cinttypes>  // PRId64 / PRIu64
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>

namespace demo
{

struct AssetTag
{
};

using AssetHandle = cd::core::Handle<AssetTag>;

struct Asset
{
    std::string name;
    std::uint32_t bytes {};
};

std::atomic<int> g_diag_events { 0 };

// File-local diagnostic callback. Declared static so GCC's
// -Wmissing-declarations doesn't trip on the definition (and we don't
// pollute the linker namespace with another extern "C" symbol).
extern "C" void diag_reporter(const cd::diag::CrashContext& ctx) noexcept;

extern "C" void diag_reporter(const cd::diag::CrashContext& ctx) noexcept
{
    g_diag_events.fetch_add(1, std::memory_order_relaxed);
    (void)ctx;
}

}  // namespace demo

int main()
{
    std::printf("=== CHROMODYNAMIC hello_foundation ===\n");
    std::printf(
        "Engine: %.*s v%u.%u.%u\n",
        static_cast<int>(cd::core::kEngineName.size()),
        cd::core::kEngineName.data(),
        static_cast<unsigned>(cd::core::kEngineVersion.major),
        static_cast<unsigned>(cd::core::kEngineVersion.minor),
        static_cast<unsigned>(cd::core::kEngineVersion.patch)
    );
    std::printf("Built with: %s on %s (%s)\n", CD_COMPILER_NAME, CD_OS_NAME, CD_ARCH_NAME);
    std::printf("Cache-line: %llu bytes\n", static_cast<unsigned long long>(cd::core::kCacheLineSize));

    // cd::diag — install crash reporter early.
    cd::diag::CrashReporter crash;
    if (!crash.install(&demo::diag_reporter))
    {
        std::printf("WARN: could not install crash reporter\n");
    }
    else
    {
        std::printf("Crash reporter installed.\n");
    }

    // cd::core — handle store + asset roundtrip.
    cd::core::HandleStore<demo::Asset, demo::AssetTag> assets;
    assets.set_type_id(0x42);
    const auto a_r = assets.insert(demo::Asset { "shader.cd", 4096 });
    const auto b_r = assets.insert(demo::Asset { "mesh.cd", 1 << 20 });
    if (!a_r || !b_r)
    {
        std::printf("ERR: asset store failed\n");
        return 1;
    }
    std::printf(
        "Assets: %u live; sample handle idx=%u gen=%u type=%u\n",
        assets.size(),
        a_r->index(),
        a_r->generation(),
        a_r->type_id()
    );

    // cd::mem — arena + tracking decorator.
    cd::mem::LinearAllocator arena { 64 * 1024 };
    cd::mem::TrackingAllocator tracked { arena, "demo-arena" };
    void* p = tracked.allocate(4096);
    if (p == nullptr)
    {
        std::printf("ERR: arena alloc failed\n");
        return 2;
    }
    // %zu accepts size_t natively on every standard-conforming printf; this
    // avoids the (often useless on x64) static_cast<unsigned long long>().
    const auto tag = tracked.tag();
    std::printf(
        "Arena: %zu bytes used / %zu capacity (tag=%.*s)\n",
        arena.bytes_in_use(),
        arena.capacity(),
        static_cast<int>(tag.size()),
        tag.data()
    );

    // cd::concurrency — SPSC ring under a producer/consumer pair.
    cd::concurrency::RingBuffer<int, 64> ring;
    std::atomic<long long> sum { 0 };
    std::thread producer { [&]
                           {
                               for (int i = 0; i < 200; ++i)
                               {
                                   while (!ring.push(i))
                                       cd::concurrency::cpu_pause();
                               }
                           } };
    std::thread consumer { [&]
                           {
                               int seen = 0;
                               while (seen < 200)
                               {
                                   int v = 0;
                                   if (ring.pop(v))
                                   {
                                       sum.fetch_add(v, std::memory_order_relaxed);
                                       ++seen;
                                   }
                                   else
                                   {
                                       cd::concurrency::cpu_pause();
                                   }
                               }
                           } };
    producer.join();
    consumer.join();
    std::printf("Ring SPSC: drained, sum=%lld (expected 19900)\n", sum.load());

    // cd::time — steady, hi-res, sim clock + frame pacer.
    auto t0 = cd::time::SteadyClock::instance().now();
    std::uint64_t hr0 = cd::time::hires_now_ns();
    cd::time::SimClock sim { std::chrono::microseconds { 16'667 } };  // 60 Hz
    cd::time::FramePacer pacer {
        cd::time::FramePacerOptions { std::chrono::milliseconds { 16 }, 4 }
    };

    for (int frame = 0; frame < 3; ++frame)
    {
        sim.tick(std::chrono::milliseconds { 16 });
        auto pr = pacer.update(std::chrono::milliseconds { 16 });
        std::printf(
            "  frame %d sim_steps=%u alpha=%.3f sim_elapsed=%.3f s\n",
            frame,
            pr.sim_steps,
            pr.alpha,
            cd::time::to_seconds(sim.elapsed())
        );
    }

    auto t1 = cd::time::SteadyClock::instance().now();
    std::uint64_t hr1 = cd::time::hires_now_ns();
    // PRId64 / PRIu64 give the printf format spec matching int64_t / uint64_t
    // on every platform without an extra cast.
    const auto steady_dt = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    std::printf("Time: steady_dt=%" PRId64 " us, hires_dt=%" PRIu64 " ns\n", steady_dt, hr1 - hr0);

    // cd::diag — non-fatal capture path
    crash.capture_non_fatal("hello_foundation-smoke");
    std::printf("Diag events recorded: %d\n", demo::g_diag_events.load());

    // Source location helper. line() already returns std::uint_least32_t → %u.
    const auto loc = cd::core::here();
    std::printf("Built from: %s:%u\n", cd::core::file_name_only(loc).data(), loc.line());

    // Clean teardown (RAII)
    crash.uninstall();
    std::printf("[hello_foundation] OK\n");
    return 0;
}
