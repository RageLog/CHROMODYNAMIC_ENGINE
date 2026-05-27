// =============================================================================
// CHROMODYNAMIC — samples/hello_scheduler/main.cpp
//
// Headless ECS scheduler demo. Builds a 3-system pipeline (sense → move
// → audit) on 1000 entities, ticks 120 frames, and:
//   * verifies the scheduler runs systems in dependency order
//   * prints per-frame timing (overall + per-system via cd::profile)
//   * dumps the resolved order so the developer can sanity-check the DAG
//
// No GPU; this is a pure CPU sample that proves cd::ecs::Scheduler
// composes with cd::profile cleanly.
// =============================================================================
#include <cd/ecs/Scheduler.hpp>
#include <cd/ecs/World.hpp>
#include <cd/profile/BufferSink.hpp>
#include <cd/profile/Scope.hpp>
#include <cd/profile/StatsAggregator.hpp>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>

namespace
{

struct Pos
{
    float x { 0.0F };
    float y { 0.0F };
};

struct Vel
{
    float dx { 1.0F };
    float dy { 0.5F };
};

struct Health
{
    int hp { 100 };
};

}  // namespace

int main()
{
    constexpr std::size_t kEntities = 1000;
    constexpr std::uint32_t kFrames = 120;
    constexpr float kDt = 1.0F / 60.0F;

    cd::ecs::World world;
    for (std::size_t i = 0; i < kEntities; ++i)
    {
        auto e = world.create();
        world.emplace<Pos>(e, Pos { static_cast<float>(i), 0.0F });
        world.emplace<Vel>(e, Vel { 0.5F, 0.25F });
        world.emplace<Health>(e, Health { 100 });
    }

    cd::profile::BufferSink sink { 4096 };
    auto* prev = cd::profile::set_sink(&sink);

    cd::ecs::Scheduler sched;
    // sense: read Pos, write Health (clamp HP if entity wanders off-grid)
    sched.add(
        cd::ecs::SystemDesc { "sense" }.reads<Pos>().writes<Health>().fn(
            [](cd::ecs::World& w)
            {
                CD_PROFILE_SCOPE("sense");
                w.each<Pos, Health>(
                    [](cd::ecs::Entity, Pos& p, Health& h)
                    {
                        if (p.x > 1000.0F || p.x < -1000.0F)
                            h.hp = 0;
                    }
                );
            }
        )
    );

    // move: read Vel, write Pos (Pos integration)
    sched.add(
        cd::ecs::SystemDesc { "move" }.reads<Vel>().writes<Pos>().fn(
            [](cd::ecs::World& w)
            {
                CD_PROFILE_SCOPE("move");
                w.each<Pos, Vel>(
                    [](cd::ecs::Entity, Pos& p, Vel& v)
                    {
                        p.x += v.dx * kDt;
                        p.y += v.dy * kDt;
                    }
                );
            }
        )
    );

    // audit: read everything, write nothing (debug counter)
    sched.add(
        cd::ecs::SystemDesc { "audit" }.reads<Pos>().reads<Vel>().reads<Health>().fn(
            [](cd::ecs::World& w)
            {
                CD_PROFILE_SCOPE("audit");
                std::uint32_t alive = 0;
                w.for_each<Health>(
                    [&](cd::ecs::Entity, Health& h)
                    {
                        if (h.hp > 0)
                            ++alive;
                    }
                );
                (void)alive;
            }
        )
    );

    // Print resolved order BEFORE ticking so the user sees it once.
    auto order = sched.preview_order();
    if (order.has_value())
    {
        std::printf("hello_scheduler: resolved order: ");
        for (const auto& n : *order)
            std::printf("%s ", n.c_str());
        std::printf("\n");
    }

    const auto t0 = std::chrono::steady_clock::now();
    for (std::uint32_t f = 0; f < kFrames; ++f)
    {
        CD_PROFILE_SCOPE("tick");
        if (!sched.tick(world).has_value())
        {
            std::fprintf(stderr, "scheduler: cycle detected\n");
            cd::profile::set_sink(prev);
            return 2;
        }
    }
    const auto t1 = std::chrono::steady_clock::now();
    const auto ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    std::printf(
        "hello_scheduler: %u frames * %zu entities * 3 systems in %.2f ms (%.3f ms/frame)\n",
        kFrames,
        kEntities,
        ms,
        ms / kFrames
    );

    cd::profile::set_sink(prev);

    // Per-scope rollup so we see which system dominates.
    cd::profile::StatsAggregator agg;
    agg.apply(sink.snapshot());
    const auto rows = agg.snapshot();
    std::printf("hello_scheduler: per-scope rollup (sorted by total time):\n");
    for (const auto& r : rows)
    {
        std::printf(
            "  %-12s  count=%5llu  avg=%.3f us  total=%.3f ms\n",
            r.name.c_str(),
            static_cast<unsigned long long>(r.count),
            r.avg_ns() / 1000.0,
            static_cast<double>(r.total_ns) / 1e6
        );
    }
    return 0;
}
