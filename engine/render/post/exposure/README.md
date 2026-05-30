# cd::post_exposure

**Purpose**: Reinhard 2002 log-avg luminance auto-exposure + EMA smoothing. Pure-CPU kernel that produces the linear exposure multiplier the composite pass pre-multiplies the HDR sample by. Architectural fix for the scene-by-scene exposure-tweaking pain that surfaced through phases 449-450 (bloom threshold + exposure default 3.0→1.0).

**Namespace**: `cd::post::exposure`.

**Headers**: `cd/post/exposure/Exposure.hpp`.

**API (4 free functions + Settings)**:
- `cd::post::exposure::Settings` — key (0.18 grey target), min/max EV clamp, adapt_speed_up/down (EMA time-constants).
- `log_avg_luminance(rgba_pixels, n)` — reference CPU reducer for tests. The production producer is a GPU compute mip-chain (queued for the composite-pass companion).
- `compute_target_ev(log_avg_lum, prev_ev, s)` — Reinhard target EV in stops.
- `apply_smoothing(prev_ev, target_ev, dt, s)` — EMA blend with separate up/down speeds (eye contracts faster than it dilates).
- `compute_exposure_multiplier(ev, s)` — EV → linear multiplier = 2^EV / key.
- `update_ev(log_avg_lum, prev_ev, dt, s)` — convenience one-shot composing target + smoothing.

**Pipeline (per frame)**:
1. GPU mip-downsample of the HDR target produces ONE float = avg `log2(luminance(c))` (queued).
2. CPU readback (or push-constant) feeds that float to `update_ev(...)`.
3. `compute_exposure_multiplier(ev, s)` returns the scalar the composite pass writes into `pc.fx.y` (replacing the hardcoded slider).
4. composite tonemap maps the now-normalised scene to display.

**Phase 454 scope** (this library):
- The math: log-avg reducer, EV calc, EMA blend, multiplier conversion.
- 12 unit tests covering grey-scene EV=0, bright/dark scene targets, clamping, dt=0, time-constant approach, skip sentinel.

**Out of Phase 454** (queued for the composite-pass companion):
- GPU compute log-luminance reduction
- CPU-readback or push-constant plumbing into composite
- Bloom threshold scaling with EV (so bloom auto-tunes to scene EV too)

**Usage**:
```cpp
#include <cd/post/exposure/Exposure.hpp>

cd::post::exposure::Settings s {};
float ev = 0.0F;  // carried frame-to-frame

// per frame:
const float la = gpu_reduce_log_luminance(hdr_target);   // queued GPU pass
ev = cd::post::exposure::update_ev(la, ev, dt_seconds, s);
const float exposure = cd::post::exposure::compute_exposure_multiplier(ev, s);
// push exposure into pc.fx.y for the composite pass.
```

**Test command**: `ctest --preset ninja-debug -R cd_test_exposure --output-on-failure`.

**Notes**:
- EMA time constant τ = 1/speed; after 5τ the EMA reaches ~99% of the target. Default speeds 2.0 (contract) / 1.0 (dilate) give τ = 0.5s/1s — game-snappy.
- The key value 0.18 matches photographic mid-grey and the rest of the PBR pipeline (Reinhard 2002).
- This kernel is intentionally GPU-free so the algorithm is unit-testable + the GPU integration can land independently per backend (Vulkan first, D3D12 / Metal later).
