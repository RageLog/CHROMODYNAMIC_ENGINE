// =============================================================================
// CHROMODYNAMIC — cd/physics/rockstar/XpbdClothSoftBody3D.hpp
// Macklin et al. (2016) XPBD (Extended Position-Based Dynamics) Cloth, Rope &
// Pressurized Soft-Body Solver.
//
// Features:
//   * Time-step invariant compliance: alpha_tilde = alpha / (dt * dt) with
//     Lagrange multiplier accumulation.
//   * Distance (stretch/shear) constraints & isometric bending constraints.
//   * Closed-mesh tetrahedral signed volume preservation & balloon overpressure
//     inflation (`rest_volume`, `overpressure`).
//   * Triangle-level aerodynamic wind drag & lift forces.
//   * Sphere and capsule analytical collision projection with Coulomb friction.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Functions.hpp>
#include <cd/math/Vector.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace cd::physics::rockstar
{

struct XpbdParticle3D
{
    cd::math::Vec3f position      {};
    cd::math::Vec3f prev_position {};
    cd::math::Vec3f velocity      {};
    float           inv_mass      { 1.0F };
};

struct XpbdDistanceConstraint3D
{
    std::uint32_t p0          { 0u };
    std::uint32_t p1          { 0u };
    float         rest_length { 1.0F };
    float         compliance  { 1e-6F };
    float         lambda      { 0.0F };
};

struct XpbdBendingConstraint3D
{
    std::uint32_t p0          { 0u };
    std::uint32_t p1          { 0u };
    float         rest_length { 1.0F };
    float         compliance  { 1e-3F };
    float         lambda      { 0.0F };
};

struct XpbdTriangleFace3D
{
    std::uint32_t i0 { 0u };
    std::uint32_t i1 { 0u };
    std::uint32_t i2 { 0u };
};

struct XpbdSphereCollider3D
{
    cd::math::Vec3f center   {};
    float           radius   { 0.5F };
    float           friction { 0.25F };
};

struct XpbdCapsuleCollider3D
{
    cd::math::Vec3f point_a  {};
    cd::math::Vec3f point_b  { 0.0F, 1.0F, 0.0F };
    float           radius   { 0.25F };
    float           friction { 0.25F };
};

class XpbdClothSoftBodySolver3D
{
public:
    XpbdClothSoftBodySolver3D() = default;

    /// Computes time-step normalized XPBD compliance: alpha_tilde = alpha / dt^2.
    [[nodiscard]] static constexpr float compute_alpha_tilde(float compliance_alpha, float dt) noexcept
    {
        const float safe_dt = dt > 1e-6F ? dt : 1e-6F;
        return std::max(0.0F, compliance_alpha) / (safe_dt * safe_dt);
    }

    /// Initializes a 2D cloth grid of (cols x rows) particles in the X-Z or X-Y plane,
    /// complete with structural/shear distance constraints, 2-ring isometric bending
    /// constraints, and aerodynamic triangle faces.
    void create_cloth_grid(
        std::uint32_t cols,
        std::uint32_t rows,
        float spacing = 0.1F,
        cd::math::Vec3f origin = { 0.0F, 2.0F, 0.0F },
        float particle_mass_kg = 0.08F,
        float distance_compliance = 1e-6F,
        float bending_compliance = 2e-3F,
        bool pin_top_corners = true)
    {
        clear();
        if (cols < 2u || rows < 2u)
        {
            return;
        }

        const float inv_m = particle_mass_kg > 1e-6F ? (1.0F / particle_mass_kg) : 0.0F;
        particles_.reserve(static_cast<std::size_t>(cols) * rows);

        auto idx = [cols](std::uint32_t c, std::uint32_t r) noexcept -> std::uint32_t
        {
            return r * cols + c;
        };

        for (std::uint32_t r = 0u; r < rows; ++r)
        {
            for (std::uint32_t c = 0u; c < cols; ++c)
            {
                const cd::math::Vec3f pos {
                    origin.x + static_cast<float>(c) * spacing,
                    origin.y - static_cast<float>(r) * spacing,
                    origin.z
                };
                const bool pinned = pin_top_corners && (r == 0u) && (c == 0u || c == cols - 1u);
                particles_.push_back(XpbdParticle3D { pos, pos, {}, pinned ? 0.0F : inv_m });
            }
        }

        for (std::uint32_t r = 0u; r < rows; ++r)
        {
            for (std::uint32_t c = 0u; c < cols; ++c)
            {
                const std::uint32_t p = idx(c, r);
                // Structural stretch constraints
                if (c + 1u < cols)
                {
                    add_distance_constraint(p, idx(c + 1u, r), distance_compliance);
                }
                if (r + 1u < rows)
                {
                    add_distance_constraint(p, idx(c, r + 1u), distance_compliance);
                }
                // Shear constraints + triangle faces
                if (c + 1u < cols && r + 1u < rows)
                {
                    const std::uint32_t p_right = idx(c + 1u, r);
                    const std::uint32_t p_down = idx(c, r + 1u);
                    const std::uint32_t p_diag = idx(c + 1u, r + 1u);
                    add_distance_constraint(p, p_diag, distance_compliance * 2.0F);
                    add_distance_constraint(p_right, p_down, distance_compliance * 2.0F);

                    triangles_.push_back(XpbdTriangleFace3D { p, p_down, p_right });
                    triangles_.push_back(XpbdTriangleFace3D { p_right, p_down, p_diag });
                }
                // 2-ring isometric bending constraints
                if (c + 2u < cols)
                {
                    add_bending_constraint(p, idx(c + 2u, r), bending_compliance);
                }
                if (r + 2u < rows)
                {
                    add_bending_constraint(p, idx(c, r + 2u), bending_compliance);
                }
            }
        }
    }

    /// Initializes a closed octahedral/icosahedral pressurized soft-body (balloon)
    /// with volume preservation and `overpressure` inflation support.
    void create_pressurized_soft_body(
        cd::math::Vec3f center = { 0.0F, 1.5F, 0.0F },
        float radius = 0.5F,
        float overpressure = 1.15F,
        float distance_compliance = 1e-4F,
        float volume_compliance = 1e-5F)
    {
        clear();
        const float r = std::max(radius, 0.05F);
        const std::array<cd::math::Vec3f, 6> verts {{
            center + cd::math::Vec3f {  r,  0.0F,  0.0F },
            center + cd::math::Vec3f { -r,  0.0F,  0.0F },
            center + cd::math::Vec3f {  0.0F,  r,  0.0F },
            center + cd::math::Vec3f {  0.0F, -r,  0.0F },
            center + cd::math::Vec3f {  0.0F,  0.0F,  r },
            center + cd::math::Vec3f {  0.0F,  0.0F, -r },
        }};

        for (const auto& v : verts)
        {
            particles_.push_back(XpbdParticle3D { v, v, {}, 1.0F });
        }

        // Octahedron closed surface (8 outward-oriented triangles)
        constexpr std::array<XpbdTriangleFace3D, 8> faces {{
            { 0u, 2u, 4u }, { 4u, 2u, 1u }, { 1u, 2u, 5u }, { 5u, 2u, 0u },
            { 4u, 3u, 0u }, { 1u, 3u, 4u }, { 5u, 3u, 1u }, { 0u, 3u, 5u }
        }};
        for (const auto& f : faces)
        {
            triangles_.push_back(f);
        }

        // Surface edges
        constexpr std::array<std::pair<std::uint32_t, std::uint32_t>, 12> edges {{
            { 0u, 2u }, { 0u, 3u }, { 0u, 4u }, { 0u, 5u },
            { 1u, 2u }, { 1u, 3u }, { 1u, 4u }, { 1u, 5u },
            { 2u, 4u }, { 4u, 3u }, { 3u, 5u }, { 5u, 2u }
        }};
        for (const auto& [a, b] : edges)
        {
            add_distance_constraint(a, b, distance_compliance);
        }

        rest_volume_ = compute_mesh_volume();
        overpressure_ = std::max(overpressure, 0.1F);
        volume_compliance_ = volume_compliance;
        volume_constraint_enabled_ = true;
    }

    void clear() noexcept
    {
        particles_.clear();
        distance_constraints_.clear();
        bending_constraints_.clear();
        triangles_.clear();
        sphere_colliders_.clear();
        capsule_colliders_.clear();
        rest_volume_ = 0.0F;
        overpressure_ = 1.0F;
        volume_constraint_enabled_ = false;
    }

    std::uint32_t add_particle(cd::math::Vec3f pos, float inv_mass = 1.0F)
    {
        const auto id = static_cast<std::uint32_t>(particles_.size());
        particles_.push_back(XpbdParticle3D { pos, pos, {}, std::max(0.0F, inv_mass) });
        return id;
    }

    void add_distance_constraint(std::uint32_t p0, std::uint32_t p1, float compliance = 1e-6F)
    {
        if (p0 >= particles_.size() || p1 >= particles_.size())
        {
            return;
        }
        const float rest = cd::math::length(particles_[p0].position - particles_[p1].position);
        distance_constraints_.push_back(XpbdDistanceConstraint3D { p0, p1, rest, compliance, 0.0F });
    }

    void add_bending_constraint(std::uint32_t p0, std::uint32_t p1, float compliance = 1e-3F)
    {
        if (p0 >= particles_.size() || p1 >= particles_.size())
        {
            return;
        }
        const float rest = cd::math::length(particles_[p0].position - particles_[p1].position);
        bending_constraints_.push_back(XpbdBendingConstraint3D { p0, p1, rest, compliance, 0.0F });
    }

    void set_wind_velocity(cd::math::Vec3f wind_vel) noexcept
    {
        wind_velocity_ = wind_vel;
    }

    [[nodiscard]] constexpr cd::math::Vec3f wind_velocity() const noexcept
    {
        return wind_velocity_;
    }

    void set_overpressure(float overpressure_factor, float volume_compliance = 1e-5F) noexcept
    {
        overpressure_ = std::max(overpressure_factor, 0.1F);
        volume_compliance_ = std::max(volume_compliance, 0.0F);
        if (rest_volume_ <= 1e-6F)
        {
            rest_volume_ = compute_mesh_volume();
        }
        volume_constraint_enabled_ = (rest_volume_ > 1e-6F);
    }

    [[nodiscard]] constexpr float rest_volume() const noexcept
    {
        return rest_volume_;
    }

    [[nodiscard]] constexpr float overpressure() const noexcept
    {
        return overpressure_;
    }

    /// Computes signed volume of the triangle mesh via divergence theorem (tetrahedral sum).
    [[nodiscard]] float compute_mesh_volume() const noexcept
    {
        double vol6 = 0.0;
        for (const auto& tri : triangles_)
        {
            if (tri.i0 >= particles_.size() || tri.i1 >= particles_.size() || tri.i2 >= particles_.size())
            {
                continue;
            }
            const auto& p0 = particles_[tri.i0].position;
            const auto& p1 = particles_[tri.i1].position;
            const auto& p2 = particles_[tri.i2].position;
            vol6 += static_cast<double>(cd::math::dot(cd::math::cross(p0, p1), p2));
        }
        return static_cast<float>(std::abs(vol6) / 6.0);
    }

    void add_sphere_collider(cd::math::Vec3f center, float radius, float friction = 0.25F)
    {
        sphere_colliders_.push_back(XpbdSphereCollider3D { center, std::max(radius, 1e-4F), friction });
    }

    void add_capsule_collider(cd::math::Vec3f a, cd::math::Vec3f b, float radius, float friction = 0.25F)
    {
        capsule_colliders_.push_back(XpbdCapsuleCollider3D { a, b, std::max(radius, 1e-4F), friction });
    }

    /// Executes an XPBD simulation step using `substeps` and `solver_iterations`.
    void step(
        float dt,
        std::uint32_t substeps = 4u,
        std::uint32_t solver_iterations = 6u) noexcept
    {
        if (dt <= 0.0F || particles_.empty())
        {
            return;
        }

        const std::uint32_t safe_substeps = std::max(substeps, 1u);
        const std::uint32_t safe_iters = std::max(solver_iterations, 1u);
        const float sub_dt = dt / static_cast<float>(safe_substeps);

        for (std::uint32_t s = 0u; s < safe_substeps; ++s)
        {
            // 1. Apply aerodynamic wind lift & drag forces to triangle faces
            apply_aerodynamic_wind_forces(sub_dt);

            // 2. Predict positions with gravity & damping
            for (auto& p : particles_)
            {
                p.prev_position = p.position;
                if (p.inv_mass <= 0.0F)
                {
                    continue;
                }
                p.velocity += gravity_ * sub_dt;
                p.velocity *= std::max(0.0F, 1.0F - damping_ * sub_dt);
                p.position += p.velocity * sub_dt;
            }

            // Reset XPBD Lagrange multipliers for this substep
            for (auto& c : distance_constraints_)
            {
                c.lambda = 0.0F;
            }
            for (auto& b : bending_constraints_)
            {
                b.lambda = 0.0F;
            }
            float volume_lambda = 0.0F;

            // 3. Gauss-Seidel XPBD constraint projection loop
            for (std::uint32_t iter = 0u; iter < safe_iters; ++iter)
            {
                for (auto& c : distance_constraints_)
                {
                    solve_distance_constraint(c.p0, c.p1, c.rest_length, c.compliance, c.lambda, sub_dt);
                }
                for (auto& b : bending_constraints_)
                {
                    solve_distance_constraint(b.p0, b.p1, b.rest_length, b.compliance, b.lambda, sub_dt);
                }
                if (volume_constraint_enabled_ && rest_volume_ > 1e-6F)
                {
                    solve_volume_constraint(volume_lambda, sub_dt);
                }
                solve_collisions();
            }

            // 4. Update velocities from position change
            const float inv_sub_dt = 1.0F / std::max(sub_dt, 1e-6F);
            for (auto& p : particles_)
            {
                if (p.inv_mass > 0.0F)
                {
                    p.velocity = (p.position - p.prev_position) * inv_sub_dt;
                }
            }
        }
    }

    /// Direct one-shot sphere collision projection on all free particles.
    std::uint32_t collide_sphere(cd::math::Vec3f center, float radius, float friction = 0.25F) noexcept
    {
        std::uint32_t hits = 0u;
        const float r = std::max(radius, 1e-4F);
        for (auto& p : particles_)
        {
            if (p.inv_mass <= 0.0F)
            {
                continue;
            }
            const cd::math::Vec3f delta = p.position - center;
            const float dist = cd::math::length(delta);
            if (dist < r)
            {
                const cd::math::Vec3f n =
                    (dist > 1e-5F) ? (delta / dist) : cd::math::Vec3f { 0.0F, 1.0F, 0.0F };
                p.position = center + n * r;
                // Tangential friction damping
                const cd::math::Vec3f disp = p.position - p.prev_position;
                const cd::math::Vec3f tangent = disp - n * cd::math::dot(disp, n);
                p.prev_position += tangent * std::clamp(friction, 0.0F, 1.0F);
                ++hits;
            }
        }
        return hits;
    }

    /// Direct one-shot capsule collision projection on all free particles.
    std::uint32_t collide_capsule(
        cd::math::Vec3f point_a,
        cd::math::Vec3f point_b,
        float radius,
        float friction = 0.25F) noexcept
    {
        std::uint32_t hits = 0u;
        const cd::math::Vec3f ab = point_b - point_a;
        const float ab_len_sq = cd::math::length_squared(ab);
        const float r = std::max(radius, 1e-4F);

        for (auto& p : particles_)
        {
            if (p.inv_mass <= 0.0F)
            {
                continue;
            }
            const float t = (ab_len_sq > 1e-6F)
                ? std::clamp(cd::math::dot(p.position - point_a, ab) / ab_len_sq, 0.0F, 1.0F)
                : 0.0F;
            const cd::math::Vec3f closest = point_a + ab * t;
            const cd::math::Vec3f delta = p.position - closest;
            const float dist = cd::math::length(delta);
            if (dist < r)
            {
                const cd::math::Vec3f n =
                    (dist > 1e-5F) ? (delta / dist) : cd::math::Vec3f { 0.0F, 1.0F, 0.0F };
                p.position = closest + n * r;
                const cd::math::Vec3f disp = p.position - p.prev_position;
                const cd::math::Vec3f tangent = disp - n * cd::math::dot(disp, n);
                p.prev_position += tangent * std::clamp(friction, 0.0F, 1.0F);
                ++hits;
            }
        }
        return hits;
    }

    [[nodiscard]] std::span<const XpbdParticle3D> particles() const noexcept
    {
        return particles_;
    }

    [[nodiscard]] std::span<XpbdParticle3D> particles_mut() noexcept
    {
        return particles_;
    }

    [[nodiscard]] std::span<const XpbdDistanceConstraint3D> distance_constraints() const noexcept
    {
        return distance_constraints_;
    }

    [[nodiscard]] std::span<const XpbdBendingConstraint3D> bending_constraints() const noexcept
    {
        return bending_constraints_;
    }

private:
    void solve_distance_constraint(
        std::uint32_t i0,
        std::uint32_t i1,
        float rest_len,
        float compliance,
        float& lambda,
        float dt) noexcept
    {
        if (i0 >= particles_.size() || i1 >= particles_.size())
        {
            return;
        }
        auto& p0 = particles_[i0];
        auto& p1 = particles_[i1];
        const float w_sum = p0.inv_mass + p1.inv_mass;
        if (w_sum <= 0.0F)
        {
            return;
        }

        const cd::math::Vec3f diff = p0.position - p1.position;
        const float dist = cd::math::length(diff);
        if (dist <= 1e-6F)
        {
            return;
        }

        const float constraint_c = dist - rest_len;
        const float alpha_tilde = compute_alpha_tilde(compliance, dt);
        const float delta_lambda = (-constraint_c - alpha_tilde * lambda) / (w_sum + alpha_tilde);
        lambda += delta_lambda;

        const cd::math::Vec3f grad = diff / dist;
        p0.position += grad * (p0.inv_mass * delta_lambda);
        p1.position -= grad * (p1.inv_mass * delta_lambda);
    }

    void solve_volume_constraint(float& volume_lambda, float dt) noexcept
    {
        if (triangles_.empty())
        {
            return;
        }

        std::vector<cd::math::Vec3f> grads(particles_.size(), cd::math::Vec3f {});
        double six_vol = 0.0;

        for (const auto& tri : triangles_)
        {
            if (tri.i0 >= particles_.size() || tri.i1 >= particles_.size() || tri.i2 >= particles_.size())
            {
                continue;
            }
            const auto& x0 = particles_[tri.i0].position;
            const auto& x1 = particles_[tri.i1].position;
            const auto& x2 = particles_[tri.i2].position;

            six_vol += static_cast<double>(cd::math::dot(cd::math::cross(x0, x1), x2));
            grads[tri.i0] += cd::math::cross(x1, x2) * (1.0F / 6.0F);
            grads[tri.i1] += cd::math::cross(x2, x0) * (1.0F / 6.0F);
            grads[tri.i2] += cd::math::cross(x0, x1) * (1.0F / 6.0F);
        }

        const float current_vol = static_cast<float>(std::abs(six_vol) / 6.0);
        const float target_vol = rest_volume_ * overpressure_;
        const float constraint_c = current_vol - target_vol;

        float denom = 0.0F;
        for (std::size_t i = 0; i < particles_.size(); ++i)
        {
            denom += particles_[i].inv_mass * cd::math::length_squared(grads[i]);
        }

        const float alpha_tilde = compute_alpha_tilde(volume_compliance_, dt);
        if (denom + alpha_tilde <= 1e-9F)
        {
            return;
        }

        const float delta_lambda = (-constraint_c - alpha_tilde * volume_lambda) / (denom + alpha_tilde);
        volume_lambda += delta_lambda;

        const float sign_scale = (six_vol >= 0.0) ? 1.0F : -1.0F;
        for (std::size_t i = 0; i < particles_.size(); ++i)
        {
            if (particles_[i].inv_mass > 0.0F)
            {
                particles_[i].position += grads[i] * (sign_scale * particles_[i].inv_mass * delta_lambda);
            }
        }
    }

    void apply_aerodynamic_wind_forces(float dt) noexcept
    {
        if (cd::math::length_squared(wind_velocity_) <= 1e-6F)
        {
            return;
        }

        constexpr float kAirDensity = 1.225F;
        constexpr float kDragCoeff = 1.15F;
        constexpr float kLiftCoeff = 0.35F;

        for (const auto& tri : triangles_)
        {
            if (tri.i0 >= particles_.size() || tri.i1 >= particles_.size() || tri.i2 >= particles_.size())
            {
                continue;
            }
            auto& p0 = particles_[tri.i0];
            auto& p1 = particles_[tri.i1];
            auto& p2 = particles_[tri.i2];

            const cd::math::Vec3f v_face = (p0.velocity + p1.velocity + p2.velocity) * (1.0F / 3.0F);
            const cd::math::Vec3f v_rel = wind_velocity_ - v_face;
            const float v_rel_speed = cd::math::length(v_rel);
            if (v_rel_speed <= 1e-4F)
            {
                continue;
            }

            const cd::math::Vec3f cross_edge =
                cd::math::cross(p1.position - p0.position, p2.position - p0.position);
            const float double_area = cd::math::length(cross_edge);
            if (double_area <= 1e-6F)
            {
                continue;
            }

            const float area = 0.5F * double_area;
            cd::math::Vec3f normal = cross_edge / double_area;
            if (cd::math::dot(normal, v_rel) < 0.0F)
            {
                normal = -normal;
            }

            const cd::math::Vec3f v_dir = v_rel / v_rel_speed;
            const float cos_theta = std::clamp(cd::math::dot(normal, v_dir), 0.0F, 1.0F);
            const float q_pressure = 0.5F * kAirDensity * v_rel_speed * v_rel_speed * area;

            const cd::math::Vec3f drag_force = v_dir * (q_pressure * kDragCoeff * cos_theta);
            const cd::math::Vec3f lift_force = normal * (q_pressure * kLiftCoeff * (1.0F - cos_theta * cos_theta));
            const cd::math::Vec3f force_per_vertex = (drag_force + lift_force) * (1.0F / 3.0F);

            p0.velocity += force_per_vertex * (p0.inv_mass * dt);
            p1.velocity += force_per_vertex * (p1.inv_mass * dt);
            p2.velocity += force_per_vertex * (p2.inv_mass * dt);
        }
    }

    void solve_collisions() noexcept
    {
        for (const auto& s : sphere_colliders_)
        {
            (void)collide_sphere(s.center, s.radius, s.friction);
        }
        for (const auto& c : capsule_colliders_)
        {
            (void)collide_capsule(c.point_a, c.point_b, c.radius, c.friction);
        }
    }

    std::vector<XpbdParticle3D>           particles_ {};
    std::vector<XpbdDistanceConstraint3D> distance_constraints_ {};
    std::vector<XpbdBendingConstraint3D>  bending_constraints_ {};
    std::vector<XpbdTriangleFace3D>       triangles_ {};
    std::vector<XpbdSphereCollider3D>     sphere_colliders_ {};
    std::vector<XpbdCapsuleCollider3D>    capsule_colliders_ {};
    cd::math::Vec3f                       gravity_ { 0.0F, -9.81F, 0.0F };
    cd::math::Vec3f                       wind_velocity_ {};
    float                                 damping_ { 0.12F };
    float                                 rest_volume_ { 0.0F };
    float                                 overpressure_ { 1.0F };
    float                                 volume_compliance_ { 1e-5F };
    bool                                  volume_constraint_enabled_ { false };
};

using XpbdClothSoftBody3D = XpbdClothSoftBodySolver3D;

}  // namespace cd::physics::rockstar
