// =============================================================================
// CHROMODYNAMIC — tests/stress/main.cpp (cd_stress)
//
// Multi-subsystem stability & memory-leak soak harness (FAZ 8.2 & FAZ 10.3):
//   1) Variable-size scratch buffer churn
//   2) ECS World & Stage Element/Vessel/Pilot spawn/despawn churn
//   3) Deterministic EngineLoop fixed + variable step soak
//   4) Optional --frames <N> (fast CI/test verification) or --duration <sec>
// =============================================================================
#include <cd/core/Version.hpp>
#include <cd/ecs/World.hpp>
#include <cd/gameplay_framework/VesselPilotDirector.hpp>
#include <cd/math/Random.hpp>
#include <cd/runtime/EngineLoop.hpp>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace
{

constexpr std::uint64_t kDefaultDurationSec = 2;

struct StressArgs
{
    std::uint64_t duration_sec { kDefaultDurationSec };
    std::uint64_t max_frames { 0 };
};

[[nodiscard]] StressArgs parse_args(int argc, char** argv) noexcept
{
    StressArgs out {};
    for (int i = 1; i + 1 < argc; ++i)
    {
        const std::string flag { argv[i] };
        if (flag == "--duration")
        {
            try
            {
                out.duration_sec = static_cast<std::uint64_t>(std::stoull(argv[i + 1]));
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
                out.duration_sec = kDefaultDurationSec;
            }
        }
        else if (flag == "--frames")
        {
            try
            {
                out.max_frames = static_cast<std::uint64_t>(std::stoull(argv[i + 1]));
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
                out.max_frames = 60;
            }
        }
    }
    return out;
}

}  // namespace

int main(int argc, char** argv)
{
    const StressArgs args = parse_args(argc, argv);
    std::printf("[cd_stress] CHROMODYNAMIC %u.%u.%u — duration=%llu s max_frames=%llu\n",
                cd::core::kEngineVersion.major,
                cd::core::kEngineVersion.minor,
                cd::core::kEngineVersion.patch,
                static_cast<unsigned long long>(args.duration_sec),
                static_cast<unsigned long long>(args.max_frames));

    cd::math::Random rng { 0xC0FFEEULL };
    cd::runtime::EngineContext ctx;
    cd::runtime::EngineLoop loop(ctx);

    const auto start = std::chrono::steady_clock::now();
    const auto end   = start + std::chrono::seconds(args.duration_sec);

    std::uint64_t iter = 0;
    std::uint64_t alloc_bytes_total = 0;

    while (true)
    {
        if (args.max_frames > 0 && iter >= args.max_frames)
        {
            break;
        }
        if (args.max_frames == 0 && std::chrono::steady_clock::now() >= end)
        {
            break;
        }
        ++iter;

        // 1) Variable-size scratch allocation churn
        const std::size_t n = 256U + static_cast<std::size_t>(rng.next_float() * 2048.0F);
        std::vector<std::uint32_t> scratch(n, 0xDEADBEEFU);
        alloc_bytes_total += scratch.size() * sizeof(std::uint32_t);

        // 2) ECS create/drop churn
        cd::ecs::World w;
        for (int i = 0; i < 16; ++i)
        {
            (void)w.create();
        }

        // 3) Stage + Avatar + SynthPilot + EngineLoop step soak
        if ((iter % 8U) == 0U)
        {
            auto& stage = loop.stage();
            auto& avatar = stage.root().emplace_child<cd::gameplay_vessel::Avatar>("SoakAvatar");
            auto& pilot  = stage.root().emplace_child<cd::gameplay_pilot::SynthPilot>("SoakPilot");
            avatar.embark(&pilot);
            (void)loop.step_frame(1.0F / 60.0F);
            (void)stage.root().detach_child(&pilot);
            (void)stage.root().detach_child(&avatar);
        }
        else
        {
            (void)loop.step_frame(1.0F / 60.0F);
        }
    }

    std::printf("[cd_stress] DONE — iters=%llu frames=%llu total_alloc_mb=%.2f\n",
                static_cast<unsigned long long>(iter),
                static_cast<unsigned long long>(loop.total_frames()),
                static_cast<double>(alloc_bytes_total) / 1048576.0);
    return 0;
}
