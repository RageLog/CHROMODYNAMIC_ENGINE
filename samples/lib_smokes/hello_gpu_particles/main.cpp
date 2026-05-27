// =============================================================================
// CHROMODYNAMIC — samples/hello_gpu_particles
//
// CPU-reference smoke for cd::gpu_particles. Spawns 1024 particles in a
// fountain configuration, advances 60 frames @ 16 ms with gravity, and
// reports the live count + bounding box after each second. Same math as
// the kSimulateCS compute kernel; this lets us validate the simulation
// API without standing up a full compute pipeline.
// =============================================================================
#include <cd/core/Version.hpp>
#include <cd/gpu_particles/GpuParticles.hpp>
#include <cd/math/Random.hpp>

#include <cmath>
#include <cstdio>
#include <vector>

int main()
{
    std::printf("CHROMODYNAMIC %u.%u.%u — hello_gpu_particles\n",
                static_cast<unsigned>(cd::core::kEngineVersion.major),
                static_cast<unsigned>(cd::core::kEngineVersion.minor),
                static_cast<unsigned>(cd::core::kEngineVersion.patch));

    namespace gp = cd::gpu_particles;

    constexpr std::size_t kCount = 1024;
    std::vector<gp::Particle> particles;
    particles.reserve(kCount);

    cd::math::Random rng { 0xC0FFEEu };
    for (std::size_t i = 0; i < kCount; ++i)
    {
        gp::Particle p {};
        p.position = { 0.0F, 0.1F, 0.0F };
        const float theta = rng.next_float() * 2.0F * 3.14159265F;
        const float phi   = rng.next_float() * 0.5F * 3.14159265F;  // upper hemi
        const float speed = 3.0F + 2.0F * rng.next_float();
        p.velocity = {
            std::sin(phi) * std::cos(theta) * speed,
            std::cos(phi) * speed,
            std::sin(phi) * std::sin(theta) * speed };
        p.life       = 0.5F + 1.5F * rng.next_float();
        p.max_life   = p.life;
        p.color      = { 1.0F, 0.6F, 0.2F, 1.0F };
        particles.push_back(p);
    }
    std::printf("  spawned %zu particles\n", particles.size());

    const cd::math::Vec3f gravity { 0.0F, -9.81F, 0.0F };
    constexpr float dt = 1.0F / 60.0F;
    float t = 0.0F;
    for (int frame = 0; frame < 120; ++frame)
    {
        gp::advance(particles, dt, gravity);
        t += dt;
        if (frame % 30 == 29)
        {
            const std::uint32_t live = gp::compact_alive(particles);
            cd::math::Vec3f bmin {  1e30F,  1e30F,  1e30F };
            cd::math::Vec3f bmax { -1e30F, -1e30F, -1e30F };
            for (std::uint32_t i = 0; i < live; ++i)
            {
                const auto& p = particles[i];
                bmin.x = std::min(bmin.x, p.position.x);
                bmin.y = std::min(bmin.y, p.position.y);
                bmin.z = std::min(bmin.z, p.position.z);
                bmax.x = std::max(bmax.x, p.position.x);
                bmax.y = std::max(bmax.y, p.position.y);
                bmax.z = std::max(bmax.z, p.position.z);
            }
            std::printf("  t=%4.2fs  live=%4u  bbox=[(%5.2f, %5.2f, %5.2f) → (%5.2f, %5.2f, %5.2f)]\n",
                        static_cast<double>(t), live,
                        static_cast<double>(bmin.x), static_cast<double>(bmin.y), static_cast<double>(bmin.z),
                        static_cast<double>(bmax.x), static_cast<double>(bmax.y), static_cast<double>(bmax.z));
        }
    }

    const std::uint32_t final_live = gp::compact_alive(particles);
    std::printf("[hello_gpu_particles] PARITY OK — %u of %zu particles still alive after 2 s\n",
                final_live, particles.size());
    return 0;
}
