// =============================================================================
// CHROMODYNAMIC — cd/physics/soft_body/SoftBody.cpp
// Phase 721 — cd::physics::soft_body Sprint-1 implementation.
//
// Integration method: Verlet (position-based).
//
//   x_new = x + (x - x_prev) * (1 - damping) + a * dt^2
//
// where a = gravity + (accumulated_force * inv_mass).
//
// Constraint solver: Position-Based Dynamics (PBD) Gauss-Seidel style.
//
//   For each SpringConstraint (a, b):
//     delta_vec  = p[b] - p[a]
//     dist       = ||delta_vec||
//     error      = dist - rest_length
//     correction = stiffness * error / (inv_mass[a] + inv_mass[b]) * delta_vec / dist
//     p[a] += +correction * inv_mass[a]
//     p[b] += -correction * inv_mass[b]
//
//   Iterated solver_iterations times per tick for stiffness convergence.
//   Pinned particles (inv_mass == 0 or pinned == true) are unaffected by
//   corrections and are restored to their initial positions after each pass.
//
// Damping implementation (Jakobsen "velocity damping"):
//   The Verlet velocity is implicit in (x - x_prev). Damping is applied as:
//     x_prev = x_prev + (x - x_prev) * damping
//   i.e., the "velocity component" (x - x_prev) is scaled down each tick.
//   This is applied BEFORE integration so the new position already reflects
//   the damped velocity.
//
// apply_force() — instantaneous impulse in Verlet:
//   A constant force F applied over one step dt produces acceleration a = F * inv_mass.
//   In Verlet the next position shift from acceleration is: +a * dt^2.
//   Equivalently, shifting prev_pos by -a * dt^2 gives the same next position.
//   We store the offset in m_force_accum and apply it during tick integration.
//   m_force_accum is zeroed at the end of every tick (impulse is one-shot).
// =============================================================================

#include <cd/physics/soft_body/SoftBody.hpp>

#include <algorithm>
#include <cmath>

namespace cd::physics::soft_body
{

// ---- SoftBody::configure ---------------------------------------------------

void SoftBody::configure(const SoftBodyConfig& cfg) noexcept
{
    m_cfg       = cfg;
    m_particles = cfg.particles;

    m_force_accum.assign(m_particles.size(), std::array<float, 3>{ 0.0F, 0.0F, 0.0F });
}

// ---- SoftBody::tick --------------------------------------------------------

void SoftBody::tick(float dt, std::array<float, 3> gravity) noexcept
{
    if (dt <= 0.0F || m_particles.empty())
    {
        return;
    }

    const float dt2     = dt * dt;
    const float damping = std::clamp(m_cfg.damping, 0.0F, 1.0F);

    // ---- 1. Verlet integration + gravity + accumulated forces ----------------
    for (std::size_t i = 0; i < m_particles.size(); ++i)
    {
        Particle& p = m_particles[i];

        if (p.pinned || p.inv_mass <= 0.0F)
        {
            // Pinned particles do not move; keep prev == pos for clean restart.
            p.prev_position = p.position;
            continue;
        }

        // Velocity estimate (Jakobsen damping on the implicit velocity term).
        const std::array<float, 3> vel {
            (p.position[0] - p.prev_position[0]) * (1.0F - damping),
            (p.position[1] - p.prev_position[1]) * (1.0F - damping),
            (p.position[2] - p.prev_position[2]) * (1.0F - damping)
        };

        // Acceleration = gravity + force_accum * inv_mass.
        const std::array<float, 3>& fa = m_force_accum[i];
        const std::array<float, 3> accel {
            gravity[0] + fa[0] * p.inv_mass,
            gravity[1] + fa[1] * p.inv_mass,
            gravity[2] + fa[2] * p.inv_mass
        };

        // Verlet: x_new = x + vel + a * dt^2.
        const std::array<float, 3> new_pos {
            p.position[0] + vel[0] + accel[0] * dt2,
            p.position[1] + vel[1] + accel[1] * dt2,
            p.position[2] + vel[2] + accel[2] * dt2
        };

        p.prev_position = p.position;
        p.position      = new_pos;
    }

    // ---- 2. PBD constraint solver (iterated) ---------------------------------
    const uint32_t iters = (m_cfg.solver_iterations > 0U) ? m_cfg.solver_iterations : 1U;
    for (uint32_t iter = 0U; iter < iters; ++iter)
    {
        solve_constraints();
        enforce_pins();
    }

    // ---- 3. Reset per-tick force accumulator ----------------------------------
    for (auto& fa : m_force_accum)
    {
        fa = { 0.0F, 0.0F, 0.0F };
    }
}

// ---- SoftBody::particles ---------------------------------------------------

std::span<const Particle> SoftBody::particles() const noexcept
{
    return std::span<const Particle>{ m_particles.data(), m_particles.size() };
}

// ---- SoftBody::apply_force -------------------------------------------------

void SoftBody::apply_force(uint32_t particle_idx, std::array<float, 3> force) noexcept
{
    if (particle_idx >= static_cast<uint32_t>(m_force_accum.size()))
    {
        return; // Out-of-range: silently ignored per API contract.
    }

    m_force_accum[particle_idx][0] += force[0];
    m_force_accum[particle_idx][1] += force[1];
    m_force_accum[particle_idx][2] += force[2];
}

// ---- SoftBody::solve_constraints -------------------------------------------

void SoftBody::solve_constraints() noexcept
{
    for (const SpringConstraint& sc : m_cfg.constraints)
    {
        if (sc.a >= static_cast<uint32_t>(m_particles.size()) ||
            sc.b >= static_cast<uint32_t>(m_particles.size()) ||
            sc.a == sc.b)
        {
            continue; // Invalid constraint: skip.
        }

        Particle& pa = m_particles[sc.a];
        Particle& pb = m_particles[sc.b];

        const float w_sum = pa.inv_mass + pb.inv_mass;
        if (w_sum <= 0.0F)
        {
            continue; // Both static: no correction possible.
        }

        // Delta vector and current distance.
        const float dx = pb.position[0] - pa.position[0];
        const float dy = pb.position[1] - pa.position[1];
        const float dz = pb.position[2] - pa.position[2];

        const float dist2 = dx * dx + dy * dy + dz * dz;
        if (dist2 < 1e-12F)
        {
            continue; // Degenerate: skip division by zero.
        }

        const float dist  = std::sqrt(dist2);
        const float error = dist - sc.rest_length;

        // PBD correction scalar per unit direction.
        const float lambda = (sc.stiffness * error) / (w_sum * dist);

        if (!pa.pinned && pa.inv_mass > 0.0F)
        {
            pa.position[0] += pa.inv_mass * lambda * dx;
            pa.position[1] += pa.inv_mass * lambda * dy;
            pa.position[2] += pa.inv_mass * lambda * dz;
        }

        if (!pb.pinned && pb.inv_mass > 0.0F)
        {
            pb.position[0] -= pb.inv_mass * lambda * dx;
            pb.position[1] -= pb.inv_mass * lambda * dy;
            pb.position[2] -= pb.inv_mass * lambda * dz;
        }
    }
}

// ---- SoftBody::enforce_pins ------------------------------------------------

void SoftBody::enforce_pins() noexcept
{
    // Restore pinned particles to their original (configured) positions.
    // m_cfg.particles holds the authoritative initial state.
    for (std::size_t i = 0; i < m_particles.size(); ++i)
    {
        if (m_particles[i].pinned || m_particles[i].inv_mass <= 0.0F)
        {
            m_particles[i].position      = m_cfg.particles[i].position;
            m_particles[i].prev_position = m_cfg.particles[i].position;
        }
    }
}

}  // namespace cd::physics::soft_body
