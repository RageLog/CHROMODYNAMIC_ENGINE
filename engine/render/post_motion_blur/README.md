# cd::post_motion_blur

**Purpose**: per-mesh + camera motion blur using the velocity G-buffer. Stochastic sample reconstruction across N taps along the velocity vector; samples are weighted by depth-test passes so foreground objects do not bleed onto static backgrounds.

**Namespace**: `cd::post_motion_blur`.

**Headers**: `cd/post_motion_blur/{Settings,Reconstruct}.hpp`.

**Primary types**:
- `cd::post_motion_blur::Settings` -- runtime knobs: { strength, tap_count, depth_compare_threshold }.
- `cd::post_motion_blur::reconstruct(velocity_view, sample_count) -> rgb` -- shader-side reconstruction function.

**Test command**: `ctest --preset ninja-debug -R cd_test_post_motion_blur --output-on-failure`.

**Notes**:
- Composite-pass shader inlines the reconstruction; this library provides Settings + GLSL string-views.
- hello_engine R-Showcase exposes `fx.motion_blur` (0.0 default; user-opt-in).
- Per-mesh blur requires the cd::velocity G-buffer pass to be active for the current frame.
