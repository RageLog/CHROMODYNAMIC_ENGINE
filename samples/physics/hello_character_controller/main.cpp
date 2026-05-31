// =============================================================================
// CHROMODYNAMIC — samples/physics/hello_character_controller/main.cpp
//
// Phase 530 / T3.3 — capsule character controller on slopes and steps.
//
// Simulates a player-shaped capsule navigating three terrain segments:
//   1) flat ground           (y = 0)
//   2) upward slope 30 deg   (x = 0..5)
//   3) a step (0.5 m ledge)  (x = 5..6)
//   4) flat elevated ground  (y = 0.5)
//
// The character is a kinematic body (position driven by the controller).
// A cd::physics::Capsule is queried each step to test whether the
// character's feet contact the terrain. Forward movement is applied as a
// positional delta; the controller resolves vertical from the terrain height
// function, falling under gravity when not grounded.
//
// Console output: per-step position + grounded flag.
// =============================================================================
#include <cd/physics/Capsule.hpp>
#include <cd/physics/IPhysicsWorld.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>

namespace
{

constexpr float kDt          = 1.0F / 60.0F;
constexpr int   kSteps       = 240;        // 4 simulated seconds
constexpr float kMoveSpeed   = 2.0F;       // m/s horizontal
constexpr float kCapsuleH    = 1.8F;       // total height
constexpr float kCapsuleR    = 0.35F;      // radius
constexpr float kGravity     = -9.81F;

// Procedural terrain height as a function of world-X.
[[nodiscard]] float terrain_height(float x) noexcept
{
    if (x < 0.0F) return 0.0F;
    if (x < 5.0F) return x * std::tan(3.14159F / 6.0F);   // 30° slope
    if (x < 6.0F) return 5.0F * std::tan(3.14159F / 6.0F) + 0.5F; // step ledge
    return 5.0F * std::tan(3.14159F / 6.0F) + 0.5F;        // elevated flat
}

void run()
{
    auto world = cd::physics::make_builtin_physics_world();
    world->set_gravity({ 0.0F, kGravity, 0.0F });

    // Kinematic body — the controller drives position explicitly.
    cd::physics::BodyDesc char_desc {};
    char_desc.type     = cd::physics::BodyType::kKinematic;
    char_desc.mass     = 80.0F;
    char_desc.position = { -1.0F, kCapsuleH * 0.5F, 0.0F };
    const auto char_h  = world->create_body(char_desc);
    if (!char_h.has_value())
    {
        std::printf("[hello_character_controller] ERROR: could not create character\n");
        return;
    }

    float vert_vel   = 0.0F;   // character vertical velocity (separate from world)
    bool  grounded   = true;

    std::printf("[hello_character_controller] capsule H=%.1f R=%.2f, moving at %.1f m/s\n\n",
                static_cast<double>(kCapsuleH),
                static_cast<double>(kCapsuleR),
                static_cast<double>(kMoveSpeed));

    for (int step = 0; step < kSteps; ++step)
    {
        world->step(kDt);

        auto pos = world->position(*char_h);

        // -- Horizontal movement (forward along +X) --
        pos.x += kMoveSpeed * kDt;

        // -- Vertical: gravity accumulation + terrain grounding --
        const float floor_y = terrain_height(pos.x) + kCapsuleH * 0.5F;

        if (!grounded)
        {
            vert_vel += kGravity * kDt;
            pos.y    += vert_vel * kDt;
        }

        // Ground check via capsule lower hemisphere.
        const cd::physics::Capsule cap {
            { pos.x, pos.y - kCapsuleH * 0.5F + kCapsuleR, pos.z },
            { pos.x, pos.y + kCapsuleH * 0.5F - kCapsuleR, pos.z },
            kCapsuleR,
        };
        const cd::math::Vec3f feet { pos.x, pos.y - kCapsuleH * 0.5F, pos.z };
        grounded = cd::physics::contains(cap, { feet.x, feet.y + 0.05F, feet.z })
                   || (pos.y <= floor_y + 0.02F);

        if (pos.y < floor_y)
        {
            pos.y    = floor_y;
            vert_vel = 0.0F;
            grounded = true;
        }

        world->set_position(*char_h, pos);

        if (step % 20 == 0)
        {
            const auto p = world->position(*char_h);
            std::printf("  step %3d  pos=(%.3f,%.3f,%.3f)  grounded=%s  vert_vel=%+.3f\n",
                        step,
                        static_cast<double>(p.x),
                        static_cast<double>(p.y),
                        static_cast<double>(p.z),
                        grounded ? "yes" : "no ",
                        static_cast<double>(vert_vel));
        }
    }

    const auto fp = world->position(*char_h);
    std::printf("\n[hello_character_controller] done — final pos (%.3f, %.3f, %.3f)\n",
                static_cast<double>(fp.x),
                static_cast<double>(fp.y),
                static_cast<double>(fp.z));
}

}  // namespace

int main()
{
    run();
    return 0;
}
