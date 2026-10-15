// =============================================================================
// CHROMODYNAMIC -- tools/bench_archetype
//
// ArchetypeWorld vs SparseSetWorld iteration microbenchmark.
// =============================================================================
#include <cd/ecs/ArchetypeWorld.hpp>
#include <cd/ecs/World.hpp>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace
{

struct Position { float x, y, z; };
struct Velocity { float vx, vy, vz; };

}  // namespace

int main()
{
    constexpr int kEntities = 1000;
    constexpr int kIters    = 1000;

    cd::ecs::World sw;
    for (int i = 0; i < kEntities; ++i) {
        auto e = sw.create();
        sw.emplace<Position>(e, Position { static_cast<float>(i), 0.0F, 0.0F });
        sw.emplace<Velocity>(e, Velocity { 1.0F, 0.0F, 0.0F });
    }
    auto query = sw.query<Position, Velocity>();

    cd::ecs::ArchetypeWorld aw;
    for (int i = 0; i < kEntities; ++i) {
        (void)aw.emplace<Position, Velocity>(
            Position { static_cast<float>(i), 0.0F, 0.0F },
            Velocity { 1.0F, 0.0F, 0.0F });
    }

    auto now = []() {
        return std::chrono::high_resolution_clock::now();
    };

    volatile float sink_sw = 0.0F;
    auto t0_sw = now();
    for (int i = 0; i < kIters; ++i) {
        query.each(sw, [&](cd::ecs::Entity, Position& p, Velocity& v) {
            sink_sw = sink_sw + p.x * v.vx;
        });
    }
    auto t1_sw = now();
    const double ns_sw =
        std::chrono::duration<double, std::nano>(t1_sw - t0_sw).count();
    const double per_iter_sw = ns_sw / kIters;
    const double per_entity_ns_sw = per_iter_sw / kEntities;

    volatile float sink_aw = 0.0F;
    auto t0_aw = now();
    for (int i = 0; i < kIters; ++i) {
        aw.each<Position, Velocity>(
            [&](cd::ecs::Entity, Position& p, Velocity& v) {
                sink_aw = sink_aw + p.x * v.vx;
            });
    }
    auto t1_aw = now();
    const double ns_aw =
        std::chrono::duration<double, std::nano>(t1_aw - t0_aw).count();
    const double per_iter_aw = ns_aw / kIters;
    const double per_entity_ns_aw = per_iter_aw / kEntities;

    std::printf("CHROMODYNAMIC -- ArchetypeWorld vs SparseSetWorld microbench\n");
    std::printf("  entities    : %d\n", kEntities);
    std::printf("  iterations  : %d\n", kIters);
    std::printf("  query       : each<Position, Velocity>\n\n");
    std::printf("[sparse-set ] %.3f us / iter  (%.2f ns / entity)\n",
                per_iter_sw / 1000.0, per_entity_ns_sw);
    std::printf("[archetype  ] %.3f us / iter  (%.2f ns / entity)\n",
                per_iter_aw / 1000.0, per_entity_ns_aw);
    (void)sink_sw;
    (void)sink_aw;
    return 0;
}
