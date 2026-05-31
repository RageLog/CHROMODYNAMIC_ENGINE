// =============================================================================
// CHROMODYNAMIC — samples/world/hello_particle_system
// Phase 590 — console proof that cd::particle::system::System is consumable
// from an external sample without any render/RHI/scene dependency.
//
// Scenario:
//   1. Create a System with one emitter spawning 10 particles/sec.
//   2. Tick 60 times at dt = 1/30 s (2 seconds of simulation).
//   3. Print the final live particle count and average XYZ position.
//   4. Exit 0 on success.
//
// Expected after 2 s at 10 p/s with 1-s lifetime:
//   Live count ~ 10 (steady state: 10 spawned/s, 10 die/s at 1-s life).
// =============================================================================
#include <cd/particle/system/ParticleSystem.hpp>

#include <cstdio>
#include <vector>

int main()
{
    namespace cps = cd::particle::system;

    std::fprintf(stdout, "CHROMODYNAMIC — hello_particle_system\n");

    // ---- setup ---------------------------------------------------------------
    cps::EmitterSpec spec {};
    spec.emit_rate_per_sec   = 10.0F;
    spec.position            = {0.0F, 0.0F, 0.0F};
    spec.velocity_min        = {-1.0F, 0.0F, -1.0F};
    spec.velocity_max        = { 1.0F, 4.0F,  1.0F};
    spec.particle.life_seconds = 1.0F;

    cps::System system {};
    const cps::EmitterId eid = system.add_emitter(spec);
    std::fprintf(stdout, "emitter id: %u\n", static_cast<unsigned>(eid));

    // ---- tick 60x at dt=1/30 (2 seconds of simulation) ----------------------
    const float dt = 1.0F / 30.0F;
    for (int i = 0; i < 60; ++i)
    {
        system.tick(dt);
    }

    // ---- inspect final state ------------------------------------------------
    const std::size_t count = system.particle_count();
    std::fprintf(stdout, "final particle count: %zu\n", count);

    float avg_x = 0.0F;
    float avg_y = 0.0F;
    float avg_z = 0.0F;

    if (count > 0U)
    {
        std::vector<cps::ParticleSnapshot> snap(count);
        const std::size_t written = system.snapshot(snap);

        for (std::size_t i = 0U; i < written; ++i)
        {
            avg_x += snap[i].pos[0];
            avg_y += snap[i].pos[1];
            avg_z += snap[i].pos[2];
        }
        const float inv = 1.0F / static_cast<float>(written);
        avg_x *= inv;
        avg_y *= inv;
        avg_z *= inv;
    }

    std::fprintf(stdout, "avg position: (%.3f, %.3f, %.3f)\n",
                 static_cast<double>(avg_x),
                 static_cast<double>(avg_y),
                 static_cast<double>(avg_z));

    std::fprintf(stdout, "[hello_particle_system] OK\n");
    return 0;
}
