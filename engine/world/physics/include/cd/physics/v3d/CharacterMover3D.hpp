// =============================================================================
// CHROMODYNAMIC — cd/physics/v3d/CharacterMover3D.hpp
// Jolt CharacterVirtual-style 3D kinematic character controller with
// iterative collide-and-slide, walkable slope angle limit, automatic stair
// step-up, and downward ground snapping.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Constants.hpp>
#include <cd/math/Vector.hpp>
#include <cd/physics/v3d/Narrowphase3D.hpp>
#include <cd/physics/v3d/PhysicsWorld3D.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace cd::physics::v3d
{

class CharacterMover3D
{
public:
    float max_slope_deg { 45.0F };
    float step_up_height { 0.35F };
    float ground_snap_dist { 0.15F };
    float skin_width { 0.005F };
    std::uint32_t max_slide_iterations { 4u };
    cd::math::Vec3f up_vector { 0.0F, 1.0F, 0.0F };

    CharacterMover3D() = default;

    void move_and_slide(
        PhysicsWorld3D& world,
        std::uint32_t body_id,
        cd::math::Vec3f desired_velocity,
        float dt) noexcept
    {
        RigidBody3D* body = world.get_body(body_id);
        if (body == nullptr || dt <= 0.0F)
        {
            return;
        }

        const cd::math::Vec3f start_pos = body->transform.position;
        const float max_slope_rad = max_slope_deg * (3.14159265358979323846F / 180.0F);
        const float min_walkable_cos = std::cos(max_slope_rad);
        const cd::math::Vec3f up = cd::math::normalize(up_vector);

        on_ground_ = false;
        ground_normal_ = up;
        ground_body_id_ = 0u;

        // 1. Initial depenetration + ground contact check at starting pose
        resolve_overlaps(world, *body, up, min_walkable_cos);

        // 2. Iterative collide-and-slide with stair step-up
        cd::math::Vec3f remaining = desired_velocity * dt;

        for (std::uint32_t iter = 0u; iter < max_slide_iterations; ++iter)
        {
            const float dist = cd::math::length(remaining);
            if (dist <= 1.0e-5F)
            {
                break;
            }

            const cd::math::Vec3f dir = remaining / dist;
            const RaycastHit3D hit = world.shapecast(
                body->shape, body->transform, dir, dist, body->filter, body->id);

            if (!hit.hit)
            {
                body->transform.position += remaining;
                remaining = {};
                break;
            }

            const float safe_advance = std::max(0.0F, hit.fraction * dist - skin_width);
            body->transform.position += dir * safe_advance;
            remaining *= (1.0F - hit.fraction);

            cd::math::Vec3f n = hit.normal;
            if (cd::math::dot(n, dir) > 0.0F)
            {
                n = -n;
            }

            const float cos_angle = cd::math::dot(n, up);
            if (cos_angle >= min_walkable_cos)
            {
                on_ground_ = true;
                ground_normal_ = n;
                ground_body_id_ = hit.body_id;
            }
            else if (step_up_height > 0.0F)
            {
                // Attempt stair step-up over low vertical obstacle
                cd::math::Vec3f horiz_rem = remaining - up * cd::math::dot(remaining, up);
                const float horiz_len = cd::math::length(horiz_rem);
                if (horiz_len > 1.0e-4F)
                {
                    core::Transform3D step_xf = body->transform;
                    step_xf.position += up * step_up_height;

                    const RaycastHit3D fwd_hit = world.shapecast(
                        body->shape, step_xf, horiz_rem / horiz_len, horiz_len, body->filter, body->id);

                    if (!fwd_hit.hit || fwd_hit.fraction > 0.25F)
                    {
                        const float fwd_dist = fwd_hit.hit
                                                   ? std::max(0.0F, fwd_hit.fraction * horiz_len - skin_width)
                                                   : horiz_len;
                        step_xf.position += (horiz_rem / horiz_len) * fwd_dist;

                        const RaycastHit3D down_hit = world.shapecast(
                            body->shape, step_xf, -up, step_up_height + ground_snap_dist, body->filter, body->id);

                        if (down_hit.hit)
                        {
                            cd::math::Vec3f dn = down_hit.normal;
                            if (cd::math::dot(dn, up) < 0.0F)
                            {
                                dn = -dn;
                            }
                            if (cd::math::dot(dn, up) >= min_walkable_cos)
                            {
                                const float drop = std::max(
                                    0.0F,
                                    down_hit.fraction * (step_up_height + ground_snap_dist) - skin_width);
                                step_xf.position -= up * drop;
                                body->transform.position = step_xf.position;
                                on_ground_ = true;
                                ground_normal_ = dn;
                                ground_body_id_ = down_hit.body_id;
                                remaining = {};
                                break;
                            }
                        }
                    }
                }
            }

            // Project remaining motion onto the collision tangent plane
            const float into_surface = cd::math::dot(remaining, n);
            if (into_surface < 0.0F)
            {
                remaining -= n * into_surface;
            }
        }

        // 3. Final depenetration pass
        resolve_overlaps(world, *body, up, min_walkable_cos);

        // 4. Downward ground probe & snap when not jumping upward
        if (!on_ground_ && cd::math::dot(desired_velocity, up) <= 0.5F && ground_snap_dist > 0.0F)
        {
            const RaycastHit3D ground_hit = world.shapecast(
                body->shape, body->transform, -up, ground_snap_dist, body->filter, body->id);
            if (ground_hit.hit)
            {
                cd::math::Vec3f gn = ground_hit.normal;
                if (cd::math::dot(gn, up) < 0.0F)
                {
                    gn = -gn;
                }
                if (cd::math::dot(gn, up) >= min_walkable_cos)
                {
                    const float snap_drop = std::max(0.0F, ground_hit.fraction * ground_snap_dist - skin_width);
                    body->transform.position -= up * snap_drop;
                    on_ground_ = true;
                    ground_normal_ = gn;
                    ground_body_id_ = ground_hit.body_id;
                }
            }
        }

        body->linear_velocity = (body->transform.position - start_pos) * (1.0F / dt);
    }

    [[nodiscard]] constexpr bool on_ground() const noexcept
    {
        return on_ground_;
    }

    [[nodiscard]] constexpr cd::math::Vec3f ground_normal() const noexcept
    {
        return ground_normal_;
    }

    [[nodiscard]] constexpr std::uint32_t ground_body_id() const noexcept
    {
        return ground_body_id_;
    }

private:
    void resolve_overlaps(
        PhysicsWorld3D& world,
        RigidBody3D& body,
        cd::math::Vec3f up,
        float min_walkable_cos) noexcept
    {
        for (const RigidBody3D& other : world.bodies())
        {
            if (other.id == body.id || other.is_sensor ||
                !world.filter_matrix().should_collide(body.filter, other.filter))
            {
                continue;
            }

            const ContactManifold3D m = collide_shapes_3d(
                body.shape, body.transform, other.shape, other.transform);
            if (m.empty())
            {
                continue;
            }

            // m.normal points from body (A) to other (B); push body along -m.normal.
            const cd::math::Vec3f push_out = -m.normal;
            float max_pen = 0.0F;
            for (std::uint8_t i = 0u; i < m.point_count; ++i)
            {
                max_pen = std::max(max_pen, m.points[i].penetration);
            }
            if (max_pen > 0.0F)
            {
                body.transform.position += push_out * max_pen;
            }

            if (cd::math::dot(push_out, up) >= min_walkable_cos)
            {
                on_ground_ = true;
                ground_normal_ = push_out;
                ground_body_id_ = other.id;
            }
        }
    }

    bool on_ground_ { false };
    cd::math::Vec3f ground_normal_ { 0.0F, 1.0F, 0.0F };
    std::uint32_t ground_body_id_ { 0u };
};

}  // namespace cd::physics::v3d
