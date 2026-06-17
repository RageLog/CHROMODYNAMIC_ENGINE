// =============================================================================
// CHROMODYNAMIC — cd/gpu_particles/GpuParticles.hpp
// Day 23 — GPU compute particle simulation.
//
// IMPLEMENTED (sim-CS-v1, sealed in
// docs/ADR/ADR-20260616-band6-render-misc-scope.md §2):
//   * CPU reference advance() + compact_alive() (tested, drive
//     vkCmdDrawIndirect from the live count).
//   * One simulate compute kernel (kSimulateCS) that advances the buffer
//     in place and atomically accumulates the live instance_count into the
//     indirect-draw args layout it declares.
//
// NOT IMPLEMENTED (promote-on-need — needs the GPU-driven-particles
// consumer integration, not a TODO of the above):
//   * GPU emit/spawn kernel (CPU-side spawn is the caller's job today).
//   * Host-side indirect-draw buffer ownership + render-pass wiring.
//
// Reference: Riccio 2014 (AMD GPU particles) + standard compute-
// shader particle pipelines (Frostbite, UE).
// =============================================================================
#pragma once

#include <cd/math/Vector.hpp>

#include <algorithm>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>

namespace cd::gpu_particles
{

struct Particle
{
    cd::math::Vec3f position { 0, 0, 0 };
    float           life     { 0.0F };
    cd::math::Vec3f velocity { 0, 0, 0 };
    float           max_life { 1.0F };
    cd::math::Vec4f color    { 1, 1, 1, 1 };
};

/// CPU advance for tests + offline simulation. `dt` in seconds.
inline void
advance(std::span<Particle> particles, float dt, cd::math::Vec3f gravity) noexcept
{
    for (auto& p : particles)
    {
        if (p.life <= 0.0F) continue;
        p.velocity.x += gravity.x * dt;
        p.velocity.y += gravity.y * dt;
        p.velocity.z += gravity.z * dt;
        p.position.x += p.velocity.x * dt;
        p.position.y += p.velocity.y * dt;
        p.position.z += p.velocity.z * dt;
        p.life -= dt;
        // Fade alpha with remaining life fraction.
        p.color.w = std::clamp(p.life / std::max(p.max_life, 1e-3F), 0.0F, 1.0F);
    }
}

/// Compact alive-particle list into the head of the buffer. Returns
/// the live count. Use the result to drive `vkCmdDrawIndirect`.
[[nodiscard]] inline std::uint32_t
compact_alive(std::span<Particle> particles) noexcept
{
    std::uint32_t live = 0;
    for (auto& p : particles)
    {
        if (p.life <= 0.0F) continue;
        if (std::cmp_not_equal(live, &p - particles.data()))
            particles[live] = p;
        ++live;
    }
    return live;
}

// ---- GLSL kernels -----------------------------------------------------------

constexpr std::string_view kSimulateCS = R"glsl(
#version 460
struct Particle {
  vec3 position; float life;
  vec3 velocity; float max_life;
  vec4 color;
};
layout(local_size_x = 64) in;
layout(set = 0, binding = 0) buffer Buf { Particle particles[]; } P;
layout(set = 0, binding = 1) buffer IndirectArgs {
  uint vertex_count; uint instance_count;
  uint first_vertex; uint first_instance;
} Ind;
layout(push_constant) uniform PC {
  uint count;
  float dt;
  vec3 gravity;
} pc;
void main() {
  uint i = gl_GlobalInvocationID.x;
  if (i >= pc.count) return;
  Particle p = P.particles[i];
  if (p.life <= 0.0) return;
  p.velocity += pc.gravity * pc.dt;
  p.position += p.velocity * pc.dt;
  p.life     -= pc.dt;
  p.color.a   = clamp(p.life / max(p.max_life, 1e-3), 0.0, 1.0);
  P.particles[i] = p;
  if (p.life > 0.0) atomicAdd(Ind.instance_count, 1u);
}
)glsl";

}  // namespace cd::gpu_particles
