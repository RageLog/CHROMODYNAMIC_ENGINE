// =============================================================================
// CHROMODYNAMIC — samples/physics/hello_falling_box/main.cpp
//
// Phase 530 / T3.3 — single dynamic box falling onto a static ground plane.
//
// Demonstrates:
//   * make_builtin_physics_world() factory
//   * BodyType::kStatic (immovable ground)  + BodyType::kDynamic (free-falling box)
//   * Semi-implicit Euler gravity integration over 120 steps at 1/60 s
//   * Simple collision response: box is reflected back up when it crosses y=0
//
// NOTE: cd::physics_jolt is the full-fidelity backend (Phase 525 / W4).
// Until Jolt ships this sample uses the built-in Euler integrator which
// implements gravity + impulse but has no collision detection. A manual
// floor reflection below demonstrates the collision hook that a real backend
// will supply automatically.
//
// Console output:  step N  box.y = <value>  box.vy = <value>
// =============================================================================
#include <cd/physics/IPhysicsWorld.hpp>

#include <cstdio>
#include <memory>

namespace
{

constexpr float kDt          = 1.0F / 60.0F;  // fixed timestep  (s)
constexpr int   kSteps       = 120;            // 2 simulated seconds
constexpr float kFloorY      = 0.0F;           // static ground plane  (y = 0)
constexpr float kBoxHalfSize = 0.5F;           // half-extent of the 1x1x1 box
constexpr float kRestitution = 0.6F;           // energy retained on bounce

void run()
{
    auto world = cd::physics::make_builtin_physics_world();
    world->set_gravity({ 0.0F, -9.81F, 0.0F });

    // --- Static ground plane body (position signals the surface y) -----------
    cd::physics::BodyDesc ground_desc {};
    ground_desc.type     = cd::physics::BodyType::kStatic;
    ground_desc.position = { 0.0F, kFloorY, 0.0F };
    ground_desc.mass     = 1.0F;  // ignored for static
    const auto ground    = world->create_body(ground_desc);
    if (!ground.has_value())
    {
        std::printf("[hello_falling_box] ERROR: could not create ground body\n");
        return;
    }

    // --- Dynamic box body starting 10 m above the floor ----------------------
    cd::physics::BodyDesc box_desc {};
    box_desc.type             = cd::physics::BodyType::kDynamic;
    box_desc.position         = { 0.0F, 10.0F, 0.0F };
    box_desc.mass             = 2.0F;
    box_desc.linear_damping   = 0.01F;  // light air resistance
    const auto box            = world->create_body(box_desc);
    if (!box.has_value())
    {
        std::printf("[hello_falling_box] ERROR: could not create box body\n");
        return;
    }

    std::printf("[hello_falling_box] starting simulation — %d steps @ %.4f s\n",
                kSteps, static_cast<double>(kDt));
    std::printf("  ground at y=%.2f   box starts at y=%.2f   mass=%.1f kg\n\n",
                static_cast<double>(kFloorY),
                static_cast<double>(world->position(*box).y),
                static_cast<double>(box_desc.mass));

    for (int step = 0; step < kSteps; ++step)
    {
        world->step(kDt);

        const auto pos = world->position(*box);
        const auto vel = world->linear_velocity(*box);

        // Manual floor collision — the built-in integrator does not include
        // collision detection. This mimics what Jolt will provide natively.
        const float contact_y = kFloorY + kBoxHalfSize;
        if (pos.y < contact_y && vel.y < 0.0F)
        {
            // Positional correction: push the box back above the floor.
            world->set_position(*box, { pos.x, contact_y, pos.z });
            // Velocity reflection with restitution.
            world->set_linear_velocity(*box, { vel.x, -vel.y * kRestitution, vel.z });
        }

        if (step % 10 == 0)
        {
            const auto p2 = world->position(*box);
            const auto v2 = world->linear_velocity(*box);
            std::printf("  step %3d  box.y = %8.4f  box.vy = %8.4f\n",
                        step,
                        static_cast<double>(p2.y),
                        static_cast<double>(v2.y));
        }
    }

    const auto final_pos = world->position(*box);
    std::printf("\n[hello_falling_box] done — final box position: (%.4f, %.4f, %.4f)\n",
                static_cast<double>(final_pos.x),
                static_cast<double>(final_pos.y),
                static_cast<double>(final_pos.z));
    std::printf("  bodies in world: %zu\n", world->body_count());
}

}  // namespace

int main()
{
    run();
    return 0;
}
