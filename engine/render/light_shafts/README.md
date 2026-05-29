# cd::light_shafts

**Purpose**: radial sun-shaft / god-rays effect via screen-space occlusion sampling. Samples along the screen-space ray from each pixel back toward the projected sun position, accumulates occluder darkness, blooms the result into the composite.

**Namespace**: `cd::light_shafts`.

**Headers**: `cd/light_shafts/{Settings,Shafts}.hpp`.

**Primary types**:
- `cd::light_shafts::Settings` -- { strength, density, decay, weight, exposure, sample_count }.
- `cd::light_shafts::sample_shafts(uv, sun_screen_pos, depth_view) -> rgb`.

**Test command**: `ctest --preset ninja-debug -R cd_test_light_shafts --output-on-failure`.

**Notes**:
- Composite-pass inline.
- hello_engine R-Showcase exposes `fx.shafts_strength` (W4-E default 0.75F so they are clearly visible on first run).
- Sun position must be projected to screen-space before the sample dispatch; off-screen sun produces minimal contribution by design.
