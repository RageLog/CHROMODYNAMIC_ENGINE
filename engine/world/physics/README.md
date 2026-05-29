# cd::physics

**Purpose**: Built-in physics engine kernel. Provides shapes (AABB, OBB, capsule, sphere, triangle), collision primitives (ray/sphere, swept capsule), contact point generation, and inertia tensor calculations. Designed for CPU-only rigid-body simulation; soft-body parameters included for future integration.

**Namespace**: `cd::physics`.

**Public Headers**:
- `cd/physics/Aabb.hpp` — axis-aligned bounding box; contains/intersect tests.
- `cd/physics/Obb.hpp` — oriented bounding box.
- `cd/physics/Sphere.hpp` — sphere; ray/sphere, sphere/sphere collision.
- `cd/physics/Capsule.hpp` — capsule (swept sphere); spine-based representation.
- `cd/physics/Ray.hpp` — ray; hit/intersection queries.
- `cd/physics/RaySphere.hpp` — ray-sphere collision detection.
- `cd/physics/CapsuleSphere.hpp` — capsule-sphere closest point + penetration.
- `cd/physics/ContactPoint.hpp` — collision contact (position, normal, depth).
- `cd/physics/SweepResult.hpp` — result of swept collision (hit, contact, normal).
- `cd/physics/InertiaTensor.hpp` — inertia tensor computation for rigid bodies.
- `cd/physics/IPhysicsWorld.hpp` — physics world interface (extensible for custom backends).
- `cd/physics/SpringJoint.hpp`, `cd/physics/SoftBodyParams.hpp` — future soft-body.

**Primary Types**:
- `AABB`, `OBB`, `Sphere`, `Capsule`, `Triangle` — collision shapes.
- `Ray`, `SweepResult` — query and response structures.
- `ContactPoint` — collision event (position, normal, depth).
- `InertiaTensor` — precomputed mass distribution.

**Build**:
```bash
cmake --build --preset ninja-debug --target cd_physics
ctest --preset ninja-debug -R physics --output-on-failure
```

**Dependencies**: cd::math, cd::core.

**Notes**:
- CPU-only physics kernel; GPU rigid-body simulation (particles, constraints) lives in cd::gpu_particles / cd::volumetric.
- Capsule-sphere is the hot path for character-environment collision.
- No broad-phase spatial partitioning; broad-phase lives in the scene graph (cd::scene).
