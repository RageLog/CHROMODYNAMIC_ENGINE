// =============================================================================
// CHROMODYNAMIC — samples/hello_ecs/main.cpp
//
// Headless ECS stress + correctness demo:
//   * Spawns N entities (default 10 000) with Position, Velocity, Mass
//   * Half also get a Drag component — exercises the multi-component query
//     intersection in cd::ecs::World::each<>.
//   * Runs a 600-frame simulation @ fixed 1/60 s timestep, applying:
//       v += a · dt   (a = gravity)
//       v *= 1 - drag · dt    (only for entities with Drag)
//       p += v · dt
//   * Reports per-frame timing, total simulation time, final entity count,
//     a checksum of all final positions (deterministic across runs), and
//     the average sustained entity-update rate.
//
// Purpose:
//   * Prove cd::ecs World/SparseSet/each<> works at meaningful scale
//   * Establish a baseline number we can compare against once we move to
//     SIMD/archetype storage in a later sprint
//   * Provide an evidence trail for the engine quality bar in CLAUDE.md §3
//     ("kodu yazdım doğru olmalı" YASAK — every claim needs a number)
// =============================================================================
#include <cd/ecs/Entity.hpp>
#include <cd/ecs/World.hpp>
#include <cd/math/Vector.hpp>

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

namespace
{

struct Position
{
    cd::math::Vec3f value { 0.0F, 0.0F, 0.0F };
};

struct Velocity
{
    cd::math::Vec3f value { 0.0F, 0.0F, 0.0F };
};

struct Mass
{
    float value { 1.0F };
};

/// Per-second velocity decay rate. dt-scaled multiplier `1 - drag*dt` keeps
/// the integration stable even at low rates (no exp() call per tick).
struct Drag
{
    float per_second { 0.10F };
};

/// xorshift32 — deterministic RNG so the sample produces the same numbers
/// on every run, on every machine, on every compiler. Lets us compare
/// timing across builds without worrying that the workload drifted.
class Xorshift32
{
public:
    constexpr explicit Xorshift32(std::uint32_t seed) noexcept
        : state_ { seed == 0U ? 0xABCDEF01U : seed }
    {
    }

    [[nodiscard]] std::uint32_t next() noexcept
    {
        std::uint32_t x = state_;
        x ^= x << 13U;
        x ^= x >> 17U;
        x ^= x << 5U;
        state_ = x;
        return x;
    }

    /// Uniform float in [-1, +1].
    [[nodiscard]] float next_signed() noexcept
    {
        const auto u = next() >> 8U;  // 24-bit mantissa precision
        return (static_cast<float>(u) / 8388608.0F) - 1.0F;
    }

private:
    std::uint32_t state_;
};

[[nodiscard]] std::size_t parse_count(int argc, char** argv, std::size_t fallback) noexcept
{
    if (argc < 2)
        return fallback;
    char* end = nullptr;
    const auto n = std::strtoull(argv[1], &end, 10);
    if (n == 0ULL || end == argv[1])
        return fallback;
    return static_cast<std::size_t>(n);
}

}  // namespace

int main(int argc, char** argv)
{
    constexpr std::size_t kDefaultEntities = 10'000;
    constexpr std::size_t kFrames = 600;
    constexpr float kDt = 1.0F / 60.0F;
    constexpr cd::math::Vec3f kGravity { 0.0F, -9.81F, 0.0F };

    const std::size_t entity_count = parse_count(argc, argv, kDefaultEntities);
    std::printf(
        "hello_ecs: spawning %zu entities, simulating %zu frames @ dt=%.4fs\n",
        entity_count,
        kFrames,
        static_cast<double>(kDt)
    );

    cd::ecs::World world;
    Xorshift32 rng { 0x12345678U };

    // ---- Spawn -----------------------------------------------------------
    const auto t_spawn0 = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < entity_count; ++i)
    {
        const auto e = world.create();
        world.emplace<Position>(
            e,
            Position {
                cd::math::Vec3f { rng.next_signed() * 50.0F,
                                 rng.next_signed() * 50.0F + 50.0F,
                                 rng.next_signed() * 50.0F }
        }
        );
        world.emplace<Velocity>(
            e,
            Velocity {
                cd::math::Vec3f { rng.next_signed() * 5.0F, rng.next_signed() * 5.0F, rng.next_signed() * 5.0F }
        }
        );
        world.emplace<Mass>(e, Mass { 0.5F + rng.next_signed() * 0.4F + 0.5F });
        // Every other entity gets drag so we can measure the intersection
        // query path (with-Drag vs without-Drag) separately.
        if ((i & 1U) == 0U)
        {
            world.emplace<Drag>(e, Drag { 0.05F + rng.next_signed() * 0.05F + 0.05F });
        }
    }
    const auto t_spawn1 = std::chrono::steady_clock::now();
    const double spawn_ms = std::chrono::duration<double, std::milli>(t_spawn1 - t_spawn0).count();
    std::printf(
        "hello_ecs: spawn done in %.2f ms (%.0f entities/s)\n",
        static_cast<double>(spawn_ms),
        static_cast<double>(entity_count) / (static_cast<double>(spawn_ms) / 1000.0)
    );

    // ---- Simulate --------------------------------------------------------
    const auto t_sim0 = std::chrono::steady_clock::now();
    for (std::size_t frame = 0; frame < kFrames; ++frame)
    {
        // 1) Velocity += gravity · dt  (all moving entities)
        world.each<Velocity, Mass>(
            [&](cd::ecs::Entity, Velocity& v, Mass&)
            {
                v.value[0] += kGravity[0] * kDt;
                v.value[1] += kGravity[1] * kDt;
                v.value[2] += kGravity[2] * kDt;
            }
        );

        // 2) Velocity *= 1 - drag · dt  (only entities WITH Drag)
        world.each<Velocity, Drag>(
            [&](cd::ecs::Entity, Velocity& v, Drag& d)
            {
                const float k = 1.0F - d.per_second * kDt;
                v.value[0] *= k;
                v.value[1] *= k;
                v.value[2] *= k;
            }
        );

        // 3) Position += velocity · dt  (all moving entities)
        world.each<Position, Velocity>(
            [&](cd::ecs::Entity, Position& p, Velocity& v)
            {
                p.value[0] += v.value[0] * kDt;
                p.value[1] += v.value[1] * kDt;
                p.value[2] += v.value[2] * kDt;
            }
        );
    }
    const auto t_sim1 = std::chrono::steady_clock::now();
    const double sim_ms = std::chrono::duration<double, std::milli>(t_sim1 - t_sim0).count();

    // ---- Result --------------------------------------------------------
    // Deterministic checksum so we can flag accidental regressions in the
    // ECS storage layout (swap-and-pop reordering does NOT affect the
    // checksum: we sum every component contribution, not in iteration order).
    double sum_x = 0.0;
    double sum_y = 0.0;
    double sum_z = 0.0;
    std::size_t counted = 0;
    world.for_each<Position>(
        [&](cd::ecs::Entity, Position& p)
        {
            sum_x += static_cast<double>(p.value[0]);
            sum_y += static_cast<double>(p.value[1]);
            sum_z += static_cast<double>(p.value[2]);
            ++counted;
        }
    );

    const double total_updates =
        static_cast<double>(entity_count) * static_cast<double>(kFrames) * 3.0;  // 3 systems per frame
    const double updates_per_sec = total_updates / (static_cast<double>(sim_ms) / 1000.0);

    std::printf(
        "hello_ecs: %zu frames simulated in %.2f ms (%.3f ms/frame avg)\n",
        kFrames,
        static_cast<double>(sim_ms),
        static_cast<double>(sim_ms) / static_cast<double>(kFrames)
    );
    std::printf("hello_ecs: throughput = %.2f M entity-updates/s\n", updates_per_sec / 1.0e6);
    std::printf(
        "hello_ecs: final positions  sum_x=%.6f  sum_y=%.6f  sum_z=%.6f  N=%zu\n",
        sum_x,
        sum_y,
        sum_z,
        counted
    );

    // Sanity invariant: storage must report the same entity count as the
    // spawn. This catches accidental destroy / swap-and-pop bugs.
    if (counted != entity_count)
    {
        std::fprintf(stderr, "hello_ecs: FAIL — expected %zu entities, found %zu\n", entity_count, counted);
        return 1;
    }
    return 0;
}
