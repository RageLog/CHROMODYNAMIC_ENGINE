// =============================================================================
// CHROMODYNAMIC - cd/particle/system/ParticleSystem.hpp
// Phase 564 (M3 W5C) - CPU-side particle simulation system.
// Phase 583 (M5 W1) - Renamed from system_v2; no v1 ever existed.
// Phase 1268 (Band-70) - drag coefficient + max_particles cap added.
//
// cd::particle::system::System is a CPU particle system with SoA storage
// and swap-and-pop deletion.  Per-particle integration uses semi-implicit
// Euler.  GPU dispatch is out of scope (V3).
//
// API (Sprint-1 + Band-70 CPU extensions):
//   ParticleSpec  — per-particle authored parameters (life, size ramp, color ramp).
//   EmitterSpec   — emitter parameters + embedded ParticleSpec.
//                   New (Band-70): drag coefficient (linear air-resistance decay);
//                                  max_particles cap per emitter (0 = unlimited).
//   EmitterId     — opaque handle returned by add_emitter(); stable until
//                   remove_emitter() is called.
//   System        — tick(dt), add_emitter, remove_emitter, particle_count,
//                   snapshot(span<ParticleSnapshot>) for renderer consumption.
//   ParticleSnapshot — renderer-facing: position, size, color.
//
// Numerical notes:
//   Semi-implicit Euler: velocity updated first from acceleration; position
//   then updated using the new velocity.  Acceleration is zero in Sprint-1
//   (no external forces); the integration path is correct for future V3.
//   Drag (Band-70): exponential velocity decay v *= exp(-drag * dt).
//   Exact for constant drag (no discretisation error); stable for any dt > 0.
//
// SoA layout:
//   Particle state is stored in seven parallel std::vector<float>s
//   (+ one uint32_t vector for emitter ids) so future SIMD sweeps can operate
//   without scatter.  Deletion uses swap-and-pop for O(1) removal.
//
// Threading: NOT thread-safe.  Wrap with an external mutex if concurrent
// emitter add/remove or concurrent tick calls are needed.
//
// Library boundary (CLAUDE.md §7):
//   Depends only on cd::core (CD_NODISCARD, CHROMA_WORLD_API).
//   No render / RHI / scene / log includes.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace cd::particle::system
{

// -----------------------------------------------------------------------------
// EmitterId — opaque handle for an active emitter.
// kInvalidEmitter is the sentinel returned on failure.
// -----------------------------------------------------------------------------
using EmitterId = std::uint32_t;
inline constexpr EmitterId kInvalidEmitter = static_cast<EmitterId>(-1);

// -----------------------------------------------------------------------------
// ParticleSpec — authored parameters applied to every particle spawned by a
// given emitter.
// -----------------------------------------------------------------------------
struct ParticleSpec
{
    float                 life_seconds {1.0F};       ///< Particle lifetime in seconds.
    float                 size_start   {1.0F};       ///< World-space size at birth.
    float                 size_end     {0.0F};       ///< World-space size at death.
    std::array<float, 4>  color_start  {1.0F, 1.0F, 1.0F, 1.0F}; ///< RGBA at birth.
    std::array<float, 4>  color_end    {1.0F, 1.0F, 1.0F, 0.0F}; ///< RGBA at death.
};

// -----------------------------------------------------------------------------
// EmitterSpec — position + velocity envelope for spawning, plus an embedded
// ParticleSpec that every spawned particle inherits.
// -----------------------------------------------------------------------------
struct EmitterSpec
{
    float                emit_rate_per_sec {10.0F};          ///< Particles per second.
    std::array<float, 3> position         {0.0F, 0.0F, 0.0F};
    std::array<float, 3> velocity_min     {-1.0F, 0.0F, -1.0F};
    std::array<float, 3> velocity_max     { 1.0F, 4.0F,  1.0F};
    /// Linear drag coefficient (air resistance).  Applied each tick as
    /// v *= exp(-drag * dt).  0.0 (default) = no drag (backward-compatible).
    /// Must be >= 0; negative values are clamped to 0 at spawn time.
    float                drag             {0.0F};
    /// Hard cap on the number of particles simultaneously alive that were
    /// spawned by this emitter.  0 (default) = unlimited (backward-compatible).
    /// Spawn is skipped when alive_count >= max_particles (> 0).
    std::uint32_t        max_particles    {0U};
    ParticleSpec         particle         {};
};

// -----------------------------------------------------------------------------
// ParticleSnapshot — renderer-facing read-only view of one live particle.
// Written by System::snapshot().
// -----------------------------------------------------------------------------
struct ParticleSnapshot
{
    std::array<float, 3> pos   {0.0F, 0.0F, 0.0F};
    float                size  {1.0F};
    std::array<float, 4> color {1.0F, 1.0F, 1.0F, 1.0F};
};

// -----------------------------------------------------------------------------
// System — owns all live particles and all registered emitters.
//
// Lifecycle:
//   * add_emitter(spec)    — register an emitter; returns a stable EmitterId.
//   * remove_emitter(id)   — unregister; particles already spawned by this
//                            emitter continue to live until their life expires.
//   * tick(dt)             — advance simulation: spawn new particles from each
//                            active emitter, integrate existing particles with
//                            semi-implicit Euler, expire particles whose age
//                            exceeds life_seconds.
//   * particle_count()     — current live particle count.
//   * snapshot(span)       — fill caller-owned span with per-particle data.
//                            Writes min(span.size(), particle_count()) entries.
//                            Returns number of entries written.
// -----------------------------------------------------------------------------
class System
{
public:
    System()  = default;
    ~System() = default;

    System(const System&)            = delete;
    System& operator=(const System&) = delete;
    System(System&&) noexcept            = default;
    System& operator=(System&&) noexcept = default;

    // -- emitter management --------------------------------------------------

    /// Register an emitter; returns a stable id for later removal.
    EmitterId add_emitter(EmitterSpec spec);

    /// Unregister an emitter.  Particles spawned before removal continue to
    /// live until their life expires.  No-op if id is unknown.
    void remove_emitter(EmitterId id) noexcept;

    // -- simulation ----------------------------------------------------------

    /// Advance the simulation by `dt` seconds.
    /// Spawns new particles from each registered emitter, integrates all live
    /// particles (semi-implicit Euler), and expires finished particles.
    void tick(float dt);

    // -- inspection ----------------------------------------------------------

    /// Current live particle count.
    [[nodiscard]] std::size_t particle_count() const noexcept;

    /// Fill `out` with a snapshot of live particles.
    /// Writes min(out.size(), particle_count()) entries.
    /// Returns the number of entries written.
    [[nodiscard]] std::size_t snapshot(std::span<ParticleSnapshot> out) const;

private:
    // -------------------------------------------------------------------------
    // SoA particle storage — float channels + uint32 channel.
    //   px/py/pz   — position
    //   vx/vy/vz   — velocity
    //   drag       — per-particle drag coefficient (copied from EmitterSpec at spawn)
    //   age        — accumulated time since birth
    //   life       — total life duration (from ParticleSpec)
    //   size_start / size_end — interpolated per tick
    //   color_start (rgba) / color_end (rgba)
    //   emitter_id — which emitter spawned this particle
    //
    // All vectors are parallel: index N refers to the same particle across all.
    // -------------------------------------------------------------------------
    std::vector<float>        px_ {};
    std::vector<float>        py_ {};
    std::vector<float>        pz_ {};
    std::vector<float>        vx_ {};
    std::vector<float>        vy_ {};
    std::vector<float>        vz_ {};
    std::vector<float>        drag_ {};  ///< per-particle drag coefficient
    std::vector<float>        age_ {};
    std::vector<float>        life_ {};
    std::vector<float>        size_start_ {};
    std::vector<float>        size_end_ {};
    // color channels: 4 floats each
    std::vector<float>        cr0_ {};  ///< color_start R
    std::vector<float>        cg0_ {};  ///< color_start G
    std::vector<float>        cb0_ {};  ///< color_start B
    std::vector<float>        ca0_ {};  ///< color_start A
    std::vector<float>        cr1_ {};  ///< color_end   R
    std::vector<float>        cg1_ {};  ///< color_end   G
    std::vector<float>        cb1_ {};  ///< color_end   B
    std::vector<float>        ca1_ {};  ///< color_end   A
    std::vector<EmitterId>    spawner_ {};  ///< which emitter spawned each particle

    // -------------------------------------------------------------------------
    // Emitter registry — parallel arrays (dense-indexed by position).
    // -------------------------------------------------------------------------
    struct EmitterEntry
    {
        EmitterId     id          {kInvalidEmitter};
        EmitterSpec   spec        {};
        float         accum       {0.0F};  ///< fractional particle accumulator
        std::uint32_t alive_count {0U};    ///< particles currently alive from this emitter
        bool          alive       {false};
    };

    std::vector<EmitterEntry> emitters_   {};
    EmitterId                 next_id_    {0};

    // -------------------------------------------------------------------------
    // Internal helpers
    // -------------------------------------------------------------------------

    /// Spawn one particle from the emitter at index `ei`.
    void spawn_particle_(std::size_t ei);

    /// Swap-and-pop the particle at index `i`.
    void erase_particle_(std::size_t i) noexcept;
};

}  // namespace cd::particle::system
