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
//   We store the offset in force_accum_ and apply it during tick integration.
//   force_accum_ is zeroed at the end of every tick (impulse is one-shot).
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
//
// Sprint-2.5 — Bending + ground plane (depth pass):
//
//   solve_bending(): Provot (1995) flexion springs. Each BendingConstraint is
//   the SAME PBD distance projection as a structural spring but over a longer
//   span (i..i+2) with independent stiffness, so the sheet resists folding
//   without changing its stretch behaviour. Shares project_distance() with
//   solve_constraints() — identical, verified math.
//
//   solve_ground(): unilateral half-space constraint C(x) = n_hat . x - offset
//   >= 0. Penetrating particles are projected onto the plane along +n_hat;
//   optional Coulomb tangential friction damps the in-plane Verlet velocity by
//   shifting prev_position. Static/pinned particles are immovable obstacles.
//   Degenerate (zero-length) normal and disabled flag are early-out no-ops.
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
    cfg_       = cfg;
    particles_ = cfg.particles;

    const std::size_t n = particles_.size();
    force_accum_.assign(n, std::array<float, 3>{ 0.0F, 0.0F, 0.0F });

    // Sprint-2 scratch buffers. Sized once at configure() so tick() does no
    // heap allocation. bucket_start sizing uses a next-power-of-two table to
    // keep the modulo as a bitmask; with N <= 32 use 64, otherwise round up.
    cell_coord_.assign(n, std::array<int32_t, 3>{ 0, 0, 0 });
    cell_hash_.assign(n, 0U);
    hash_index_.assign(n, 0U);

    std::size_t table_size = 64U;
    while (table_size < n * 2U)
    {
        table_size *= 2U;
    }
    bucket_start_.assign(table_size + 1U, 0U);
}

// ---- SoftBody::tick --------------------------------------------------------

void SoftBody::tick(float dt, std::array<float, 3> gravity) noexcept
{
    if (dt <= 0.0F || particles_.empty())
    {
        return;
    }

    const float dt2     = dt * dt;
    const float damping = std::clamp(cfg_.damping, 0.0F, 1.0F);

    // ---- 1. Verlet integration + gravity + accumulated forces ----------------
    for (std::size_t i = 0; i < particles_.size(); ++i)
    {
        Particle& p = particles_[i];

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
        const std::array<float, 3>& fa = force_accum_[i];
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
    const uint32_t iters = (cfg_.solver_iterations > 0U) ? cfg_.solver_iterations : 1U;
    for (uint32_t iter = 0U; iter < iters; ++iter)
    {
        solve_constraints();
        solve_bending();
        solve_self_collisions();
        solve_ground();
        enforce_pins();
    }

    // ---- 3. Reset per-tick force accumulator ----------------------------------
    for (auto& fa : force_accum_)
    {
        fa = { 0.0F, 0.0F, 0.0F };
    }
}

// ---- SoftBody::particles ---------------------------------------------------

std::span<const Particle> SoftBody::particles() const noexcept
{
    return std::span<const Particle>{ particles_.data(), particles_.size() };
}

// ---- SoftBody::apply_force -------------------------------------------------

void SoftBody::apply_force(uint32_t particle_idx, std::array<float, 3> force) noexcept
{
    if (particle_idx >= static_cast<uint32_t>(force_accum_.size()))
    {
        return; // Out-of-range: silently ignored per API contract.
    }

    force_accum_[particle_idx][0] += force[0];
    force_accum_[particle_idx][1] += force[1];
    force_accum_[particle_idx][2] += force[2];
}

// ---- SoftBody::solve_constraints -------------------------------------------

namespace
{

/// One PBD distance-constraint projection between particles a and b toward
/// `rest_length` with the given `stiffness`. Shared by the structural-spring
/// and Provot-bending passes (identical math, different rest length /
/// stiffness tuning). Guards: index range, a==b, both-static (w_sum==0),
/// coincident (dist^2 ~ 0 -> divide-by-zero), and zero rest-length (which
/// simply drives the pair to coincidence — the dist^2 guard then stops once
/// they touch). Returns nothing; mutates `pts` in place.
void project_distance(std::vector<Particle>& pts, uint32_t ia, uint32_t ib,
                      float rest_length, float stiffness) noexcept
{
    if (ia >= static_cast<uint32_t>(pts.size()) ||
        ib >= static_cast<uint32_t>(pts.size()) ||
        ia == ib)
    {
        return; // Invalid constraint: skip.
    }

    Particle& pa = pts[ia];
    Particle& pb = pts[ib];

    const float w_sum = pa.inv_mass + pb.inv_mass;
    if (w_sum <= 0.0F)
    {
        return; // Both static: no correction possible.
    }

    const float dx = pb.position[0] - pa.position[0];
    const float dy = pb.position[1] - pa.position[1];
    const float dz = pb.position[2] - pa.position[2];

    const float dist2 = dx * dx + dy * dy + dz * dz;
    if (dist2 < 1e-12F)
    {
        return; // Degenerate / coincident: skip division by zero.
    }

    const float dist  = std::sqrt(dist2);
    const float error = dist - rest_length;

    // PBD correction scalar per unit direction.
    const float lambda = (stiffness * error) / (w_sum * dist);

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

}  // namespace

void SoftBody::solve_constraints() noexcept
{
    for (const SpringConstraint& sc : cfg_.constraints)
    {
        project_distance(particles_, sc.a, sc.b, sc.rest_length, sc.stiffness);
    }
}

// ---- SoftBody::solve_bending -----------------------------------------------

void SoftBody::solve_bending() noexcept
{
    // Provot (1995) flexion springs: identical PBD distance projection over a
    // longer span (typically i..i+2) so cloth/rope resist folding without
    // affecting the structural stretch springs. Empty vector -> zero cost.
    for (const BendingConstraint& bc : cfg_.bending_constraints)
    {
        project_distance(particles_, bc.a, bc.b, bc.bend_rest_length, bc.stiffness);
    }
}

// ---- SoftBody::solve_ground ------------------------------------------------

void SoftBody::solve_ground() noexcept
{
    const GroundPlane& gp = cfg_.ground;
    if (!gp.enable_ground)
    {
        return; // Disabled: zero cost.
    }

    // Normalise the plane normal. A degenerate (zero-length) normal is a config
    // error; skip rather than divide by zero.
    const float nlen2 = gp.normal[0] * gp.normal[0] +
                        gp.normal[1] * gp.normal[1] +
                        gp.normal[2] * gp.normal[2];
    if (nlen2 < 1e-12F)
    {
        return; // Degenerate normal.
    }

    const float inv_nlen = 1.0F / std::sqrt(nlen2);
    const std::array<float, 3> nhat {
        gp.normal[0] * inv_nlen,
        gp.normal[1] * inv_nlen,
        gp.normal[2] * inv_nlen
    };

    const float friction = std::clamp(gp.friction, 0.0F, 1.0F);

    for (Particle& p : particles_)
    {
        if (p.pinned || p.inv_mass <= 0.0F)
        {
            continue; // Static particles are not pushed by the ground.
        }

        // Signed distance into the free half-space: C = n.x - offset.
        const float signed_dist = nhat[0] * p.position[0] +
                                  nhat[1] * p.position[1] +
                                  nhat[2] * p.position[2] - gp.offset;

        if (signed_dist >= 0.0F)
        {
            continue; // Above / on the plane: no contact (unilateral).
        }

        // Penetration depth (positive). Project the particle back onto the
        // plane surface along +n: x += (-signed_dist) * n.
        const float push = -signed_dist;
        p.position[0] += push * nhat[0];
        p.position[1] += push * nhat[1];
        p.position[2] += push * nhat[2];

        // Tangential Coulomb friction: damp the in-plane component of the
        // implicit Verlet velocity (pos - prev_pos) by `friction`. friction==0
        // -> frictionless slide, friction==1 -> tangential velocity killed.
        if (friction > 0.0F)
        {
            const float vx = p.position[0] - p.prev_position[0];
            const float vy = p.position[1] - p.prev_position[1];
            const float vz = p.position[2] - p.prev_position[2];

            // Normal component of velocity (along n) is left untouched; only
            // the tangential part is scaled.
            const float vn = vx * nhat[0] + vy * nhat[1] + vz * nhat[2];
            const float tx = vx - vn * nhat[0];
            const float ty = vy - vn * nhat[1];
            const float tz = vz - vn * nhat[2];

            // Shift prev_position toward position by `friction` * tangential
            // velocity, which removes that fraction of the in-plane motion on
            // the next tick's velocity estimate.
            p.prev_position[0] += friction * tx;
            p.prev_position[1] += friction * ty;
            p.prev_position[2] += friction * tz;
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
    const SelfCollision& sc_cfg = cfg_.self_collision;
    if (!sc_cfg.enable_self_collision)
    {
        return; // Disabled: zero cost for Sprint-1 ropes.
    }

    const std::size_t n = particles_.size();
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
    const std::size_t table_size = bucket_start_.size() - 1U;
    const auto        table_mask = static_cast<uint32_t>(table_size - 1U);

    // ---- 1. Quantize + hash all particles ------------------------------------
    for (std::size_t i = 0; i < n; ++i)
    {
        const Particle& p = particles_[i];
        const int32_t cx = quantize_cell(p.position[0], inv_cell_size);
        const int32_t cy = quantize_cell(p.position[1], inv_cell_size);
        const int32_t cz = quantize_cell(p.position[2], inv_cell_size);
        cell_coord_[i] = { cx, cy, cz };
        cell_hash_[i]  = spatial_hash(cx, cy, cz, table_mask);
        hash_index_[i] = static_cast<uint32_t>(i);
    }

    // ---- 2. Sort indices by hash (stable not required) -----------------------
    std::ranges::sort(hash_index_,
                      [&](uint32_t a, uint32_t b) noexcept {
                          return cell_hash_[a] < cell_hash_[b];
                      });

    // ---- 3. Build CSR bucket_start (counting + prefix sum) -------------------
    std::ranges::fill(bucket_start_, 0U);
    for (std::size_t i = 0; i < n; ++i)
    {
        ++bucket_start_[cell_hash_[i] + 1U];
    }
    for (std::size_t b = 1; b < bucket_start_.size(); ++b)
    {
        bucket_start_[b] += bucket_start_[b - 1U];
    }
    // hash_index_ is already grouped by hash (we sorted); the prefix-sum
    // bucket_start now gives the [start, end) range for each bucket.

    // ---- 4. Near-phase: visit 3x3x3 neighbourhood for each particle ----------
    // To avoid double counting we only accept candidates with j > i. Pinned
    // particles are still iterated as "i" so they can push free "j" candidates
    // out (the w_sum > 0 guard below preserves both-pinned no-op semantics).
    for (std::size_t i = 0; i < n; ++i)
    {
        const int32_t cx0 = cell_coord_[i][0];
        const int32_t cy0 = cell_coord_[i][1];
        const int32_t cz0 = cell_coord_[i][2];

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

                    const uint32_t b_begin = bucket_start_[h];
                    const uint32_t b_end   = bucket_start_[h + 1U];

                    for (uint32_t k = b_begin; k < b_end; ++k)
                    {
                        const uint32_t j = hash_index_[k];
                        if (j <= static_cast<uint32_t>(i))
                        {
                            continue; // Skip self + already-processed pair.
                        }

                        // Hash collision filter: confirm the candidate truly
                        // lives in the queried integer cell.
                        if (cell_coord_[j][0] != cx ||
                            cell_coord_[j][1] != cy ||
                            cell_coord_[j][2] != cz)
                        {
                            continue;
                        }

                        Particle& pa = particles_[i];
                        Particle& pb = particles_[j];

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
    // cfg_.particles holds the authoritative initial state.
    for (std::size_t i = 0; i < particles_.size(); ++i)
    {
        if (particles_[i].pinned || particles_[i].inv_mass <= 0.0F)
        {
            particles_[i].position      = cfg_.particles[i].position;
            particles_[i].prev_position = cfg_.particles[i].position;
        }
    }
}

}  // namespace cd::physics::soft_body
