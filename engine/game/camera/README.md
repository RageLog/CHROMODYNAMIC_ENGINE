# cd::game::camera

**Purpose**: Cinemachine-style virtual camera stack + brain. Phase 482 / G3.3 in the Phase-G gameplay tier.

**Namespace**: `cd::game::camera`.

**Header**: `cd/game/camera/VirtualCamera.hpp`.

**Primary types**:

| Type | Role |
|---|---|
| `VCamSettings` | Lens (fov, near, far) + framing (position_offset, rotation_offset) + per-axis damping half-life. |
| `VirtualCamera` | One vcam entry: target entity + priority + blend_in/out durations + enabled flag + settings. |
| `CameraBrain` | Registry of vcams. `add_vcam`, `remove_vcam`, `tick(dt, target_pos_fn)` -> `cd::camera::Camera`. |

**Selection rule** (Unity Cinemachine parity):

* Each tick the brain picks the highest-priority **enabled** vcam as the "live" source.
* Ties broken by registration order (older wins).
* When the live id changes, a blend starts from the brain's current output to the new live vcam's instantaneous sample. Blend duration = `max(prev.blend_out, next.blend_in)` ("the more emphatic side wins").
* A third vcam interrupting a blend re-anchors the blend FROM the current output TO the new winner (no double-blending).

**Damping**: critically-damped spring per axis (zeta = 1):

```
omega = 2 / damping_seconds
acc   = -2*omega*vel - omega^2 * (current - target)
vel  += acc * dt
cur  += vel * dt
```

`damping_seconds == 0` short-circuits to a snap (no overshoot, no lag). Critically-damped chosen over under/over-damped because gameplay cameras must not overshoot or feel sluggish - matches *Game Programming Gems 4* ch. 1.10 ("A Critically Damped, Tunable Spring") and Cinemachine's "Body Damping" axis.

Damping is **disabled during blends** because the blend itself is a temporal interpolant; layering damping on top is the usual misconfiguration that slows the blend tail to a crawl.

**Negative dt contract**: `tick(dt < 0, ...)` returns the previous output unchanged and sets `last_tick_ok() = false`. The brief calls for negative-dt rejection; we surface it without throwing so frame-loop integrators stay exception-free.

**Usage example**:

```cpp
#include <cd/game/camera/VirtualCamera.hpp>

using namespace cd::game::camera;

CameraBrain brain;

// Resolve entity -> world position via your scene graph.
auto target_pos_fn = [&scene](cd::ecs::Entity e) -> cd::math::Vec3f {
    if (auto* w = scene.world_transform(e); w != nullptr)
        return { w->matrix[3][0], w->matrix[3][1], w->matrix[3][2] };
    return {};
};

// Free-look vcam at priority 10.
VirtualCamera v_free;
v_free.set_target(player_entity);
v_free.settings().position_offset = { 0, 2, 5 };
v_free.settings().damping         = { 0.3F, 0.3F, 0.3F };
const auto free_id = brain.add_vcam(std::move(v_free), /*priority*/ 10);

// Cinematic shot at priority 100 with a 1.5s blend in/out.
VirtualCamera v_cine;
v_cine.set_target(cine_dolly_entity);
v_cine.settings().fov_y = 0.5F;
v_cine.blend_in (1.5F);
v_cine.blend_out(1.5F);
const auto cine_id = brain.add_vcam(std::move(v_cine), /*priority*/ 100);

// Per-frame:
cd::camera::Camera live_cam = brain.tick(dt, target_pos_fn);
renderer.set_camera(live_cam);

// To return to free-look:
brain.remove_vcam(cine_id);
```

**Why a `TargetPositionFn` callback instead of a scene pointer?**

The brief lists `cd::scene` as a dependency, but the brain only ever needs *target world position* - never the rest of the scene graph. Injecting a `std::function<Vec3f(Entity)>` keeps the dependency arrow flowing upward through `sample/editor -> game::camera` instead of forcing every consumer of this library to link the scene graph. The sample / editor wraps `scene.world_transform(e)` in a 3-line lambda at the call site; the unit tests inject a hash-map stub. `cd::scene::SceneCameraController` is the high-level facade that closes the loop.

**Test command**: `ctest --preset ninja-debug -R cd_test_game_camera --output-on-failure`.

**Design references**:

- Unity Technologies, "Cinemachine - Virtual Camera + Brain" - Unity Documentation.
- Bourg & Bystedt, *Game Programming Gems 4*, ch. 1.10 "A Critically Damped, Tunable Spring".
- Half-Life 2 / Source Engine SDK - third-person follow camera with critically-damped springs.
- ID Software, Doom (2016) GDC presentation - "Smooth follow" critically-damped per-axis.
