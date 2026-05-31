// =============================================================================
// CHROMODYNAMIC — samples/physics/hello_ragdoll/main.cpp
//
// Phase 530 / T3.3 — multi-joint constraint chain simulating a ragdoll.
//
// Models a simplified 7-segment ragdoll: torso -> head, l/r upper arm,
// l/r lower arm. Each pair is held together by a spring (cd::physics::
// SpringJoint). The constraint forces are applied each step as impulses
// so the built-in Euler integrator enforces them without a full constraint
// solver. This is the "assertion-only sim" approach permitted while Jolt
// (Phase 525) is pending.
//
// Hierarchy:
//   [0] torso  (dynamic, heavy anchor)
//   [1] head   <- spring from torso
//   [2] l_uarm <- spring from torso
//   [3] r_uarm <- spring from torso
//   [4] l_larm <- spring from l_uarm
//   [5] r_larm <- spring from r_uarm
//
// Simulation: 120 steps @ 1/60 s — prints torso + head position each 20 steps.
// =============================================================================
#include <cd/physics/IPhysicsWorld.hpp>
#include <cd/physics/SpringJoint.hpp>

#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

namespace
{

constexpr float kDt    = 1.0F / 60.0F;
constexpr int   kSteps = 120;

// Enforce a single spring joint for one step. Applies symmetric impulses to
// both bodies to restore the rest length. This is a simple Hooke's-law PBD
// (position-based dynamics) correction — adequate for console demonstration.
void enforce_spring(cd::physics::IPhysicsWorld& world,
                    const cd::physics::SpringJoint& joint,
                    cd::physics::BodyHandle ha,
                    cd::physics::BodyHandle hb,
                    float dt) noexcept
{
    const auto pa = world.position(ha);
    const auto pb = world.position(hb);
    const float dx = pb.x - pa.x;
    const float dy = pb.y - pa.y;
    const float dz = pb.z - pa.z;
    const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (dist < 1e-6F)
        return;

    const float stretch = dist - joint.rest_length;
    // Spring force magnitude = stiffness * stretch
    // Apply as impulse proportional to dt (forward-Euler on the constraint).
    const float inv_dist = 1.0F / dist;
    const float force_mag = joint.stiffness * stretch;

    // Relative velocity along the joint axis — used for damping.
    const auto va = world.linear_velocity(ha);
    const auto vb = world.linear_velocity(hb);
    const float rel_v = (vb.x - va.x) * dx * inv_dist
                      + (vb.y - va.y) * dy * inv_dist
                      + (vb.z - va.z) * dz * inv_dist;
    const float damp_mag = joint.damping * rel_v;

    const float impulse_mag = (force_mag + damp_mag) * dt;
    const cd::math::Vec3f impulse_a {
        impulse_mag * dx * inv_dist,
        impulse_mag * dy * inv_dist,
        impulse_mag * dz * inv_dist,
    };
    const cd::math::Vec3f impulse_b {
        -impulse_a.x,
        -impulse_a.y,
        -impulse_a.z,
    };
    world.apply_impulse(ha, impulse_a);
    world.apply_impulse(hb, impulse_b);
}

void run()
{
    auto world = cd::physics::make_builtin_physics_world();
    world->set_gravity({ 0.0F, -9.81F, 0.0F });

    // Spawn ragdoll segments.  Positions are relative to the torso (0,5,0).
    struct Segment { const char* name; float mass; cd::math::Vec3f pos; };
    const std::array<Segment, 6> segments {{
        { "torso ", 10.0F, { 0.0F,  5.0F, 0.0F } },
        { "head  ",  2.0F, { 0.0F,  6.5F, 0.0F } },
        { "l_uarm",  2.5F, {-1.0F,  5.2F, 0.0F } },
        { "r_uarm",  2.5F, { 1.0F,  5.2F, 0.0F } },
        { "l_larm",  1.5F, {-1.8F,  4.0F, 0.0F } },
        { "r_larm",  1.5F, { 1.8F,  4.0F, 0.0F } },
    }};

    std::vector<cd::physics::BodyHandle> handles;
    handles.reserve(segments.size());
    for (const auto& seg : segments)
    {
        cd::physics::BodyDesc bd {};
        bd.type             = cd::physics::BodyType::kDynamic;
        bd.mass             = seg.mass;
        bd.linear_damping   = 0.5F;
        bd.position         = seg.pos;
        auto h = world->create_body(bd);
        if (!h.has_value())
        {
            std::printf("[hello_ragdoll] ERROR: could not create segment '%s'\n", seg.name);
            return;
        }
        handles.push_back(*h);
    }

    // Build spring joints: parent index -> child index, rest_length.
    struct Link { std::size_t parent; std::size_t child; float rest; };
    const std::array<Link, 5> links {{
        { 0, 1, 1.5F },   // torso -> head
        { 0, 2, 1.1F },   // torso -> l_uarm
        { 0, 3, 1.1F },   // torso -> r_uarm
        { 2, 4, 1.3F },   // l_uarm -> l_larm
        { 3, 5, 1.3F },   // r_uarm -> r_larm
    }};

    std::vector<cd::physics::SpringJoint> joints;
    joints.reserve(links.size());
    for (const auto& lk : links)
        joints.push_back(cd::physics::make_rope_link(
            static_cast<std::uint32_t>(lk.parent),
            static_cast<std::uint32_t>(lk.child),
            lk.rest));

    std::printf("[hello_ragdoll] %zu segments, %zu joints, simulating %d steps\n\n",
                handles.size(), joints.size(), kSteps);

    for (int step = 0; step < kSteps; ++step)
    {
        // Apply spring constraints before the physics step (sub-step approach).
        for (const auto& jt : joints)
            enforce_spring(*world, jt,
                           handles[jt.body_a],
                           handles[jt.body_b],
                           kDt);

        world->step(kDt);

        // Simple floor clamp for every segment.
        for (auto& h : handles)
        {
            const auto p = world->position(h);
            if (p.y < 0.3F)
            {
                const auto v = world->linear_velocity(h);
                world->set_position(h, { p.x, 0.3F, p.z });
                if (v.y < 0.0F)
                    world->set_linear_velocity(h, { v.x * 0.8F, -v.y * 0.2F, v.z * 0.8F });
            }
        }

        if (step % 20 == 0)
        {
            const auto pt = world->position(handles[0]);
            const auto ph = world->position(handles[1]);
            std::printf("  step %3d  torso=(%.3f,%.3f,%.3f)  head=(%.3f,%.3f,%.3f)\n",
                        step,
                        static_cast<double>(pt.x), static_cast<double>(pt.y), static_cast<double>(pt.z),
                        static_cast<double>(ph.x), static_cast<double>(ph.y), static_cast<double>(ph.z));
        }
    }

    std::printf("\n[hello_ragdoll] done — bodies in world: %zu\n", world->body_count());
}

}  // namespace

int main()
{
    run();
    return 0;
}
