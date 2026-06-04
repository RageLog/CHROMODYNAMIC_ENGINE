// =============================================================================
// CHROMODYNAMIC - cd/particle/system/ParticleSystem.cpp
// Phase 564 (M3 W5C) - System implementation.
// Phase 583 (M5 W1) - Renamed from system_v2; no v1 ever existed.
//
// Implementation notes:
//
//   * SoA layout: all particle state lives in parallel std::vector<float>
//     channels (px/py/pz, vx/vy/vz, age, life, size_start, size_end, and
//     4x color_start + 4x color_end channels).  A parallel emitter_id vector
//     tracks which emitter spawned each particle.
//
//   * Deletion: swap-and-pop (O(1)).  The last particle is moved into the
//     erased slot; no order preservation is guaranteed between ticks.
//
//   * Integration: semi-implicit Euler.  Sprint-1 has no external acceleration
//     so velocity is constant between spawns; the v-first / x-second ordering
//     is structurally correct for future V3 force fields.
//
//   * Spawn accumulator: each emitter carries a float accumulator that
//     accumulates `emit_rate_per_sec * dt` each tick.  Whole integers are
//     consumed as spawn events; the fractional remainder carries forward.
//
//   * Velocity sampling: uniform per-component sampling in [velocity_min,
//     velocity_max] using std::mt19937 seeded once at construction.  No
//     cd::math dependency so this library only links against cd::core.
//
//   * GPU dispatch: out of scope (V3).
// =============================================================================
#include <cd/particle/system/ParticleSystem.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <ranges>

namespace cd::particle::system
{

// -----------------------------------------------------------------------------
// Internal random state — one per System instance so deterministic test seeds
// can be added later without changing the API.
// -----------------------------------------------------------------------------
namespace
{

// Thread-local so parallel test binaries do not share state.  This is fine
// for Sprint-1; V3 can inject an explicit seed via an extended constructor.
thread_local std::mt19937 s_rng {std::random_device{}()};

float random_range(float lo, float hi) noexcept
{
    if (lo >= hi)
    {
        return lo;
    }
    std::uniform_real_distribution<float> dist {lo, hi};
    return dist(s_rng);
}

}  // namespace

// =============================================================================
// Emitter management
// =============================================================================

EmitterId System::add_emitter(EmitterSpec spec)
{
    const EmitterId id = next_id_++;
    EmitterEntry entry {};
    entry.id    = id;
    entry.spec  = std::move(spec);
    entry.accum = 0.0F;
    entry.alive = true;
    emitters_.emplace_back(std::move(entry));
    return id;
}

void System::remove_emitter(EmitterId id) noexcept
{
    for (auto& e : emitters_)
    {
        if (e.id == id)
        {
            e.alive = false;
            return;
        }
    }
}

// =============================================================================
// Simulation
// =============================================================================

void System::tick(float dt)
{
    if (dt <= 0.0F)
    {
        return;
    }

    // ------------------------------------------------------------------
    // 1. Spawn from active emitters.
    // ------------------------------------------------------------------
    for (std::size_t ei = 0; ei < emitters_.size(); ++ei)
    {
        EmitterEntry& e = emitters_[ei];
        if (!e.alive)
        {
            continue;
        }
        e.accum += e.spec.emit_rate_per_sec * dt;
        while (e.accum >= 1.0F)
        {
            e.accum -= 1.0F;
            spawn_particle_(ei);
        }
    }

    // Compact dead emitters lazily after spawning so indices stay stable.
    const auto dead = std::ranges::remove_if(emitters_,
                       [](const EmitterEntry& e) noexcept { return !e.alive; });
    emitters_.erase(dead.begin(), dead.end());

    // ------------------------------------------------------------------
    // 2. Integrate + expire live particles (swap-and-pop).
    // ------------------------------------------------------------------
    std::size_t i = 0;
    while (i < px_.size())
    {
        age_[i] += dt;

        if (age_[i] >= life_[i])
        {
            // Expired — swap-and-pop.
            erase_particle_(i);
            // Do NOT increment i; the swapped-in particle at index i needs
            // to be processed in the next iteration.
            continue;
        }

        // Semi-implicit Euler: velocity updated first (no force in Sprint-1),
        // then position updated from new velocity.
        // v_new = v_old   (no acceleration this sprint)
        // p_new = p_old + v_new * dt
        px_[i] += vx_[i] * dt;
        py_[i] += vy_[i] * dt;
        pz_[i] += vz_[i] * dt;

        ++i;
    }
}

// =============================================================================
// Inspection
// =============================================================================

std::size_t System::particle_count() const noexcept
{
    return px_.size();
}

std::size_t System::snapshot(std::span<ParticleSnapshot> out) const
{
    const std::size_t n = std::min(out.size(), px_.size());
    for (std::size_t i = 0; i < n; ++i)
    {
        const float t = (life_[i] > 0.0F) ? std::clamp(age_[i] / life_[i], 0.0F, 1.0F) : 1.0F;

        ParticleSnapshot& s = out[i];
        s.pos[0] = px_[i];
        s.pos[1] = py_[i];
        s.pos[2] = pz_[i];

        // Size lerp: size_start -> size_end over normalized lifetime.
        s.size = size_start_[i] + (size_end_[i] - size_start_[i]) * t;

        // Color lerp per channel.
        s.color[0] = cr0_[i] + (cr1_[i] - cr0_[i]) * t;
        s.color[1] = cg0_[i] + (cg1_[i] - cg0_[i]) * t;
        s.color[2] = cb0_[i] + (cb1_[i] - cb0_[i]) * t;
        s.color[3] = ca0_[i] + (ca1_[i] - ca0_[i]) * t;
    }
    return n;
}

// =============================================================================
// Internal helpers
// =============================================================================

void System::spawn_particle_(std::size_t ei)
{
    const EmitterSpec& spec = emitters_[ei].spec;
    const ParticleSpec& ps  = spec.particle;

    // Position = emitter origin.
    px_.push_back(spec.position[0]);
    py_.push_back(spec.position[1]);
    pz_.push_back(spec.position[2]);

    // Velocity: uniform per-component in [velocity_min, velocity_max].
    vx_.push_back(random_range(spec.velocity_min[0], spec.velocity_max[0]));
    vy_.push_back(random_range(spec.velocity_min[1], spec.velocity_max[1]));
    vz_.push_back(random_range(spec.velocity_min[2], spec.velocity_max[2]));

    age_.push_back(0.0F);
    life_.push_back(std::max(ps.life_seconds, std::numeric_limits<float>::epsilon()));

    size_start_.push_back(ps.size_start);
    size_end_.push_back(ps.size_end);

    cr0_.push_back(ps.color_start[0]);
    cg0_.push_back(ps.color_start[1]);
    cb0_.push_back(ps.color_start[2]);
    ca0_.push_back(ps.color_start[3]);

    cr1_.push_back(ps.color_end[0]);
    cg1_.push_back(ps.color_end[1]);
    cb1_.push_back(ps.color_end[2]);
    ca1_.push_back(ps.color_end[3]);

    spawner_.push_back(emitters_[ei].id);
}

void System::erase_particle_(std::size_t i) noexcept
{
    const std::size_t last = px_.size() - 1U;

    auto swap_pop = [last, i](std::vector<float>& v) noexcept {
        v[i] = v[last];
        v.pop_back();
    };

    swap_pop(px_);
    swap_pop(py_);
    swap_pop(pz_);
    swap_pop(vx_);
    swap_pop(vy_);
    swap_pop(vz_);
    swap_pop(age_);
    swap_pop(life_);
    swap_pop(size_start_);
    swap_pop(size_end_);
    swap_pop(cr0_);
    swap_pop(cg0_);
    swap_pop(cb0_);
    swap_pop(ca0_);
    swap_pop(cr1_);
    swap_pop(cg1_);
    swap_pop(cb1_);
    swap_pop(ca1_);

    spawner_[i] = spawner_[last];
    spawner_.pop_back();
}

}  // namespace cd::particle::system
