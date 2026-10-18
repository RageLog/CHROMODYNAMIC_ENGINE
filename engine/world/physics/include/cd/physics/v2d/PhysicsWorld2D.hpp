// =============================================================================
// CHROMODYNAMIC — cd/physics/v2d/PhysicsWorld2D.hpp
// Box2D v3 / Godot 4 grade 2D physics world orchestrating DynamicBvh2D
// broadphase, analytical + SAT narrowphase, IslandBuilder, Soft-Step TGS
// sub-stepping solver, joints, and anti-tunneling CCD.
// =============================================================================
#pragma once

#include <cd/physics/core/CollisionFilterMatrix.hpp>
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsign-conversion"
#endif
#include <cd/physics/core/DynamicBvh2D.hpp>
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
#include <cd/physics/core/IslandBuilder.hpp>
#include <cd/physics/core/PhysicsTypes.hpp>
#include <cd/physics/core/SpatialAabb2D.hpp>
#include <cd/physics/v2d/ContactManifold2D.hpp>
#include <cd/physics/v2d/Joints2D.hpp>
#include <cd/physics/v2d/Narrowphase2D.hpp>
#include <cd/physics/v2d/RigidBody2D.hpp>
#include <cd/physics/v2d/Shapes2D.hpp>
#include <cd/physics/v2d/SoftStepSolver2D.hpp>
#include <cd/physics/v2d/SweepCcd2D.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace cd::physics::v2d
{

class PhysicsWorld2D
{
public:
    PhysicsWorld2D() = default;

    void set_gravity(cd::math::Vec2f gravity) noexcept
    {
        gravity_ = gravity;
    }

    [[nodiscard]] constexpr cd::math::Vec2f gravity() const noexcept
    {
        return gravity_;
    }

    void set_sleep_settings(const core::SleepSettings& settings) noexcept
    {
        sleep_settings_ = settings;
    }

    [[nodiscard]] constexpr const core::SleepSettings& sleep_settings() const noexcept
    {
        return sleep_settings_;
    }

    [[nodiscard]] core::CollisionFilterMatrix& filter_matrix() noexcept
    {
        return filter_matrix_;
    }

    [[nodiscard]] const core::CollisionFilterMatrix& filter_matrix() const noexcept
    {
        return filter_matrix_;
    }

    [[nodiscard]] std::uint32_t create_body(RigidBody2D body)
    {
        if (body.id == 0u || get_body(body.id) != nullptr)
        {
            body.id = next_body_id_++;
        }
        else
        {
            next_body_id_ = std::max(next_body_id_, body.id + 1u);
        }

        if (!body.is_dynamic())
        {
            body.mass        = 0.0F;
            body.inv_mass    = 0.0F;
            body.inertia     = 0.0F;
            body.inv_inertia = 0.0F;
        }
        else if (body.mass <= 0.0F || body.inv_mass <= 0.0F || body.inertia <= 0.0F || body.inv_inertia <= 0.0F)
        {
            body.recompute_mass_properties();
        }

        const core::SpatialAabb2D aabb = compute_aabb_2d(body.shape, body.transform);
        body.bvh_proxy                 = bvh_.insert_proxy(body.id, aabb);
        const std::uint32_t id         = body.id;
        bodies_.push_back(std::move(body));
        return id;
    }

    [[nodiscard]] std::uint32_t create_body(
        core::BodyMotionType motion_type,
        const core::Transform2D& transform,
        const ShapeVariant2D& shape,
        const core::PhysicsMaterial& material = {})
    {
        RigidBody2D body {};
        body.motion_type = motion_type;
        body.transform   = transform;
        body.shape       = shape;
        body.material    = material;
        body.recompute_mass_properties();
        return create_body(std::move(body));
    }

    bool remove_body(std::uint32_t body_id)
    {
        for (auto it = bodies_.begin(); it != bodies_.end(); ++it)
        {
            if (it->id == body_id)
            {
                if (it->bvh_proxy != core::DynamicBvh2D::kNullNode)
                {
                    bvh_.remove_proxy(it->bvh_proxy);
                }
                bodies_.erase(it);
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] RigidBody2D* get_body(std::uint32_t body_id) noexcept
    {
        for (auto& b : bodies_)
        {
            if (b.id == body_id)
            {
                return &b;
            }
        }
        return nullptr;
    }

    [[nodiscard]] const RigidBody2D* get_body(std::uint32_t body_id) const noexcept
    {
        for (const auto& b : bodies_)
        {
            if (b.id == body_id)
            {
                return &b;
            }
        }
        return nullptr;
    }

    [[nodiscard]] std::span<RigidBody2D> bodies() noexcept
    {
        return bodies_;
    }

    [[nodiscard]] std::span<const RigidBody2D> bodies() const noexcept
    {
        return bodies_;
    }

    [[nodiscard]] std::size_t body_count() const noexcept
    {
        return bodies_.size();
    }

    std::size_t add_joint(const JointVariant2D& joint)
    {
        joints_.push_back(joint);
        return joints_.size() - 1u;
    }

    [[nodiscard]] std::span<JointVariant2D> joints() noexcept
    {
        return joints_;
    }

    [[nodiscard]] std::span<const JointVariant2D> joints() const noexcept
    {
        return joints_;
    }

    void step(float dt, std::uint32_t substeps = 4u)
    {
        if (dt <= 0.0F || bodies_.empty())
        {
            return;
        }

        const std::uint32_t num_substeps = std::max(1u, substeps);
        const float sub_dt               = dt / static_cast<float>(num_substeps);

        for (std::uint32_t s = 0u; s < num_substeps; ++s)
        {
            // 1. Integrate velocities
            for (auto& b : bodies_)
            {
                b.integrate_velocities(sub_dt, gravity_);
            }

            // 2. Anti-tunneling CCD TOI clamping for fast bodies
            resolve_ccd_substep(sub_dt);

            // 3. Synchronize broadphase BVH proxies & detect contacts
            update_bvh_proxies(sub_dt);
            detect_contacts();

            // 4. Build simulation islands
            build_simulation_islands();

            // 5. Warm-start + velocity/joint constraints
            warm_start(active_manifolds_, bodies_);
            for (int iter = 0; iter < 4; ++iter)
            {
                for (auto& j : joints_)
                {
                    solve_joint_2d(j, bodies_, sub_dt);
                }
                solve_velocity_constraints(active_manifolds_, bodies_, sub_dt, 30.0F, 1.0F);
            }

            // 6. Integrate positions
            for (auto& b : bodies_)
            {
                b.integrate_positions(sub_dt);
            }

            // 7. Refresh manifolds at new positions & solve position constraints
            update_bvh_proxies(0.0F);
            detect_contacts();
            for (int iter = 0; iter < 2; ++iter)
            {
                if (solve_position_constraints(active_manifolds_, bodies_))
                {
                    break;
                }
            }

            prev_manifolds_ = active_manifolds_;
        }

        // Update sleep states
        update_sleep_states(dt);
    }

    [[nodiscard]] RaycastHit2D raycast(
        cd::math::Vec2f origin,
        cd::math::Vec2f dir,
        float max_dist,
        const core::CollisionFilter& query_filter = {}) const noexcept
    {
        RaycastHit2D best {};
        best.fraction = 1.0F;

        for (const auto& b : bodies_)
        {
            if (!filter_matrix_.should_collide(query_filter, b.filter))
            {
                continue;
            }
            const RaycastHit2D cand = raycast_shape_2d(origin, dir, max_dist, b.shape, b.transform);
            if (cand.hit && (!best.hit || cand.fraction < best.fraction))
            {
                best         = cand;
                best.body_id = b.id;
            }
        }
        return best;
    }

    [[nodiscard]] std::vector<std::uint32_t> query_aabb(const core::SpatialAabb2D& aabb) const
    {
        std::vector<std::uint32_t> result;
        bvh_.query_aabb(
            aabb,
            [&](std::uint32_t body_id)
            {
                const RigidBody2D* b = get_body(body_id);
                if (b != nullptr && compute_aabb_2d(b->shape, b->transform).overlaps(aabb))
                {
                    result.push_back(body_id);
                }
                return true;
            });
        std::sort(result.begin(), result.end());
        result.erase(std::unique(result.begin(), result.end()), result.end());
        return result;
    }

    template <typename Callback>
    void query_aabb(const core::SpatialAabb2D& aabb, Callback&& cb) const
    {
        const auto ids = query_aabb(aabb);
        for (const std::uint32_t id : ids)
        {
            cb(id);
        }
    }

    [[nodiscard]] std::span<const ContactManifold2D> active_manifolds() const noexcept
    {
        return active_manifolds_;
    }

    [[nodiscard]] std::uint32_t island_count() const noexcept
    {
        return island_count_;
    }

private:
    [[nodiscard]] std::uint32_t body_index_by_id(std::uint32_t body_id) const noexcept
    {
        for (std::uint32_t i = 0u; i < static_cast<std::uint32_t>(bodies_.size()); ++i)
        {
            if (bodies_[i].id == body_id)
            {
                return i;
            }
        }
        return core::IslandBuilder::kInvalidIndex;
    }

    void update_bvh_proxies(float dt)
    {
        for (auto& b : bodies_)
        {
            const core::SpatialAabb2D aabb = compute_aabb_2d(b.shape, b.transform);
            if (b.bvh_proxy == core::DynamicBvh2D::kNullNode)
            {
                b.bvh_proxy = bvh_.insert_proxy(b.id, aabb);
            }
            else
            {
                bvh_.move_proxy(b.bvh_proxy, aabb, b.linear_velocity * dt);
            }
        }
    }

    void resolve_ccd_substep(float sub_dt)
    {
        for (std::size_t i = 0u; i < bodies_.size(); ++i)
        {
            RigidBody2D& a = bodies_[i];
            if (!a.is_dynamic() || !a.enable_ccd || !a.awake || a.is_sensor)
            {
                continue;
            }
            const cd::math::Vec2f disp_a = a.linear_velocity * sub_dt;
            if (cd::math::length_sq_v2(disp_a) <= 1.0e-6F)
            {
                continue;
            }

            float min_toi            = 1.0F;
            const RigidBody2D* hit_b = nullptr;

            for (std::size_t j = 0u; j < bodies_.size(); ++j)
            {
                if (i == j)
                {
                    continue;
                }
                const RigidBody2D& b = bodies_[j];
                if (b.is_sensor || !filter_matrix_.should_collide(a.filter, b.filter))
                {
                    continue;
                }
                const cd::math::Vec2f disp_b = b.is_dynamic() ? (b.linear_velocity * sub_dt) : cd::math::Vec2f { 0.0F, 0.0F };
                const float toi = sweep_shapes_toi_2d(a.shape, a.transform, disp_a, b.shape, b.transform, disp_b);
                if (toi < min_toi)
                {
                    min_toi = toi;
                    hit_b   = &b;
                }
            }

            if (min_toi < 1.0F && hit_b != nullptr)
            {
                // Advance right up to impact point and project velocity so subsequent position integration stays at surface
                a.transform.position += disp_a * min_toi;
                const cd::math::Vec2f d_ab = cd::math::normalize_v2(hit_b->transform.position - a.transform.position);
                const float vn             = cd::math::dot_v2(a.linear_velocity, d_ab);
                if (vn > 0.0F)
                {
                    const float e = core::combine_restitution(a.material, hit_b->material);
                    a.linear_velocity -= d_ab * ((1.0F + e) * vn);
                }
            }
        }
    }

    void detect_contacts()
    {
        active_manifolds_.clear();
        const std::size_t n = bodies_.size();
        for (std::size_t i = 0u; i < n; ++i)
        {
            const RigidBody2D& a = bodies_[i];
            const core::SpatialAabb2D aabb_a = compute_aabb_2d(a.shape, a.transform);
            for (std::size_t j = i + 1u; j < n; ++j)
            {
                const RigidBody2D& b = bodies_[j];
                if (!a.is_dynamic() && !b.is_dynamic())
                {
                    continue;
                }
                if (!filter_matrix_.should_collide(a.filter, b.filter))
                {
                    continue;
                }
                const core::SpatialAabb2D aabb_b = compute_aabb_2d(b.shape, b.transform);
                if (!aabb_a.overlaps(aabb_b))
                {
                    continue;
                }

                ContactManifold2D m = collide_shapes_2d(a.shape, a.transform, b.shape, b.transform);
                if (!m.has_contacts())
                {
                    continue;
                }

                m.body_a               = a.id;
                m.body_b               = b.id;
                m.combined_friction    = core::combine_friction(a.material, b.material);
                m.combined_restitution = core::combine_restitution(a.material, b.material);

                // Match cached impulses from previous substep for warm-starting
                for (const auto& prev : prev_manifolds_)
                {
                    if (prev.body_a == m.body_a && prev.body_b == m.body_b)
                    {
                        for (std::uint8_t k = 0u; k < m.point_count; ++k)
                        {
                            for (std::uint8_t pk = 0u; pk < prev.point_count; ++pk)
                            {
                                if (m.points.at(k).feature_key == prev.points.at(pk).feature_key)
                                {
                                    m.points.at(k).normal_impulse  = prev.points.at(pk).normal_impulse;
                                    m.points.at(k).tangent_impulse = prev.points.at(pk).tangent_impulse;
                                    break;
                                }
                            }
                        }
                        break;
                    }
                }

                active_manifolds_.push_back(m);
            }
        }
    }

    void build_simulation_islands()
    {
        const auto count = static_cast<std::uint32_t>(bodies_.size());
        island_builder_.reset(count);
        for (std::uint32_t i = 0u; i < count; ++i)
        {
            if (!bodies_[i].is_dynamic())
            {
                island_builder_.mark_static(i);
            }
        }

        for (const auto& m : active_manifolds_)
        {
            const std::uint32_t ia = body_index_by_id(m.body_a);
            const std::uint32_t ib = body_index_by_id(m.body_b);
            if (ia != core::IslandBuilder::kInvalidIndex && ib != core::IslandBuilder::kInvalidIndex)
            {
                island_builder_.unite(ia, ib);
            }
        }

        for (const auto& jv : joints_)
        {
            std::visit(
                [&](const auto& j) noexcept
                {
                    const std::uint32_t ia = body_index_by_id(j.body_a);
                    const std::uint32_t ib = body_index_by_id(j.body_b);
                    if (ia != core::IslandBuilder::kInvalidIndex && ib != core::IslandBuilder::kInvalidIndex)
                    {
                        island_builder_.unite(ia, ib);
                    }
                },
                jv);
        }

        const auto islands = island_builder_.build_islands();
        island_count_      = static_cast<std::uint32_t>(islands.size());
    }

    void update_sleep_states(float dt) noexcept
    {
        if (!sleep_settings_.allow_sleep)
        {
            return;
        }
        const float lin_sq = sleep_settings_.linear_sleep_threshold * sleep_settings_.linear_sleep_threshold;
        const float ang_th = sleep_settings_.angular_sleep_threshold;

        for (auto& b : bodies_)
        {
            if (!b.is_dynamic() || !b.awake)
            {
                continue;
            }
            if (cd::math::length_sq_v2(b.linear_velocity) <= lin_sq &&
                std::abs(b.angular_velocity) <= ang_th)
            {
                b.sleep_timer += dt;
                if (b.sleep_timer >= sleep_settings_.time_to_sleep_sec)
                {
                    b.awake            = false;
                    b.linear_velocity  = { 0.0F, 0.0F };
                    b.angular_velocity = 0.0F;
                }
            }
            else
            {
                b.sleep_timer = 0.0F;
            }
        }
    }

    cd::math::Vec2f gravity_ { 0.0F, -9.81F };
    core::SleepSettings sleep_settings_ {};
    core::CollisionFilterMatrix filter_matrix_ {};
    core::DynamicBvh2D bvh_ {};
    core::IslandBuilder island_builder_ {};
    std::vector<RigidBody2D> bodies_ {};
    std::vector<JointVariant2D> joints_ {};
    std::vector<ContactManifold2D> active_manifolds_ {};
    std::vector<ContactManifold2D> prev_manifolds_ {};
    std::uint32_t next_body_id_ { 1u };
    std::uint32_t island_count_ { 0u };
};

}  // namespace cd::physics::v2d
