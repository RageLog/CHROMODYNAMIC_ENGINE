# cd::post_gtao

**Purpose**: ground-truth ambient occlusion via temporally-stable horizon sampling (Jimenez 2016). Composite-pass inline AO that reads depth + normal from the G-buffer and produces crease darkening + ambient occlusion factor.

**Namespace**: `cd::post_gtao`.

**Headers**: `cd/post_gtao/{Settings,Sample}.hpp`.

**Primary types**:
- `cd::post_gtao::Settings` -- { strength, max_radius, sample_count, temporal_blend }.
- `cd::post_gtao::sample(uv, depth_view, normal_view) -> float` -- shader-side AO query.

**Test command**: `ctest --preset ninja-debug -R cd_test_post_gtao --output-on-failure`.

**Notes**:
- Composite-pass inline (not a separate dispatch); samples scene depth + gbuf_normal under the AO knob.
- hello_engine R-Showcase exposes `fx.ao_strength` (0.55F default).
- Temporal blend gated on TAA being active (sample_count * frame jitter -> stable AO without flicker).
