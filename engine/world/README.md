# engine/world

**Phase 4 world tier** — simulation libraries that live one layer
above the foundation but below the game / sample / engine
orchestration. The world tier owns ECS, scene graph, physics, audio,
animation, input, and networking primitives. Higher tiers
(`game/`, `runtime/`, samples) compose these into experiences.

This directory is an **umbrella** — there is no `cd::world` library.
Each subdirectory is its own self-contained library with its own
namespace.

## Layout

```
              cd::ecs  ──►  cd::scene  ──►  cd::world_container
                                                      │
        ┌──────────────────┬───────────────────────────┤
        │                  │                           │
   cd::physics       cd::anim                    cd::input
   cd::physics_jolt  cd::anim_ik                 cd::gameplay::input_binding
   cd::physics_vehicle
   cd::physics_soft_body
        │
        │              cd::audio                cd::net
        │              cd::audio::dsp_fx        cd::net::matchmaker
        │              cd::audio::spatial       cd::network::lobby
        │                                       cd::net::session_replay
        │
        └──► cd::particle::system
        └──► cd::sample::framework
        └──► cd::gameplay::time
```

## Sub-library purpose

### Core simulation

| Library | Sprint / Phase | Role |
|---------|----------------|------|
| `cd::ecs`                  | S4.1                                        | Sparse-set ECS storage + system tick scheduling. |
| `cd::scene`                | S4.2 (planned)                              | Scene graph + transform propagation on top of ECS. |
| `cd::physics`              | S4.3 (planned)                              | `IPhysicsWorld` abstract interface. |
| `cd::physics_jolt`         | `phase525 / T0.3`                           | Jolt backend implementing `IPhysicsWorld`. |
| `cd::physics_vehicle`      | `phase670`                                  | 4-wheel CPU bicycle model + Jolt bridge. |
| `cd::physics_soft_body`    | `phase721`                                  | Cloth + rope Verlet PBD CPU sim. |
| `cd::anim`                 | S4.4 (planned)                              | Skeleton + clip + pose + LBS. |
| `cd::animation::ik`        | `phase702`                                  | CCD IK solver sidecar to `cd::anim`. |
| `cd::audio`                | S4.5 (planned)                              | 2D mixer / playback backend. |
| `cd::audio::dsp_fx`        | `phase562`                                  | Biquad / DelayLine / Reverb primitives. |
| `cd::audio::spatial`       | `phase693`                                  | 3D positional audio + HRTF. |
| `cd::input`                | S4.6 (planned)                              | Raw input event abstraction. |
| `cd::net`                  | S4.7 (planned)                              | Transport (WebSocket / reliable UDP). |
| `cd::particle::system`     | `phase564 / phase583`                       | CPU SoA particle simulation. |

### Networking lobby + replay

| Library                          | Phase         | Role |
|----------------------------------|---------------|------|
| `cd::net::matchmaker`            | `phase563`    | Skill + region match in-memory pairing. |
| `cd::network::lobby`             | `phase713`    | Room-state machine after pairing. |
| `cd::net::session_replay`        | `phase600`    | Packet record + replay (`.srpk`). |

### Application-tier glue

| Library                          | Phase         | Role |
|----------------------------------|---------------|------|
| `cd::world_container`            | gap #18       | World / Project / Level / Layer foundation. |
| `cd::sample::framework`          | Mega-Marathon M2 | `cd::sample::App` lifecycle library. |
| `cd::gameplay::time`             | `phase459`    | `TimeKeeper` multi-channel clock. |
| `cd::gameplay::input_binding`    | `phase462`    | `ActionMap` action-mapping layer. |

## Convention rules

* **No global state across world libraries.** Cross-library
  communication uses **explicit context / registry objects** — the
  caller threads them in. Reproducible save / load + multi-instance
  in a single process both depend on this rule.
* **Backend-abstract by default.** `cd::physics` is the interface;
  `cd::physics_jolt` is one backend. The same pattern applies to
  future audio / animation backends.
* **Naming-discipline.** Library directories use snake_case
  (`physics_vehicle`); namespaces use scoped colons
  (`cd::physics::vehicle`). The two must match — grep before
  inventing a new lib.

## Build / test

Each sub-library has its own gtest binary. Run the world slice:

```bash
ctest --preset ninja-debug -R '^cd_test_(ecs|scene|physics|anim|audio|input|net|particle|world_)'
```

## See also

* `CLAUDE.md §7` — modularity rule.
* `docs/ADR/ADR-20260530-jolt-physics-integration.md` — physics
  backend selection.
* `docs/ADR/ADR-20260530-gameplay-library-family.md` — gameplay-tier
  library taxonomy.
