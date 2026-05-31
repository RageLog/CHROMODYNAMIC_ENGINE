// =============================================================================
// CHROMODYNAMIC — samples/physics/hello_pile/main.cpp
//
// Phase 530 / T3.3 — 50 dynamic spheres settling into a pile on a static floor.
//
// Demonstrates:
//   * Spawning many (50) dynamic bodies with randomised start positions
//   * Linear damping to simulate rolling friction / dissipation
//   * Per-step kinetic energy tracking to observe settling behaviour
//   * Manual floor clamp (built-in integrator; Jolt collision is transparent)
//
// Console output: per-step total KE   every 30 steps, final body statistics.
// =============================================================================
#include <cd/physics/IPhysicsWorld.hpp>

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <memory>
#include <vector>

namespace
{

constexpr int   kNumBalls    = 50;
constexpr float kDt          = 1.0F / 60.0F;
constexpr int   kSteps       = 300;   // 5 simulated seconds
constexpr float kRadius      = 0.5F;
constexpr float kFloorY      = 0.0F;
constexpr float kRestitution = 0.3F;  // highly damped — pile settles fast

// Minimal deterministic LCG so the sample has no external dependency.
struct Lcg
{
    std::uint32_t state { 12345u };
    [[nodiscard]] float next_f(float lo, float hi) noexcept
    {
        state = state * 1664525u + 1013904223u;
        const float f = static_cast<float>(state >> 16) / 65535.0F;
        return lo + f * (hi - lo);
    }
};

void run()
{
    auto world = cd::physics::make_builtin_physics_world();
    world->set_gravity({ 0.0F, -9.81F, 0.0F });

    // Static floor.
    cd::physics::BodyDesc floor_desc {};
    floor_desc.type     = cd::physics::BodyType::kStatic;
    floor_desc.position = { 0.0F, kFloorY, 0.0F };
    floor_desc.mass     = 1.0F;
    const auto floor_h  = world->create_body(floor_desc);
    if (!floor_h.has_value())
    {
        std::printf("[hello_pile] ERROR: could not create floor\n");
        return;
    }

    // Dynamic balls — stacked loosely in a 5x5x2 grid with jitter.
    std::vector<cd::physics::BodyHandle> balls;
    balls.reserve(kNumBalls);
    Lcg rng {};
    for (int i = 0; i < kNumBalls; ++i)
    {
        const float col = static_cast<float>(i % 5) - 2.0F;
        const float row = static_cast<float>((i / 5) % 5) - 2.0F;
        const float lay = static_cast<float>(i / 25);

        cd::physics::BodyDesc bd {};
        bd.type           = cd::physics::BodyType::kDynamic;
        bd.mass           = rng.next_f(0.5F, 2.0F);
        bd.linear_damping = 1.5F;  // strong damping → fast pile settling
        bd.position       = {
            col + rng.next_f(-0.2F, 0.2F),
            kRadius + row * 1.1F + lay * 5.0F + 2.0F,
            rng.next_f(-1.0F, 1.0F),
        };
        bd.linear_velocity = { rng.next_f(-0.5F, 0.5F), 0.0F, rng.next_f(-0.5F, 0.5F) };
        auto h = world->create_body(bd);
        if (h.has_value())
            balls.push_back(*h);
    }

    std::printf("[hello_pile] %zu balls spawned, simulating %d steps\n\n",
                balls.size(), kSteps);

    for (int step = 0; step < kSteps; ++step)
    {
        world->step(kDt);

        // Floor collision pass — clamp each ball above the floor.
        for (const auto& h : balls)
        {
            const auto p = world->position(h);
            const auto v = world->linear_velocity(h);
            const float contact_y = kFloorY + kRadius;
            if (p.y < contact_y && v.y < 0.0F)
            {
                world->set_position(h, { p.x, contact_y, p.z });
                world->set_linear_velocity(h, { v.x * 0.9F, -v.y * kRestitution, v.z * 0.9F });
            }
        }

        if (step % 30 == 0)
        {
            double ke = 0.0;
            for (const auto& h : balls)
            {
                const auto v = world->linear_velocity(h);
                const float speed2 = v.x * v.x + v.y * v.y + v.z * v.z;
                ke += 0.5 * static_cast<double>(speed2);  // m=1 normalised
            }
            std::printf("  step %3d  total KE = %10.4f  bodies = %zu\n",
                        step, ke, world->body_count());
        }
    }

    // Final statistics.
    float y_min = 1e9F;
    float y_max = -1e9F;
    for (const auto& h : balls)
    {
        const float y = world->position(h).y;
        if (y < y_min) y_min = y;
        if (y > y_max) y_max = y;
    }
    std::printf("\n[hello_pile] done — pile height: %.3f m (lowest %.3f, highest %.3f)\n",
                static_cast<double>(y_max - y_min),
                static_cast<double>(y_min),
                static_cast<double>(y_max));
}

}  // namespace

int main()
{
    run();
    return 0;
}
