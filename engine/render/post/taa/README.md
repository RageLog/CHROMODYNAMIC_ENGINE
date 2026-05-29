# cd::post_taa

**Purpose**: temporal anti-aliasing. Halton-jittered camera + per-mesh velocity G-buffer + history reprojection + neighborhood clamp. Produces stable subpixel detail without the cost of MSAA on G-buffers.

**Namespace**: `cd::post_taa`.

**Headers**: `cd/post_taa/{Halton,Taa,NeighborhoodClamp}.hpp`.

**Primary types**:
- `cd::post_taa::halton_sequence(i, base)` -- low-discrepancy jitter sequence (Halton-2 + Halton-3).
- `cd::post_taa::compute_jitter(frame_idx, extent)` -- pixel-space jitter to bake into the projection matrix every frame.
- `cd::post_taa::neighborhood_clamp(history, current_min, current_max)` -- 3x3 neighborhood YCoCg clamp; eliminates ghosting on rapid camera motion.

**Test command**: `ctest --preset ninja-debug -R cd_test_post_taa --output-on-failure`.

**Notes**:
- TAA blend lives in the composite fragment shader (cd::post_composite); this library only provides the jitter + clamp math.
- Per-mesh velocity reprojection requires the cd::velocity G-buffer pass.
- hello_engine exposes the TAA blend factor via the R-Showcase HelloEngineFx aggregate (taa_amount, default 0.0F until user opts in).
