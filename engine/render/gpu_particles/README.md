# cd::gpu_particles

## Purpose
GPU-resident particle system with compute-based simulation and culling. Enables millions of particles with dynamic emission, physics, and LOD without CPU overhead. Integrates directly with framegraph for efficient rendering and sorting.

## Namespace
`cd::<render>::gpu_particles::`

## Public headers
- `include/cd/gpu_particles/ParticlePool.hpp` — Unified particle data structure
- `include/cd/gpu_particles/Emitter.hpp` — Emission descriptor and burst logic
- `include/cd/gpu_particles/Simulation.hpp` — Physics update kernel parameters

## Primary types
- `GpuParticles::ParticlePool` — GPU buffer collection (position, velocity, lifetime, color)
- `GpuParticles::Emitter` — Per-frame spawn configuration (rate, velocity distribution, lifetime)
- `GpuParticles::PhysicsParams` — Drag, gravity, damping factors

## Usage example
```cpp
#include <cd/gpu_particles/ParticlePool.hpp>

// Allocate particle pool on GPU.
cd::gpu_particles::ParticlePool pool(max_particles);

// Emit particles.
pool.emit({
  .position = glm::vec3(0.0f),
  .velocity_distribution = gaussian(mean_vel, std_dev),
  .lifetime_range = glm::vec2(0.5f, 2.0f),
  .count = 100
});

// Simulate (via compute shader dispatch).
pool.update(dt, gravity, wind_force);

// Render (via indirect draw).
pool.render(framegraph, camera);
```

## Build/Test
```bash
cmake --build --preset ninja-debug --target cd_gpu_particles
ctest --preset ninja-debug -R gpu_particles
```

## Dependencies
- `cd::core` — engine types
- `cd::math` — vector/matrix math

## References
- **Garland & Heckbert 1997**, "Surface Simplification Using Quadric Error Metrics" (particle LOD basis)
- Compute integration: cd::rhi framegraph patterns

## Notes
- Header-only public API; GPU compute implementation in cd::rhi.
- Supports instanced rendering with GPU-driven culling.
- Integrates with cd::post_bloom and additive blending for visual effects.
