// =============================================================================
// CHROMODYNAMIC — samples/hello_particles
// Phase 24.D — headless particle integration smoke. Spawns 128 particles
// at the origin with random upward velocities, applies gravity, ticks the
// simulation for 60 frames @ 60Hz, and prints living count + max-height
// statistics. Demonstrates cd::scene::ParticleSystem + cd::math::Random.
// =============================================================================
#include <cd/core/Version.hpp>
#include <cd/math/Random.hpp>
#include <cd/scene/ParticleSystem.hpp>

#include <cstdio>

int main()
{
    std::fprintf(stdout, "CHROMODYNAMIC %u.%u.%u — hello_particles\n",
                 static_cast<unsigned>(cd::core::kEngineVersion.major),
                 static_cast<unsigned>(cd::core::kEngineVersion.minor),
                 static_cast<unsigned>(cd::core::kEngineVersion.patch));

    cd::scene::ParticleSystem ps { 256 };
    cd::math::Random rng { 42 };

    // Spawn a fountain at origin: 128 particles, random upward velocity.
    for (int i = 0; i < 128; ++i)
    {
        const cd::math::Vec3f vel {
            rng.range(-1.0F, 1.0F),
            rng.range(2.0F, 4.0F),
            rng.range(-1.0F, 1.0F),
        };
        ps.spawn(cd::math::Vec3f { 0, 0, 0 }, vel, rng.range(0.8F, 1.5F));
    }
    std::fprintf(stdout, "spawn: %zu live particles\n", ps.live_count());

    const float dt = 1.0F / 60.0F;
    const cd::math::Vec3f gravity { 0.0F, -9.81F, 0.0F };

    float max_height = 0.0F;
    for (int frame = 0; frame < 60; ++frame)
    {
        ps.tick(dt, gravity);
        for (const auto& p : ps.particles())
            if (p.alive && p.position.y > max_height)
                max_height = p.position.y;
    }
    std::fprintf(stdout, "after 1s: %zu live particles, max height = %.3f m\n",
                 ps.live_count(), static_cast<double>(max_height));
    std::fprintf(stdout, "[hello_particles] OK\n");
    return 0;
}
