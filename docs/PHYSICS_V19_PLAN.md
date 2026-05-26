# v1.9 Physics + Gameplay Plan

The current physics layer (engine/world/physics/) exposes an abstract
collision-primitive API: Aabb, Sphere, Capsule, Obb, Ray, Triangle,
SpringJoint, SoftBodyParams, IPhysicsWorld, plus narrow-phase helpers
(CapsuleSphere, RaySphere, SweepResult). There is no rigid-body
dynamics integrator yet — IPhysicsWorld is an interface awaiting a
concrete implementation.

v1.9 ships are split into five sub-units:

## v1.9.1 — Jolt rigid-body integration
- Vendor Jolt (vcpkg manifest: jolt-physics)
- Implement IPhysicsWorld backed by JPH::PhysicsSystem
- Map cd::physics primitives to JPH shapes one-to-one
- Per-tick simulation step at fixed 60 Hz; interpolate to render dt
- ECS adapter: RigidBodyComponent stores the JPH::BodyID
- hello_engine adds a 'P' shortcut + 'Physics: Toggle gravity' palette
  command

## v1.9.2 — CCD for fast-moving objects
- Enable JPH motion-quality kLinearCast on objects flagged ccd=true
- Add ECS field RigidBodyComponent::motion_quality
- Stress test: a small sphere fired at 100 m/s into a thin plane
  must not tunnel

## v1.9.3 — Cloth simulation
- Position-based dynamics (Müller 2007) on top of cd::physics
  SpringJoint primitives
- Cloth resource: triangle mesh + per-vertex inverse-mass +
  edge/bend constraint sets
- Compute pipeline (Vulkan) for the integrator; CPU reference for
  determinism tests
- hello_engine demo: a 1 m square cloth pinned at two corners

## v1.9.4 — Character controller
- Capsule-based kinematic body with stair-step + slope handling
- Input mapping: WASD planar move, Space jump, Shift sprint
- Integrate with the existing camera so the user can 'walk' through
  the sphere grid rather than orbit-only

## v1.9.5 — Scripting + AI behaviour trees
- cd::script::Engine (current placeholder) -> Lua 5.4 (vcpkg lua)
- Hot-reload script files via cd::vfs change events
- Behaviour-tree node library: Sequence, Selector, Decorator, Action
- Demo: a sphere entity that randomly walks the grid using a 3-node
  tree (idle/walk/look-at-camera)

## Validation gates

- ctest -L physics — Jolt-backed IPhysicsWorld must pass the existing
  primitive tests AND the new dynamics regression suite (impulse
  response, restitution, friction, sweep).
- Frame budget: physics step <= 2 ms on a mid-tier 2024 CPU at 4k
  rigid bodies.
- Determinism: same seed + same script -> bit-identical world snapshot
  after 600 frames.

## Risk register

- Jolt's API moves between minor versions; vendoring pins to the
  release tag that ships with vcpkg at v1.9 cut.
- Lua hot-reload + Vulkan resource lifetime: lua references that
  capture engine handles must release before the host destroys them
  (test via shutdown stress).
- CCD on cloth: PBD does its own swept-collision; Jolt CCD applies
  only to rigid bodies.
