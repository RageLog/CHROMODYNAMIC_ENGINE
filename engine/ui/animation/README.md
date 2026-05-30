# cd::ui_animation

**Purpose**: Pure-C++ property animation, easing curves, and multi-channel timeline for retained-mode UI widgets. Phase 3.2 of ADR-20260530-ui-widget-library -- gives widgets, theme transitions, and HUD overlays a single shared tweening primitive without coupling to the renderer or input layer.

**Namespace**: `cd::ui::animation`.

**Headers**: `cd/ui/animation/Animation.hpp`.

**Primary types**:
- `cd::ui::animation::Easing` -- enum of curve identifiers: `kLinear`, `kEaseInQuad`, `kEaseOutQuad`, `kEaseInOutQuad`, `kEaseInCubic`, `kEaseOutCubic`, `kEaseInOutCubic`, `kEaseOutBack`, `kEaseOutElastic`, `kEaseOutBounce`.
- `cd::ui::animation::ease(t, curve)` -- free function that maps a normalised `t` in `[0..1]` through the selected Penner-reference curve. Monotonic curves stay in `[0..1]`; back/elastic overshoot is preserved as a feature.
- `cd::ui::animation::Animation<T>` -- declarative tween: `{ from, to, duration_s, easing }`. Trivially copyable; reusable across multiple Tweeners. Generic over float / `cd::math::Vec2f` / `Vec3f` / `Vec4f`.
- `cd::ui::animation::Tweener<T>` -- stateful playhead. `start(anim)` resets, `tick(dt)` advances, `value()` returns the interpolated `T`, `done()` flips true at duration. No heap allocation.
- `cd::ui::animation::Timeline` -- multi-channel float scheduler. `add(at_time_s, anim)` schedules a channel, `tick(dt)` advances the master playhead, `value_for(channel_id)` looks up the current value. `reset()` rewinds to time zero.

**Phase 3.2 scope** (this library):
- 10 easing curves matching the Penner / easings.net reference set.
- `Tweener<T>` for float and `cd::math::Vec*f` (componentwise lerp via operator overloads in `cd::math::Vector`).
- `Timeline` with sequential / overlapping float channels. Pinned at `from` before the scheduled start time; pinned at `to` after the end time.

**Out of Phase 3.2** (Phase 4+):
- Spring physics (critically damped, under/over-damped).
- Path animation along bezier / catmull-rom splines.
- Loop / reverse / ping-pong policies (caller manages today via `Tweener::start`).
- Property binding (curve -> Widget property) -- ADR Phase 3.3.

**Usage**:
```cpp
#include <cd/ui/animation/Animation.hpp>

namespace ani = cd::ui::animation;

// One-shot float tween.
ani::Tweener<float> tw;
ani::Animation<float> a { 0.0F, 100.0F, /*duration_s=*/0.5F, ani::Easing::kEaseOutCubic };
tw.start(a);
while (!tw.done())
{
    tw.tick(dt_s);
    button.set_alpha(tw.value());
}

// Multi-channel timeline for an open/close sequence.
ani::Timeline tl;
auto fade  = tl.add(0.0F, { 0.0F, 1.0F, 0.20F, ani::Easing::kEaseOutQuad });
auto slide = tl.add(0.10F, { -32.0F, 0.0F, 0.25F, ani::Easing::kEaseOutBack });
tl.tick(dt_s);
panel.set_alpha(tl.value_for(fade));
panel.set_offset_y(tl.value_for(slide));
```

**Test command**: `ctest --preset ninja-debug -R cd_test_animation --output-on-failure`. Cross-validates 14 cases (endpoint accuracy, mid-point references, monotonicity sweep, overshoot detection on `kEaseOutBack`, multi-peak detection on `kEaseOutBounce`, Tweener restart, Timeline sequential play, reset, negative-tick guard, zero-duration short-circuit).

**Notes**:
- `ease()` is a free function with a `switch`; the compiler routinely inlines it via LTO when called inside a tight Tweener loop. The function is `noexcept` and free of heap allocation.
- `Tweener<T>::lerp_` is `a + (b - a) * t`. For `cd::math::Vec*f` this resolves to the componentwise operators in `cd/math/Vector.hpp` -- no extra specialisations required.
- `Timeline::value_for` clamps to `from` before the channel starts and `to` after it ends, so callers can poll without bounds checks.
- Robert Penner easing reference values pinned in `test_animation.cpp` match easings.net (which is the de-facto cross-engine reference shared with gsap, Unity Mathf, and CSS `cubic-bezier`).
