// =============================================================================
// CHROMODYNAMIC — samples/hello_stress
//
// v2.0.7 — 24-hour stress-harness skeleton.
//
// Runs a tight loop that:
//   1) Allocates/frees scratch buffers of varying sizes (mem stress).
//   2) Spawns + despawns ECS-style entities through cd::ecs::World.
//   3) Generates uniform random draws via cd::math::Random.
//   4) Periodically prints heartbeat lines so an external watchdog can
//      confirm no hang.
//
// Designed to be invoked with --duration <seconds> by CI. Default
// duration is 60 s for local smoke; the full 24h run is gated by
// dedicated CI hardware (see docs/PRODUCTION_V20_PLAN.md v2.0.7).
//
// No graphics device is created — this is a CPU-only / engine-core
// stress harness. The render-loop stress harness lands with v2.0.7
// when the dedicated machine is available.
// =============================================================================
#include <cd/core/Version.hpp>
#include <cd/ecs/World.hpp>
#include <cd/math/Random.hpp>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace
{

constexpr std::uint64_t kDefaultDurationSec = 60;

[[nodiscard]] std::uint64_t parse_duration(int argc, char** argv) noexcept
{
    for (int i = 1; i + 1 < argc; ++i)
    {
        const std::string flag { argv[i] };
        if (flag == "--duration")
        {
            try
            {
                return static_cast<std::uint64_t>(std::stoull(argv[i + 1]));
            }
            catch (...)  // NOLINT(bugprone-empty-catch) — silent fall-through
            {
                return kDefaultDurationSec;
            }
        }
    }
    return kDefaultDurationSec;
}

}  // namespace

int main(int argc, char** argv)
{
    const std::uint64_t duration_sec = parse_duration(argc, argv);
    std::printf("[stress] CHROMODYNAMIC %u.%u.%u — %llu s run\n",
                cd::core::kEngineVersion.major,
                cd::core::kEngineVersion.minor,
                cd::core::kEngineVersion.patch,
                static_cast<unsigned long long>(duration_sec));

    cd::math::Random rng { 0xC0FFEEULL };

    const auto start = std::chrono::steady_clock::now();
    const auto end   = start + std::chrono::seconds(duration_sec);

    std::uint64_t iter = 0;
    std::uint64_t alloc_bytes_total = 0;
    auto next_heartbeat = start + std::chrono::seconds(5);

    while (std::chrono::steady_clock::now() < end)
    {
        ++iter;

        // 1) Scratch allocation — variable-size churn so the allocator
        //    sees fragmentation pressure.
        const std::size_t n = 256U + static_cast<std::size_t>(
            rng.next_float() * 4096.0F);
        std::vector<std::uint32_t> scratch(n, 0xDEADBEEFU);
        alloc_bytes_total += scratch.size() * sizeof(std::uint32_t);

        // 2) ECS create — keep the World churn under control by
        //    creating a small batch each iter and letting RAII drop them.
        cd::ecs::World w;
        for (int i = 0; i < 32; ++i)
        {
            (void)w.create();
        }

        // 3) Random consumer — uniform draws so the PRNG is exercised.
        volatile float drain = 0.0F;
        for (int i = 0; i < 64; ++i)
        {
            drain += rng.next_float();
        }
        (void)drain;

        // Heartbeat every 5 s so an external watchdog can detect hang.
        const auto now = std::chrono::steady_clock::now();
        if (now >= next_heartbeat)
        {
            const auto elapsed_sec =
                std::chrono::duration_cast<std::chrono::seconds>(now - start).count();
            std::printf("[stress] t=%llds iter=%llu alloc_mb=%.1f\n",
                        static_cast<long long>(elapsed_sec),
                        static_cast<unsigned long long>(iter),
                        static_cast<double>(alloc_bytes_total) / 1048576.0);
            std::fflush(stdout);
            next_heartbeat = now + std::chrono::seconds(5);
        }
    }

    std::printf("[stress] DONE — iters=%llu total_alloc_mb=%.1f\n",
                static_cast<unsigned long long>(iter),
                static_cast<double>(alloc_bytes_total) / 1048576.0);
    return 0;
}
