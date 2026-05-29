// =============================================================================
// CHROMODYNAMIC -- samples/world/bench_archetype
//
// Phase 370 / Marathon Run 30 / X7B -- ArchetypeWorld vs SparseSetWorld
// iteration microbenchmark.  1000-entity query, both storage paths.
//
// What this measures:
//   * SparseSetWorld: cd::ecs::World::each<Position, Velocity>()
//     -- per-row pool lookup + has<> intersection probe (smallest-pool
//     driver), reference per-entity pointer dereference.
//   * ArchetypeWorld: cd::ecs::ArchetypeWorld::each<Position, Velocity>()
//     -- archetype subset match + chunked column-base pointer + linear
//     row walk.
//
// What this does NOT measure:
//   * Insert / remove throughput (archetype dense rows favor batch
//     insert; sparse-set favors incremental churn).
//   * Random access (sparse-set is O(1); archetype is O(rows-walked)).
//   * Cross-archetype migration (archetype side layer does not
//     support it; sparse-set has no equivalent operation).
//
// Both timings are reported.  The number is the engine's own honest
// snapshot under -O2 / Debug build.  Use the Release preset for
// definitive figures.
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
    constexpr int kIters    = 1000;  // amortise timer overhead

    // ---- Sparse-set path ---------------------------------------------------
    cd::ecs::World sw;
    for (int i = 0; i < kEntities; ++i) {
        auto e = sw.create();
        sw.emplace<Position>(e, Position { static_cast<float>(i), 0.0F, 0.0F });
        sw.emplace<Velocity>(e, Velocity { 1.0F, 0.0F, 0.0F });
    }
    auto query = sw.query<Position, Velocity>();  // cache pool ptrs

    // ---- Archetype-world path ---------------------------------------------
    cd::ecs::ArchetypeWorld aw;
    for (int i = 0; i < kEntities; ++i) {
        (void)aw.emplace<Position, Velocity>(
            Position { static_cast<float>(i), 0.0F, 0.0F },
            Velocity { 1.0F, 0.0F, 0.0F });
    }

    // ---- Timing helper ----------------------------------------------------
    auto now = []() {
        return std::chrono::high_resolution_clock::now();
    };

    // ---- Sparse-set query timing ------------------------------------------
    // Touch both components so the optimiser cannot DCE the loop body.
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

    // ---- Archetype query timing -------------------------------------------
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

    // ---- Report ------------------------------------------------------------
    std::printf("CHROMODYNAMIC -- ArchetypeWorld vs SparseSetWorld microbench\n");
    std::printf("  entities    : %d\n", kEntities);
    std::printf("  iterations  : %d\n", kIters);
    std::printf("  query       : each<Position, Velocity>\n");
    std::printf("\n");
    std::printf("[sparse-set ] %.3f us / iter  (%.2f ns / entity)\n",
                per_iter_sw / 1000.0, per_entity_ns_sw);
    std::printf("[archetype  ] %.3f us / iter  (%.2f ns / entity)\n",
                per_iter_aw / 1000.0, per_entity_ns_aw);
    if (per_entity_ns_aw < per_entity_ns_sw) {
        const double speedup = per_entity_ns_sw / per_entity_ns_aw;
        std::printf("[result     ] archetype %.2fx faster on this query\n", speedup);
    } else {
        const double speedup = per_entity_ns_aw / per_entity_ns_sw;
        std::printf("[result     ] sparse-set %.2fx faster on this query\n", speedup);
    }
    // Reference the sinks so the optimiser must keep the loops live.
    (void)sink_sw;
    (void)sink_aw;
    return 0;
}
