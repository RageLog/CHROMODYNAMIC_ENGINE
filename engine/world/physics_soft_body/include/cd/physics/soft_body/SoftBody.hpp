// =============================================================================
// CHROMODYNAMIC — cd/physics/soft_body/SoftBody.hpp
// Phase 721 — cd::physics::soft_body Sprint-1 (cloth/rope CPU simulation).
// Phase 760 — Sprint-2 self-collision (spatial-hash broad-phase + impulse).
//
// Provides a mass-spring CPU soft-body simulation for cloth and rope:
//   * Particle      — point mass with Verlet position state + pin flag.
//   * SpringConstraint — distance constraint between two particle indices.
//   * SelfCollision    — particle-particle repulsion configuration (Sprint-2).
//   * SoftBodyConfig   — full assembly (particles + constraints + solver tuning).
//   * SoftBody         — configure / tick / particles / apply_force API.
//
// Sprint-1 scope:
//   * Verlet integration: x_new = 2*x - x_prev + a*dt^2.
//   * Position-Based Dynamics (PBD) distance constraint solver.
//   * Pinned particles are immovable (inv_mass == 0 treated as pinned too).
//   * External gravity applied per tick as a body force.
//   * apply_force() imposes an instantaneous velocity impulse on a particle.
//
// Sprint-2 scope (phase 760):
//   * SelfCollision configuration block — particle_radius, enable flag,
//     spatial_hash_cell_size for broad-phase grid tuning.
//   * Uniform spatial hash broad-phase: each tick after the PBD constraint
//     pass, particles are binned into integer grid cells of side cell_size
//     and we visit each cell + its 26 neighbours for candidate pairs.
//   * Near-phase PBD repulsion: pairs with |p_b - p_a| < 2 * radius are
//     pushed apart along the separating axis to restore the min-distance
//     2 * radius. Inverse-mass weighting matches the distance constraint.
//   * Stable solver ordering: self-collision runs after each distance pass
//     inside the iteration loop, then pins re-enforced.
//   * Disabled by default (enable_self_collision = false) → zero overhead
//     for existing Sprint-1 ropes.
//
// Sprint-2.5 scope (depth pass):
//   * BendingConstraint — Provot (1995) flexion springs (i..i+2 distance) so
//     cloth/rope resist folding independently of structural stretch stiffness.
//   * GroundPlane — half-space (infinite plane) unilateral collision response
//     with optional Coulomb tangential friction. Default disabled.
//   Both are opt-in (empty vector / disabled flag) -> Sprint-1/2 sims unchanged.
//
// Sprint-3 (deferred / SEALED — large solver): GPU compute (Vulkan/D3D12
// compute shader port) and FEM / continuum volumetric soft bodies. A full
// co-rotational FEM tetrahedral solver and GPU XPBD port are multi-week
// efforts outside the mass-spring/PBD charter of this library; they are
// intentionally not implemented here. See README "Sealed scope".
//
// MOMENT: A character has a cape that responds to wind + gravity via real
// cloth sim — not bone-driven fake animation. Half-Life 2 gravity-gun-pull-
// a-tablecloth moment. With Sprint-2, the cape FOLDS REALISTICALLY on itself
// when the character turns — no clipping through neck or shoulders.
//
// SOTA references:
//   * Müller et al., "Position Based Dynamics", VRIPHYS 2006.
//   * Jakobsen, "Advanced Character Physics", GDC 2001.
//   * Provot, "Deformation Constraints in a Mass-Spring Model to Describe
//     Rigid Cloth Behaviour", Graphics Interface 1995.
//   * Teschner et al., "Optimized Spatial Hashing for Collision Detection of
//     Deformable Objects", VMV 2003. (Sprint-2 broad-phase reference.)
// =============================================================================
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace cd::physics::soft_body
{

// ---------------------------------------------------------------------------
// Particle — a single mass-spring point.
// ---------------------------------------------------------------------------

struct Particle
{
    /// World-space position (m). Updated each tick by Verlet integration.
    std::array<float, 3> position { 0.0F, 0.0F, 0.0F };

    /// Previous world-space position (m). Used by Verlet to derive velocity.
    /// On first tick, set equal to position for zero initial velocity.
    std::array<float, 3> prev_position { 0.0F, 0.0F, 0.0F };

    /// Inverse mass (kg^-1). Set to 0.0 to make the particle static/pinned.
    /// Typical cloth particle: 1.0 / (total_cloth_mass / particle_count).
    float inv_mass { 1.0F };

    /// Pinning flag: when true the particle is held in place regardless of
    /// inv_mass. Convenience alias for the common "attach corner to ceiling"
    /// use-case. Equivalent to setting inv_mass = 0.0F.
    bool pinned { false };
};

// ---------------------------------------------------------------------------
// SpringConstraint — a distance constraint between two particles.
// ---------------------------------------------------------------------------

struct SpringConstraint
{
    /// Index of the first particle (must be < SoftBodyConfig::particles.size()).
    uint32_t a { 0 };

    /// Index of the second particle (must be < SoftBodyConfig::particles.size()
    /// and != a).
    uint32_t b { 0 };

    /// Rest length (m). Constraint solver drives |p[b] - p[a]| toward this.
    float rest_length { 0.0F };

    /// Stiffness in [0, 1]. 1.0 = perfectly rigid (full correction per
    /// iteration). Values < 1 give elastic / rubbery behaviour.
    /// PBD correction applied per iteration: delta = stiffness * correction.
    float stiffness { 1.0F };
};

// ---------------------------------------------------------------------------
// BendingConstraint — Provot (1995) bending resistance.
//
// A bending constraint is a *distance* constraint spanning two particles that
// are NOT directly spring-connected (typically i and i+2 along a rope, or the
// opposite corners of two adjacent cloth triangles). Driving |p[b] - p[a]|
// toward bend_rest_length penalizes folding while leaving the structural
// (rest-length) springs free to stretch/shear. This is exactly the
// "flexion / bend spring" of Provot, "Deformation Constraints in a Mass-Spring
// Model to Describe Rigid Cloth Behaviour", Graphics Interface 1995.
//
// It reuses the identical PBD distance projection as SpringConstraint; the
// separate type exists so callers can tune bend stiffness independently of the
// structural springs (cloth is usually much softer in bending than in stretch).
// ---------------------------------------------------------------------------

struct BendingConstraint
{
    /// Index of the first particle (must be < SoftBodyConfig::particles.size()).
    uint32_t a { 0 };

    /// Index of the second particle (must be < SoftBodyConfig::particles.size()
    /// and != a). Typically a + 2 along a rope, or the across-fold pair on cloth.
    uint32_t b { 0 };

    /// Rest length (m) of the bend span. Solver drives |p[b] - p[a]| toward this.
    float bend_rest_length { 0.0F };

    /// Bend stiffness in [0, 1]. 0.0 = no bending resistance (fully floppy),
    /// 1.0 = maximally rigid per iteration. Cloth typically << structural
    /// stiffness so the sheet drapes but does not crease sharply.
    float stiffness { 0.1F };
};

// ---------------------------------------------------------------------------
// GroundPlane — half-space (infinite plane) collision response.
//
// Models an immovable ground / wall as the half-space n . x >= offset. After
// each PBD iteration any particle that has penetrated the plane
// (n . position < offset) is projected back onto the plane surface along n.
// This is the standard PBD inequality (unilateral) constraint:
//   C(x) = n . x - offset >= 0,  corrected only when violated.
//
// Tangential (Coulomb) friction optionally damps the in-plane velocity of a
// contacting particle by scaling the tangential component of (pos - prev_pos).
// friction == 0 -> frictionless slide; friction == 1 -> sticking contact.
// ---------------------------------------------------------------------------

struct GroundPlane
{
    /// Master switch. When false (default) no ground constraint is applied —
    /// existing rope/cloth simulations are byte-identical to the pre-ground build.
    bool enable_ground { false };

    /// Outward plane normal (need not be unit length; it is normalised at use).
    /// Default points up (+Y), i.e. a horizontal floor.
    std::array<float, 3> normal { 0.0F, 1.0F, 0.0F };

    /// Plane offset along the (normalised) normal: the half-space kept free is
    /// n_hat . x >= offset. For a floor at world height y = h with normal +Y,
    /// set offset = h.
    float offset { 0.0F };

    /// Tangential friction in [0, 1] applied to contacting particles.
    /// 0 = frictionless, 1 = full stick (tangential velocity killed on contact).
    float friction { 0.0F };
};

// ---------------------------------------------------------------------------
// SelfCollision — Sprint-2 particle-particle repulsion configuration.
// ---------------------------------------------------------------------------

struct SelfCollision
{
    /// Master switch. When false (default), the broad-phase + near-phase are
    /// entirely skipped — Sprint-1 simulations pay zero cost.
    bool enable_self_collision { false };

    /// Effective particle radius (m). Pairs closer than 2 * particle_radius
    /// are pushed apart by a PBD-style repulsion impulse so the minimum
    /// separation is restored to 2 * particle_radius.
    /// Typical cloth quad spacing 5 cm -> radius ≈ 0.02–0.03 m.
    float particle_radius { 0.02F };

    /// Spatial-hash grid cell size (m). Cells are uniform cubes; each tick
    /// we visit a particle's home cell + the 26 neighbours. For correctness
    /// cell_size MUST be >= 2 * particle_radius so that ALL pairs closer than
    /// the collision distance fall inside the 3x3x3 neighbourhood — otherwise
    /// close contacts can be missed across cell boundaries.
    /// Typical: 2.0–4.0 * particle_radius.
    float spatial_hash_cell_size { 0.05F };
};

// ---------------------------------------------------------------------------
// SoftBodyConfig — full simulation assembly.
// ---------------------------------------------------------------------------

struct SoftBodyConfig
{
    /// All particles. Order is stable; constraint indices reference this array.
    std::vector<Particle> particles {};

    /// Distance constraints. Order does not affect correctness (PBD is
    /// iterative); however, Gauss-Seidel ordering in constraint order is used.
    std::vector<SpringConstraint> constraints {};

    /// Bending constraints (Provot flexion springs). Default empty -> no
    /// bending resistance, so existing structural-only sims are unchanged.
    /// Solved with the same PBD distance projection after the structural pass.
    std::vector<BendingConstraint> bending_constraints {};

    /// Number of PBD constraint solver iterations per tick.
    /// Higher values improve rigidity at the cost of CPU time.
    /// Typical cloth: 5–20. Rope: 10–30.
    uint32_t solver_iterations { 10 };

    /// Velocity damping factor in [0, 1] applied each tick.
    /// Derived as: prev_pos += damping * (pos - prev_pos) * (1 - damping).
    /// 0.0 = no damping (underdamped). 1.0 = full freeze.
    /// Typical cloth: 0.01–0.05.
    float damping { 0.01F };

    /// Sprint-2 self-collision parameters. Default-constructed: disabled.
    SelfCollision self_collision {};

    /// Ground / half-space collision plane. Default-constructed: disabled.
    GroundPlane ground {};
};

// ---------------------------------------------------------------------------
// SoftBody — the main simulation object.
// ---------------------------------------------------------------------------

class SoftBody
{
public:
    SoftBody() noexcept = default;
    ~SoftBody() noexcept = default;

    SoftBody(const SoftBody&) = default;
    SoftBody& operator=(const SoftBody&) = default;
    SoftBody(SoftBody&&) noexcept = default;
    SoftBody& operator=(SoftBody&&) noexcept = default;

    // ---- Setup ---------------------------------------------------------------

    /// Replace the current configuration. Copies particles and constraints.
    /// Resets any accumulated impulse state (prev_positions taken from the
    /// configured particle array — set prev_position == position to start at
    /// rest). Safe to call multiple times (re-configure mid-simulation is
    /// supported for cloth tearing / re-meshing).
    void configure(const SoftBodyConfig& cfg) noexcept;

    // ---- Per-frame update ----------------------------------------------------

    /// Advance the simulation by `dt` seconds.
    ///   1. Verlet integration applies gravity + damping.
    ///   2. Per iteration (solver_iterations): structural distance constraints,
    ///      bending constraints, self-collision, ground plane, then pin re-fix.
    ///   3. Pinned particles are fixed back to their configured positions.
    ///
    /// @param dt       Time step (seconds). Positive. Zero or negative is no-op.
    /// @param gravity  World-space gravity vector (m/s^2). Typical Earth:
    ///                 {0, -9.81, 0}.
    void tick(float dt, std::array<float, 3> gravity) noexcept;

    // ---- Output --------------------------------------------------------------

    /// Read-only view of all particles after the last tick().
    /// The span lifetime is valid until the next call to configure() or tick().
    [[nodiscard]] std::span<const Particle> particles() const noexcept;

    // ---- Impulse -------------------------------------------------------------

    /// Apply an instantaneous force impulse to a single particle for the
    /// duration of one tick. The impulse is converted to a prev_position shift:
    ///   prev_pos -= force * inv_mass * dt * dt
    /// This is equivalent to injecting a velocity kick in the Verlet scheme.
    ///
    /// @param particle_idx  Index into the particle array. Must be < count.
    ///                      Out-of-range indices are silently ignored.
    /// @param force         Force vector (N). Applied for the next tick step;
    ///                      accumulated with gravity (not persistent).
    void apply_force(uint32_t particle_idx, std::array<float, 3> force) noexcept;

private:
    // ---- Internal helpers ----------------------------------------------------

    /// Run one pass of PBD distance constraint correction (structural springs).
    void solve_constraints() noexcept;

    /// Run one pass of PBD bending-constraint correction (Provot flexion).
    /// No-op when cfg_.bending_constraints is empty.
    void solve_bending() noexcept;

    /// Run one pass of half-space ground collision response. No-op when
    /// cfg_.ground.enable_ground == false or the plane normal is degenerate.
    void solve_ground() noexcept;

    /// Run one pass of Sprint-2 particle-particle self-collision repulsion
    /// using a uniform spatial-hash broad-phase. No-op when
    /// cfg_.self_collision.enable_self_collision == false or when there are
    /// fewer than two non-pinned particles.
    void solve_self_collisions() noexcept;

    /// Re-pin all particles that have pinned == true or inv_mass == 0.0F back
    /// to their initial (configured) positions.
    void enforce_pins() noexcept;

    // ---- State ---------------------------------------------------------------

    SoftBodyConfig cfg_ {};

    /// Live particle state (copy of cfg.particles, mutated each tick).
    std::vector<Particle> particles_ {};

    /// Accumulated per-particle force offsets (reset each tick).
    /// Indexed parallel to particles_. Used by apply_force().
    std::vector<std::array<float, 3>> force_accum_ {};

    // ---- Sprint-2 self-collision scratch buffers ----------------------------
    // Allocated once at configure() time (sized to particle count) to avoid
    // per-tick heap churn in solve_self_collisions().

    /// Integer grid coordinate triple (cell_x, cell_y, cell_z) per particle.
    std::vector<std::array<int32_t, 3>> cell_coord_ {};

    /// Hash key per particle (computed each tick from cell_coord_).
    std::vector<uint32_t> cell_hash_ {};

    /// Sort-permutation: indices [0..N) reordered so equal-hash particles are
    /// contiguous. Built each tick via std::sort over cell_hash_.
    std::vector<uint32_t> hash_index_ {};

    /// For each hash bucket key (mod table size), the start offset into
    /// hash_index_ of particles whose hash equals the key.
    /// Size = bucket_count + 1; last entry = hash_index_.size().
    std::vector<uint32_t> bucket_start_ {};
};

}  // namespace cd::physics::soft_body
