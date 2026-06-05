// =============================================================================
// CHROMODYNAMIC — cd/physics/soft_body/SoftBody.cpp
// Phase 721 — cd::physics::soft_body Sprint-1 implementation.
// Phase 760 — Sprint-2 self-collision (spatial-hash broad-phase + impulse).
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
//
// Sprint-2 — Self-collision (phase 760):
//
//   Broad-phase: uniform spatial hash (Teschner 2003). Each particle is
//   quantized to integer grid coords (floor(x / cell_size)) and the triple
//   is hashed using the canonical large-prime XOR hash. Per-tick we sort
//   particle indices by hash and build a CSR-style bucket_start array so
//   particles sharing a hash bucket are contiguous and O(1) lookupable.
//
//   For each particle i we visit its 27-cell neighbourhood (3x3x3 around the
//   home cell). For each candidate j (with j > i to avoid double-count) we
//   compute |p_j - p_i| and apply a PBD repulsion when dist < 2 * radius:
//
//     contact_dist = 2 * radius
//     error        = contact_dist - dist       (positive only)
//     n            = (p_j - p_i) / dist
//     lambda       = error / (w_i + w_j)
//     p_i -= lambda * w_i * n
//     p_j += lambda * w_j * n
//
//   Pinned particles act as infinite-mass obstacles (w == 0).
//
//   Stability: ran AFTER each distance-constraint pass inside the iteration
//   loop, so corrections compose with springs symmetrically. Followed by
//   enforce_pins() to restore pinned positions.
//
//   Hash collision tolerance: two distant cells may share a hash bucket. Our
//   bucket scan checks the actual integer cell coordinates and rejects any
//   particle not in the requested cell — this turns hash collisions into a
//   filtered pass at no correctness cost.
// =============================================================================

#include <cd/physics/soft_body/SoftBody.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace cd::physics::soft_body
{

// ---- SoftBody::configure ---------------------------------------------------

void SoftBody::configure(const SoftBodyConfig& cfg) noexcept
{
    m_cfg       = cfg;
    m_particles = cfg.particles;

    const std::size_t n = m_particles.size();
    m_force_accum.assign(n, std::array<float, 3>{ 0.0F, 0.0F, 0.0F });

    // Sprint-2 scratch buffers. Sized once at configure() so tick() does no
    // heap allocation. bucket_start sizing uses a next-power-of-two table to
    // keep the modulo as a bitmask; with N <= 32 use 64, otherwise round up.
    m_cell_coord.assign(n, std::array<int32_t, 3>{ 0, 0, 0 });
    m_cell_hash.assign(n, 0U);
    m_hash_index.assign(n, 0U);

    std::size_t table_size = 64U;
    while (table_size < n * 2U)
    {
        table_size *= 2U;
    }
    m_bucket_start.assign(table_size + 1U, 0U);
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
    // Each iteration: distance constraints -> self-collision (Sprint-2) -> pins.
    // Self-collision is interleaved inside the loop so it composes with the
    // distance pass instead of being a single post-fix that could be undone
    // by the next solver_iterations call.
    const uint32_t iters = (m_cfg.solver_iterations > 0U) ? m_cfg.solver_iterations : 1U;
    for (uint32_t iter = 0U; iter < iters; ++iter)
    {
        solve_constraints();
        solve_self_collisions();
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

// ---- SoftBody::solve_self_collisions ---------------------------------------

namespace
{

/// Quantize a world-space coordinate to an integer grid cell index using a
/// constant cell size. std::floor is used so negative coords map correctly.
[[nodiscard]] int32_t quantize_cell(float v, float inv_cell_size) noexcept
{
    return static_cast<int32_t>(std::floor(v * inv_cell_size));
}

/// Canonical 3D spatial hash (Teschner et al., VMV 2003).
///   h = (cx * p1) XOR (cy * p2) XOR (cz * p3)
/// p1, p2, p3 are large primes; XOR keeps the distribution flat across grids
/// of typical cloth simulation sizes.
[[nodiscard]] uint32_t spatial_hash(int32_t cx, int32_t cy, int32_t cz,
                                    uint32_t table_mask) noexcept
{
    constexpr uint32_t kP1 = 73856093U;
    constexpr uint32_t kP2 = 19349663U;
    constexpr uint32_t kP3 = 83492791U;

    const auto ucx = static_cast<uint32_t>(cx);
    const auto ucy = static_cast<uint32_t>(cy);
    const auto ucz = static_cast<uint32_t>(cz);

    return ((ucx * kP1) ^ (ucy * kP2) ^ (ucz * kP3)) & table_mask;
}

}  // namespace

void SoftBody::solve_self_collisions() noexcept
{
    const SelfCollision& sc_cfg = m_cfg.self_collision;
    if (!sc_cfg.enable_self_collision)
    {
        return; // Disabled: zero cost for Sprint-1 ropes.
    }

    const std::size_t n = m_particles.size();
    if (n < 2U)
    {
        return; // No pair to collide.
    }

    const float radius = sc_cfg.particle_radius;
    if (radius <= 0.0F)
    {
        return; // Degenerate: invisible particles cannot collide.
    }

    // cell_size MUST be >= 2 * radius so the 3x3x3 neighbourhood covers every
    // pair that can be in contact. If the config violates this, widen at
    // runtime (silently) rather than miss collisions.
    const float cell_size      = std::max(sc_cfg.spatial_hash_cell_size, 2.0F * radius);
    const float inv_cell_size  = 1.0F / cell_size;
    const float contact_dist   = 2.0F * radius;
    const float contact_dist2  = contact_dist * contact_dist;

    // table_size was rounded to a power of two in configure(); derive a mask
    // from the size minus one (size = bucket_start.size() - 1).
    const std::size_t table_size = m_bucket_start.size() - 1U;
    const auto        table_mask = static_cast<uint32_t>(table_size - 1U);

    // ---- 1. Quantize + hash all particles ------------------------------------
    for (std::size_t i = 0; i < n; ++i)
    {
        const Particle& p = m_particles[i];
        const int32_t cx = quantize_cell(p.position[0], inv_cell_size);
        const int32_t cy = quantize_cell(p.position[1], inv_cell_size);
        const int32_t cz = quantize_cell(p.position[2], inv_cell_size);
        m_cell_coord[i] = { cx, cy, cz };
        m_cell_hash[i]  = spatial_hash(cx, cy, cz, table_mask);
        m_hash_index[i] = static_cast<uint32_t>(i);
    }

    // ---- 2. Sort indices by hash (stable not required) -----------------------
    std::sort(m_hash_index.begin(), m_hash_index.end(),
              [&](uint32_t a, uint32_t b) noexcept {
                  return m_cell_hash[a] < m_cell_hash[b];
              });

    // ---- 3. Build CSR bucket_start (counting + prefix sum) -------------------
    std::fill(m_bucket_start.begin(), m_bucket_start.end(), 0U);
    for (std::size_t i = 0; i < n; ++i)
    {
        ++m_bucket_start[m_cell_hash[i] + 1U];
    }
    for (std::size_t b = 1; b < m_bucket_start.size(); ++b)
    {
        m_bucket_start[b] += m_bucket_start[b - 1U];
    }
    // m_hash_index is already grouped by hash (we sorted); the prefix-sum
    // bucket_start now gives the [start, end) range for each bucket.

    // ---- 4. Near-phase: visit 3x3x3 neighbourhood for each particle ----------
    // To avoid double counting we only accept candidates with j > i. Pinned
    // particles are still iterated as "i" so they can push free "j" candidates
    // out (the w_sum > 0 guard below preserves both-pinned no-op semantics).
    for (std::size_t i = 0; i < n; ++i)
    {
        const int32_t cx0 = m_cell_coord[i][0];
        const int32_t cy0 = m_cell_coord[i][1];
        const int32_t cz0 = m_cell_coord[i][2];

        for (int32_t dz = -1; dz <= 1; ++dz)
        {
            for (int32_t dy = -1; dy <= 1; ++dy)
            {
                for (int32_t dx = -1; dx <= 1; ++dx)
                {
                    const int32_t cx = cx0 + dx;
                    const int32_t cy = cy0 + dy;
                    const int32_t cz = cz0 + dz;
                    const uint32_t h = spatial_hash(cx, cy, cz, table_mask);

                    const uint32_t b_begin = m_bucket_start[h];
                    const uint32_t b_end   = m_bucket_start[h + 1U];

                    for (uint32_t k = b_begin; k < b_end; ++k)
                    {
                        const uint32_t j = m_hash_index[k];
                        if (j <= static_cast<uint32_t>(i))
                        {
                            continue; // Skip self + already-processed pair.
                        }

                        // Hash collision filter: confirm the candidate truly
                        // lives in the queried integer cell.
                        if (m_cell_coord[j][0] != cx ||
                            m_cell_coord[j][1] != cy ||
                            m_cell_coord[j][2] != cz)
                        {
                            continue;
                        }

                        Particle& pa = m_particles[i];
                        Particle& pb = m_particles[j];

                        const float dxp = pb.position[0] - pa.position[0];
                        const float dyp = pb.position[1] - pa.position[1];
                        const float dzp = pb.position[2] - pa.position[2];

                        const float d2 = dxp * dxp + dyp * dyp + dzp * dzp;
                        if (d2 >= contact_dist2 || d2 < 1e-12F)
                        {
                            // Out of contact range, or coincident (degenerate
                            // -- skip rather than divide by zero; next tick
                            // perturbation usually resolves coincidence).
                            continue;
                        }

                        const float dist  = std::sqrt(d2);
                        const float error = contact_dist - dist; // > 0 here.

                        const float w_sum = pa.inv_mass + pb.inv_mass;
                        if (w_sum <= 0.0F)
                        {
                            continue; // Both pinned: no correction possible.
                        }

                        const float lambda = error / (w_sum * dist);

                        if (!pa.pinned && pa.inv_mass > 0.0F)
                        {
                            pa.position[0] -= pa.inv_mass * lambda * dxp;
                            pa.position[1] -= pa.inv_mass * lambda * dyp;
                            pa.position[2] -= pa.inv_mass * lambda * dzp;
                        }
                        if (!pb.pinned && pb.inv_mass > 0.0F)
                        {
                            pb.position[0] += pb.inv_mass * lambda * dxp;
                            pb.position[1] += pb.inv_mass * lambda * dyp;
                            pb.position[2] += pb.inv_mass * lambda * dzp;
                        }
                    }
                }
            }
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
