// =============================================================================
// CHROMODYNAMIC — samples/physics/hello_raycast/main.cpp
//
// Phase 530 / T3.3 — ray queries against static and dynamic bodies.
//
// Demonstrates:
//   * cd::physics::Ray + intersect_ray_aabb  (static AABB scene objects)
//   * cd::physics::intersect_ray_sphere      (dynamic body bounding sphere)
//   * A multi-body "scene traversal": cast N rays from different origins,
//     test against all scene objects, report closest hit
//
// Scene layout:
//   * 4 static AABB obstacles on the XZ plane
//   * 3 dynamic sphere-bounded moving balls (simulated 60 steps then queried)
//   * 8 rays cast from z = -20, pointing in +Z; evenly spread along X
//
// Console output: per-ray result (hit or miss, body type, t, hit point).
// =============================================================================
#include <cd/physics/Aabb.hpp>
#include <cd/physics/IPhysicsWorld.hpp>
#include <cd/physics/Ray.hpp>
#include <cd/physics/RaySphere.hpp>
#include <cd/physics/Sphere.hpp>

#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <optional>
#include <vector>

namespace
{

constexpr float kDt             = 1.0F / 60.0F;
constexpr int   kWarmupSteps    = 60;    // let dynamic balls fall before querying
constexpr int   kNumRays        = 8;
constexpr float kRaySpacingX    = 1.5F;
constexpr float kRayOriginZ     = -20.0F;

// Static AABB obstacles (world-space).
const std::array<cd::physics::Aabb, 4> kStaticBoxes {{
    { { -4.0F, 0.0F, 0.0F }, {  -3.0F, 3.0F, 1.0F } },
    { { -1.0F, 0.0F, 1.0F }, {   0.0F, 2.0F, 2.0F } },
    { {  2.0F, 0.0F,-1.0F }, {   3.0F, 4.0F, 0.0F } },
    { {  4.5F, 0.0F, 0.5F }, {   5.5F, 1.5F, 1.5F } },
}};

struct HitResult
{
    bool        hit       { false };
    float       t         { 0.0F };
    const char* body_kind { "none" };
    cd::math::Vec3f point {};
};

// Test a ray against all static AABBs; return closest.
[[nodiscard]] HitResult cast_vs_statics(const cd::physics::Ray& ray) noexcept
{
    HitResult best;
    best.t = 1.0e30F;
    for (const auto& box : kStaticBoxes)
    {
        auto t = cd::physics::intersect_ray_aabb(ray, box);
        if (t.has_value() && *t < best.t)
        {
            best.hit       = true;
            best.t         = *t;
            best.body_kind = "static-AABB";
            best.point     = {
                ray.origin.x + ray.direction.x * *t,
                ray.origin.y + ray.direction.y * *t,
                ray.origin.z + ray.direction.z * *t,
            };
        }
    }
    return best;
}

// Test a ray against a set of sphere-bounded dynamic bodies; return closest.
[[nodiscard]] HitResult cast_vs_dynamics(
    const cd::physics::Ray& ray,
    const std::vector<cd::physics::BodyHandle>& handles,
    const cd::physics::IPhysicsWorld& world,
    float ball_radius) noexcept
{
    HitResult best;
    best.t = 1.0e30F;
    for (const auto& h : handles)
    {
        const auto pos = world.position(h);
        const cd::physics::Sphere bsphere { pos, ball_radius };
        auto t = cd::physics::intersect_ray_sphere(ray, bsphere);
        if (t.has_value() && *t < best.t)
        {
            best.hit       = true;
            best.t         = *t;
            best.body_kind = "dynamic-sphere";
            best.point     = {
                ray.origin.x + ray.direction.x * *t,
                ray.origin.y + ray.direction.y * *t,
                ray.origin.z + ray.direction.z * *t,
            };
        }
    }
    return best;
}

void run()
{
    auto world = cd::physics::make_builtin_physics_world();
    world->set_gravity({ 0.0F, -9.81F, 0.0F });

    // Three dynamic balls starting at height 8 m, spread on X.
    constexpr float kBallRadius = 0.6F;
    const std::array<cd::math::Vec3f, 3> ball_starts {{
        { -3.5F, 8.0F, 0.5F },
        {  0.5F, 8.0F, 0.5F },
        {  4.0F, 8.0F, 0.5F },
    }};
    std::vector<cd::physics::BodyHandle> balls;
    balls.reserve(ball_starts.size());
    for (const auto& s : ball_starts)
    {
        cd::physics::BodyDesc bd {};
        bd.type     = cd::physics::BodyType::kDynamic;
        bd.mass     = 1.5F;
        bd.position = s;
        auto h = world->create_body(bd);
        if (h.has_value())
            balls.push_back(*h);
    }

    // Warm-up: simulate balls falling 60 frames.
    std::printf("[hello_raycast] simulating %d warm-up steps for dynamic balls...\n",
                kWarmupSteps);
    for (int i = 0; i < kWarmupSteps; ++i)
    {
        world->step(kDt);
        // Simple floor clamp.
        for (auto& h : balls)
        {
            const auto p = world->position(h);
            const auto v = world->linear_velocity(h);
            if (p.y < kBallRadius)
            {
                world->set_position(h, { p.x, kBallRadius, p.z });
                if (v.y < 0.0F)
                    world->set_linear_velocity(h, { v.x, -v.y * 0.4F, v.z });
            }
        }
    }

    std::printf("  After warm-up, dynamic ball positions:\n");
    for (std::size_t i = 0; i < balls.size(); ++i)
    {
        const auto p = world->position(balls[i]);
        std::printf("    ball[%zu] = (%.3f, %.3f, %.3f)\n",
                    i, static_cast<double>(p.x),
                    static_cast<double>(p.y),
                    static_cast<double>(p.z));
    }
    std::printf("\n");

    // Cast N rays from z = kRayOriginZ, pointing +Z, spread along X.
    std::printf("[hello_raycast] casting %d rays (origin.z=%.0f, dir=(0,0,+1))\n\n",
                kNumRays, static_cast<double>(kRayOriginZ));

    const float x_start = -static_cast<float>(kNumRays / 2) * kRaySpacingX;
    for (int ri = 0; ri < kNumRays; ++ri)
    {
        const float rx = x_start + static_cast<float>(ri) * kRaySpacingX;
        const cd::physics::Ray ray {
            { rx, 1.5F, kRayOriginZ },
            { 0.0F, 0.0F, 1.0F },
        };

        const HitResult hs = cast_vs_statics(ray);
        const HitResult hd = cast_vs_dynamics(ray, balls, *world, kBallRadius);

        // Pick closest.
        const HitResult* closest = nullptr;
        if (hs.hit && hd.hit)
            closest = (hs.t < hd.t) ? &hs : &hd;
        else if (hs.hit)
            closest = &hs;
        else if (hd.hit)
            closest = &hd;

        if (closest)
        {
            std::printf("  ray[%d] origin.x=%+5.2f  HIT  %-16s  t=%7.3f  pt=(%.3f,%.3f,%.3f)\n",
                        ri, static_cast<double>(rx),
                        closest->body_kind,
                        static_cast<double>(closest->t),
                        static_cast<double>(closest->point.x),
                        static_cast<double>(closest->point.y),
                        static_cast<double>(closest->point.z));
        }
        else
        {
            std::printf("  ray[%d] origin.x=%+5.2f  MISS\n",
                        ri, static_cast<double>(rx));
        }
    }

    std::printf("\n[hello_raycast] done — scene bodies: %zu\n", world->body_count());
}

}  // namespace

int main()
{
    run();
    return 0;
}
