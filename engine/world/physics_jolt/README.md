# cd::physics_jolt

**Purpose**: Jolt Physics 5.x backend implementing the `cd::physics::IPhysicsWorld` interface. Provides rigid-body dynamics, collision detection, and constraint solving for physics simulation.

**Namespace**: `cd::physics_jolt`.

**Headers**: `cd/physics_jolt/JoltWorld.hpp` and related physics types.

**Primary types**:
- `cd::physics_jolt::JoltWorld` -- implements `cd::physics::IPhysicsWorld`, wrapping Jolt's `JPH::PhysicsSystem`.
- Support for rigid bodies, colliders, constraints, raycasts, and query operations.

**Status**: Phase 525 (T0.3) ships the **STUB backend**. Full Jolt vendor integration (vcpkg `jolt-physics` port + submodule + private linking) is deferred. See `ADR-20260530-jolt-physics-integration.md` §F for the phased delivery plan.

**Integration tiers** (same pattern as `render/spirv_cross_glue` and `ui/font`):
- Tier 0 (current): Stub API with no actual physics.
- Tier 1 (follow-up): Jolt vendor wiring + basic simulation.
- Tier 2+: Constraints, ragdoll, query specialization.

**Usage example**:
```cpp
#include <cd/physics_jolt/JoltWorld.hpp>

auto world = cd::physics_jolt::JoltWorld::create();
// Physics API (stub implementation for now)
```

**Test command**: `ctest --preset ninja-debug -R cd_test_physics_jolt --output-on-failure`.

**Notes**:
- Depends on `cd::physics` (the interface) and `cd::core`.
- No external Jolt dependency linked at this phase.
- Design document: `ADR-20260530-jolt-physics-integration.md`.

**TODO**: expand coverage (currently <3 test cases); full Jolt integration pending ADR completion.
