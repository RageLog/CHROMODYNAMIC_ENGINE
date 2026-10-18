// =============================================================================
// CHROMODYNAMIC — cd/physics/v3d/PhysicsWorld3D.hpp
// Jolt / Source 2 Rubikon / PhysX 5 grade 3D rigid body simulation world with
// DynamicBvh3D broadphase, analytical + GJK/EPA narrowphase, simulation island
// sleep management, 6-DOF TGS velocity/position solver, and spatial queries.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Vector.hpp>
#include <cd/physics/core/CollisionFilterMatrix.hpp>
#include <cd/physics/core/DynamicBvh3D.hpp>
#include <cd/physics/core/IslandBuilder.hpp>
#include <cd/physics/core/PhysicsTypes.hpp>
#include <cd/physics/core/SpatialAabb3D.hpp>
#include <cd/physics/v3d/ContactManifold3D.hpp>
#include <cd/physics/v3d/Joints3D.hpp>
#include <cd/physics/v3d/Narrowphase3D.hpp>
#include <cd/physics/v3d/RigidBody3D.hpp>
#include <cd/physics/v3d/Shapes3D.hpp>
#include <cd/physics/v3d/SweepCcd3D.hpp>
#include <cd/physics/v3d/TgsContactSolver3D.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace cd::physics::v3d
{

class PhysicsWorld3D
{
public:
    PhysicsWorld3D() = default;

    [[nodiscard]] std::uint32_t create_body(const RigidBody3D& prototype = {})
    {
        RigidBody3D body = prototype;
        if (body.id == 0u || find_body_index(body.id) < bodies_.size())
        {
            body.id = next_body_id_++;
        }
        else
        {
            next_body_id_ = std::max(next_body_id_, body.id + 1u);
        }

        body.recompute_mass_properties();
        const core::SpatialAabb3D aabb = compute_aabb_3d(body.shape, body.transform);
        body.bvh_proxy = bvh_.insert_proxy(body.id, aabb);
        bodies_.push_back(std::move(body));
        return bodies_.back().id;
    }

    bool remove_body(std::uint32_t body_id)
    {
        const std::size_t idx = find_body_index(body_id);
        if (idx >= bodies_.size())
        {
            return false;
        }

        if (bodies_[idx].bvh_proxy != core::DynamicBvh3D::kNullNode)
        {
            bvh_.remove_proxy(bodies_[idx].bvh_proxy);
        }

        bodies_.erase(bodies_.begin() + static_cast<std::ptrdiff_t>(idx));

        std::erase_if(
            manifolds_,
            [body_id](const ContactManifold3D& m) noexcept
            { return m.body_a == body_id || m.body_b == body_id; });

        std::erase_if(
            joints_,
            [body_id](const JointVariant3D& jv) noexcept
            {
                return std::visit(
                    [body_id](const auto& j) noexcept
                    { return j.body_a == body_id || j.body_b == body_id; },
                    jv);
            });

        return true;
    }

    [[nodiscard]] RigidBody3D* get_body(std::uint32_t body_id) noexcept
    {
        for (RigidBody3D& b : bodies_)
        {
            if (b.id == body_id)
            {
                return &b;
            }
        }
        if (body_id < bodies_.size())
        {
            return &bodies_[body_id];
        }
        return nullptr;
    }

    [[nodiscard]] const RigidBody3D* get_body(std::uint32_t body_id) const noexcept
    {
        for (const RigidBody3D& b : bodies_)
        {
            if (b.id == body_id)
            {
                return &b;
            }
        }
        if (body_id < bodies_.size())
        {
            return &bodies_[body_id];
        }
        return nullptr;
    }

    [[nodiscard]] std::span<RigidBody3D> bodies() noexcept
    {
        return bodies_;
    }

    [[nodiscard]] std::span<const RigidBody3D> bodies() const noexcept
    {
        return bodies_;
    }

    [[nodiscard]] std::size_t body_count() const noexcept
    {
        return bodies_.size();
    }

    std::size_t add_joint(const JointVariant3D& joint)
    {
        joints_.push_back(joint);
        return joints_.size() - 1u;
    }

    template <class JointT>
        requires(!std::is_same_v<std::decay_t<JointT>, JointVariant3D>)
    std::size_t add_joint(const JointT& joint)
    {
        joints_.emplace_back(joint);
        return joints_.size() - 1u;
    }

    [[nodiscard]] std::span<const JointVariant3D> joints() const noexcept
    {
        return joints_;
    }

    constexpr void set_gravity(cd::math::Vec3f gravity) noexcept
    {
        gravity_ = gravity;
    }

    [[nodiscard]] constexpr cd::math::Vec3f gravity() const noexcept
    {
        return gravity_;
    }

    [[nodiscard]] constexpr core::CollisionFilterMatrix& filter_matrix() noexcept
    {
        return filter_matrix_;
    }

    [[nodiscard]] constexpr const core::CollisionFilterMatrix& filter_matrix() const noexcept
    {
        return filter_matrix_;
    }

    [[nodiscard]] constexpr core::SleepSettings& sleep_settings() noexcept
    {
        return sleep_settings_;
    }

    [[nodiscard]] constexpr const core::SleepSettings& sleep_settings() const noexcept
    {
        return sleep_settings_;
    }

    void step(float dt, std::uint32_t substeps = 4u, std::uint32_t velocity_iters = 8u)
    {
        if (dt <= 0.0F || bodies_.empty())
        {
            return;
        }

        const std::uint32_t n_sub = std::max(1u, substeps);
        const std::uint32_t n_vel = std::max(1u, velocity_iters);
        const float sub_dt = dt / static_cast<float>(n_sub);

        for (std::uint32_t sub = 0u; sub < n_sub; ++sub)
        {
            // 1. Velocity integration + world inertia refresh
            for (RigidBody3D& b : bodies_)
            {
                b.update_world_inertia_tensor();
                b.integrate_velocities(sub_dt, gravity_);
            }

            // 2. Continuous Collision Detection (CCD) TOI substep clamping
            for (std::size_t i = 0u; i < bodies_.size(); ++i)
            {
                RigidBody3D& ba = bodies_[i];
                if (!ba.is_dynamic() || !ba.awake || !ba.enable_ccd)
                {
                    continue;
                }
                const float disp_len = cd::math::length(ba.linear_velocity) * sub_dt;
                if (disp_len <= 1.0e-4F)
                {
                    continue;
                }

                float min_toi = 1.0F;
                for (std::size_t j = 0u; j < bodies_.size(); ++j)
                {
                    if (i == j)
                    {
                        continue;
                    }
                    const RigidBody3D& bb = bodies_[j];
                    if (bb.is_sensor || !filter_matrix_.should_collide(ba.filter, bb.filter))
                    {
                        continue;
                    }
                    const float toi = sweep_shapes_toi_3d(
                        ba.shape, ba.transform, ba.linear_velocity,
                        bb.shape, bb.transform, bb.linear_velocity, sub_dt);
                    if (toi < min_toi)
                    {
                        min_toi = toi;
                    }
                }

                if (min_toi < 1.0F)
                {
                    // Advance body right to TOI and scale remaining velocity for narrowphase contact resolution.
                    ba.transform.position += ba.linear_velocity * (sub_dt * min_toi);
                    ba.linear_velocity *= (1.0F - min_toi);
                }
            }

            // 3. Synchronize BVH proxies
            sync_bvh_proxies(sub_dt);

            // 4. Broadphase + Narrowphase + Warm-start cache transfer
            prev_manifolds_ = manifolds_;
            manifolds_.clear();

            bvh_.query_all_pairs(
                [this](std::uint32_t id_a, std::uint32_t id_b)
                {
                    if (id_a > id_b)
                    {
                        std::swap(id_a, id_b);
                    }
                    const std::size_t idx_a = find_body_index(id_a);
                    const std::size_t idx_b = find_body_index(id_b);
                    if (idx_a >= bodies_.size() || idx_b >= bodies_.size() || idx_a == idx_b)
                    {
                        return;
                    }

                    RigidBody3D& ba = bodies_[idx_a];
                    RigidBody3D& bb = bodies_[idx_b];

                    if (!ba.is_dynamic() && !bb.is_dynamic())
                    {
                        return;
                    }
                    if (!ba.awake && !bb.awake)
                    {
                        return;
                    }
                    if (!filter_matrix_.should_collide(ba.filter, bb.filter))
                    {
                        return;
                    }

                    ContactManifold3D m = collide_shapes_3d(ba.shape, ba.transform, bb.shape, bb.transform);
                    if (m.empty())
                    {
                        return;
                    }

                    m.body_a = ba.id;
                    m.body_b = bb.id;
                    m.combined_friction = core::combine_friction(ba.material, bb.material);
                    m.combined_restitution = core::combine_restitution(ba.material, bb.material);

                    transfer_warm_start_impulses(m);

                    if (!ba.is_sensor && !bb.is_sensor)
                    {
                        if (ba.is_dynamic() && !ba.awake && bb.awake)
                        {
                            ba.awake = true;
                            ba.sleep_timer = 0.0F;
                        }
                        if (bb.is_dynamic() && !bb.awake && ba.awake)
                        {
                            bb.awake = true;
                            bb.sleep_timer = 0.0F;
                        }
                        manifolds_.push_back(m);
                    }
                });

            // 5. Build simulation islands
            rebuild_islands();

            // 6. Warm-start + iterative 6-DOF TGS velocity and joint constraints
            warm_start_3d(manifolds_, bodies_, solver_cfg_.warm_start_factor);
            for (std::uint32_t iter = 0u; iter < n_vel; ++iter)
            {
                solve_velocity_constraints_3d(manifolds_, bodies_, sub_dt, solver_cfg_);
                for (JointVariant3D& joint : joints_)
                {
                    solve_joint_3d(joint, bodies_, sub_dt);
                }
            }

            // 7. Position integration
            for (RigidBody3D& b : bodies_)
            {
                b.integrate_positions(sub_dt);
            }

            // 8. Position error projection (Baumgarte / NGS)
            solve_position_constraints_3d(manifolds_, bodies_, solver_cfg_);

            // 9. Post-step BVH update & island sleep evaluation
            sync_bvh_proxies(0.0F);
            update_sleeping(sub_dt);
        }
    }

    [[nodiscard]] RaycastHit3D raycast(
        cd::math::Vec3f origin,
        cd::math::Vec3f dir,
        float max_dist = 1000.0F,
        core::CollisionFilter query_filter = {}) const noexcept
    {
        RaycastHit3D best {};
        best.fraction = 1.0F;

        for (const RigidBody3D& b : bodies_)
        {
            if (!filter_matrix_.should_collide(query_filter, b.filter))
            {
                continue;
            }
            const RaycastHit3D hit = raycast_shape_3d(origin, dir, max_dist, b.shape, b.transform);
            if (hit.hit && hit.fraction < best.fraction)
            {
                best = hit;
                best.body_id = b.id;
            }
        }
        return best;
    }

    [[nodiscard]] RaycastHit3D shapecast(
        const ShapeVariant3D& cast_shape,
        const core::Transform3D& start_xf,
        cd::math::Vec3f dir,
        float max_dist = 1000.0F,
        core::CollisionFilter query_filter = {},
        std::uint32_t ignore_body_id = 0u) const noexcept
    {
        RaycastHit3D best {};
        best.fraction = 1.0F;

        const float dir_len = cd::math::length(dir);
        if (dir_len <= 1.0e-8F || max_dist <= 0.0F)
        {
            return best;
        }
        const cd::math::Vec3f unit_dir = dir / dir_len;
        const float total_dist = max_dist * dir_len;

        for (const RigidBody3D& b : bodies_)
        {
            if (b.id == ignore_body_id || b.is_sensor || !filter_matrix_.should_collide(query_filter, b.filter))
            {
                continue;
            }
            const float toi = sweep_shapes_toi_3d(
                cast_shape, start_xf, unit_dir,
                b.shape, b.transform, { 0.0F, 0.0F, 0.0F }, total_dist);
            if (toi < best.fraction)
            {
                best.hit = true;
                best.body_id = b.id;
                best.fraction = toi;

                core::Transform3D hit_xf = start_xf;
                hit_xf.position += unit_dir * (total_dist * std::min(1.0F, toi + 0.005F));
                const ContactManifold3D m = collide_shapes_3d(cast_shape, hit_xf, b.shape, b.transform);
                if (!m.empty())
                {
                    best.normal = -m.normal;
                    best.point = m.points[0].world_point;
                }
                else
                {
                    best.normal = cd::math::normalize(hit_xf.position - b.transform.position);
                    best.point = start_xf.position + unit_dir * (total_dist * toi);
                }
            }
        }
        return best;
    }

    [[nodiscard]] std::vector<std::uint32_t> query_aabb(const core::SpatialAabb3D& aabb) const
    {
        std::vector<std::uint32_t> hits;
        bvh_.query_aabb(
            aabb,
            [this, &aabb, &hits](std::uint32_t body_id)
            {
                const RigidBody3D* b = get_body(body_id);
                if (b != nullptr && compute_aabb_3d(b->shape, b->transform).overlaps(aabb))
                {
                    hits.push_back(b->id);
                }
            });
        return hits;
    }

    template <class Callback>
    void query_aabb(const core::SpatialAabb3D& aabb, Callback&& cb) const
    {
        bvh_.query_aabb(
            aabb,
            [this, &aabb, &cb](std::uint32_t body_id)
            {
                const RigidBody3D* b = get_body(body_id);
                if (b != nullptr && compute_aabb_3d(b->shape, b->transform).overlaps(aabb))
                {
                    cb(b->id);
                }
            });
    }

    [[nodiscard]] std::span<const ContactManifold3D> active_manifolds() const noexcept
    {
        return manifolds_;
    }

    [[nodiscard]] std::size_t island_count() const noexcept
    {
        return islands_.size();
    }

private:
    [[nodiscard]] std::size_t find_body_index(std::uint32_t body_id) const noexcept
    {
        for (std::size_t i = 0u; i < bodies_.size(); ++i)
        {
            if (bodies_[i].id == body_id)
            {
                return i;
            }
        }
        return bodies_.size();
    }

    void sync_bvh_proxies(float dt)
    {
        for (RigidBody3D& b : bodies_)
        {
            const core::SpatialAabb3D aabb = compute_aabb_3d(b.shape, b.transform);
            if (b.bvh_proxy == core::DynamicBvh3D::kNullNode)
            {
                b.bvh_proxy = bvh_.insert_proxy(b.id, aabb);
            }
            else
            {
                (void)bvh_.move_proxy(b.bvh_proxy, aabb, b.linear_velocity * dt);
            }
        }
    }

    void transfer_warm_start_impulses(ContactManifold3D& m) const noexcept
    {
        for (const ContactManifold3D& prev : prev_manifolds_)
        {
            if (prev.body_a != m.body_a || prev.body_b != m.body_b)
            {
                continue;
            }
            for (std::uint8_t i = 0u; i < m.point_count; ++i)
            {
                ContactPoint3D& cp = m.points[i];
                for (std::uint8_t j = 0u; j < prev.point_count; ++j)
                {
                    const ContactPoint3D& pcp = prev.points[j];
                    if (cp.feature_key == pcp.feature_key ||
                        cd::math::length_squared(cp.world_point - pcp.world_point) < 0.0025F)
                    {
                        cp.normal_impulse = pcp.normal_impulse;
                        cp.tangent_impulse_1 = pcp.tangent_impulse_1;
                        cp.tangent_impulse_2 = pcp.tangent_impulse_2;
                        break;
                    }
                }
            }
            break;
        }
    }

    void rebuild_islands()
    {
        const auto n = static_cast<std::uint32_t>(bodies_.size());
        island_builder_.reset(n);
        for (std::uint32_t i = 0u; i < n; ++i)
        {
            if (!bodies_[i].is_dynamic())
            {
                island_builder_.mark_static(i);
            }
        }

        for (const ContactManifold3D& m : manifolds_)
        {
            const std::size_t ia = find_body_index(m.body_a);
            const std::size_t ib = find_body_index(m.body_b);
            if (ia < bodies_.size() && ib < bodies_.size())
            {
                island_builder_.unite(static_cast<std::uint32_t>(ia), static_cast<std::uint32_t>(ib));
            }
        }

        for (const JointVariant3D& jv : joints_)
        {
            std::visit(
                [this](const auto& j) noexcept
                {
                    const std::size_t ia = find_body_index(j.body_a);
                    const std::size_t ib = find_body_index(j.body_b);
                    if (ia < bodies_.size() && ib < bodies_.size())
                    {
                        island_builder_.unite(static_cast<std::uint32_t>(ia), static_cast<std::uint32_t>(ib));
                    }
                },
                jv);
        }

        islands_ = island_builder_.build_islands();
    }

    void update_sleeping(float dt) noexcept
    {
        if (!sleep_settings_.allow_sleep)
        {
            return;
        }

        const float lin_sq = sleep_settings_.linear_sleep_threshold * sleep_settings_.linear_sleep_threshold;
        const float ang_sq = sleep_settings_.angular_sleep_threshold * sleep_settings_.angular_sleep_threshold;

        for (const core::SimulationIsland& island : islands_)
        {
            float min_timer = sleep_settings_.time_to_sleep_sec + 1.0F;
            for (const std::uint32_t idx : island.body_indices)
            {
                if (idx >= bodies_.size())
                {
                    continue;
                }
                RigidBody3D& b = bodies_[idx];
                if (!b.is_dynamic())
                {
                    continue;
                }
                if (cd::math::length_squared(b.linear_velocity) > lin_sq ||
                    cd::math::length_squared(b.angular_velocity) > ang_sq)
                {
                    b.sleep_timer = 0.0F;
                    min_timer = 0.0F;
                }
                else
                {
                    b.sleep_timer += dt;
                    min_timer = std::min(min_timer, b.sleep_timer);
                }
            }

            if (min_timer >= sleep_settings_.time_to_sleep_sec)
            {
                for (const std::uint32_t idx : island.body_indices)
                {
                    if (idx < bodies_.size() && bodies_[idx].is_dynamic())
                    {
                        bodies_[idx].awake = false;
                        bodies_[idx].linear_velocity = {};
                        bodies_[idx].angular_velocity = {};
                    }
                }
            }
        }
    }

    std::vector<RigidBody3D> bodies_ {};
    std::vector<ContactManifold3D> manifolds_ {};
    std::vector<ContactManifold3D> prev_manifolds_ {};
    std::vector<JointVariant3D> joints_ {};
    std::vector<core::SimulationIsland> islands_ {};
    core::DynamicBvh3D bvh_ {};
    core::IslandBuilder island_builder_ {};
    core::CollisionFilterMatrix filter_matrix_ {};
    core::SleepSettings sleep_settings_ {};
    TgsSolverConfig3D solver_cfg_ {};
    cd::math::Vec3f gravity_ { 0.0F, -9.81F, 0.0F };
    std::uint32_t next_body_id_ { 1u };
};

}  // namespace cd::physics::v3d
