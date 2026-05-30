# ADR-20260530 — Auto-Exposure GPU Pipeline (Reinhard log-avg + EMA)

- **Status**: Accepted (companion to phase 454 cd::post_exposure CPU kernel)
- **Date**: 2026-05-30
- **Branch**: dev
- **Related**: phases 449 (bloom defaults), 450 (exposure 3.0→1.0), 454 (CPU exposure kernel), ADR-20260530-ui-widget-library

## 1. Bağlam (Context)

Phase 449'da bloom threshold 1.0→3.0, phase 450'de exposure 3.0→1.0 yapıldı.
Bunların ikisi de **manual ayarlama**: sahne içerdiğine bakıp threshold/exposure'ı
elde tutuyoruz. Kullanıcı verbatim geri bildirimi:

> "ama problem her yeni ortamn olusturdugunda surekli yeniden ince ayar
> cekme durumu problem. yeni bir cisim koysam yeniden kodmu yazcam bu yanlis."

Bu mimari bir hata. Hem bloom hem exposure **sahnenin EV'sine göre otomatik
ayarlanmalı**, slider veya magic-number ile değil.

Phase 454 ile CPU kernel (log-avg luminance + EMA smoothing + EV→multiplier
conversion) shippped + 12 test geçti. GPU reduction pass + composite hook
işin GERÇEK production kısmı; bu ADR onun mimarisini netleştirir.

## 2. Karar (Decision)

3-pass GPU pipeline + 1 CPU step:

```
[per frame, after HDR scene pass]
┌─────────────────────────────────────────────────────────────────┐
│ Pass 1 (compute): log-luminance partial reduction                │
│   input:  cd_hdr_color (R16G16B16A16_SFLOAT)                     │
│   output: partial_lum_buf (std430, N float)                      │
│   shader: 8x8 workgroup -> 1 partial per group                   │
└─────────────────────────────────────────────────────────────────┘
              ↓
┌─────────────────────────────────────────────────────────────────┐
│ Pass 2 (compute): final reduction                                │
│   input:  partial_lum_buf                                        │
│   output: avg_lum_buf (1 float = avg log2(luma))                 │
│   shader: 256x1 workgroup -> sequential reduce                   │
└─────────────────────────────────────────────────────────────────┘
              ↓
┌─────────────────────────────────────────────────────────────────┐
│ Pass 3 (CPU readback OR persistent map):                         │
│   read avg_lum -> feed into                                      │
│   cd::post::exposure::update_ev(la, prev_ev, dt, settings)       │
│   prev_ev carries frame-to-frame (CPU state).                    │
└─────────────────────────────────────────────────────────────────┘
              ↓
┌─────────────────────────────────────────────────────────────────┐
│ Composite pass (existing, modified):                             │
│   pc.fx[1] = compute_exposure_multiplier(ev, settings)           │
│   (replaces the hard-coded fx.exposure UI slider value)          │
└─────────────────────────────────────────────────────────────────┘
```

### 2.1 Pass 1 — partial reduction shader (Karis 2014 pattern)

```glsl
#version 460
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 0) uniform sampler2D cd_hdr;
layout(set = 0, binding = 1, std430) writeonly buffer Partial { float v[]; };

shared float sm[64];

float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

void main() {
    ivec2 res = textureSize(cd_hdr, 0);
    vec2 uv = (vec2(gl_GlobalInvocationID.xy) + 0.5) / vec2(res);
    vec3 c = texture(cd_hdr, uv).rgb;
    float l = max(luma(c), 1e-6);
    sm[gl_LocalInvocationIndex] = log2(l);
    barrier();
    // tree reduction 64 -> 1
    for (uint s = 32u; s > 0u; s >>= 1u) {
        if (gl_LocalInvocationIndex < s)
            sm[gl_LocalInvocationIndex] += sm[gl_LocalInvocationIndex + s];
        barrier();
    }
    if (gl_LocalInvocationIndex == 0u) {
        uint group_idx = gl_WorkGroupID.y * gl_NumWorkGroups.x + gl_WorkGroupID.x;
        v[group_idx] = sm[0] / 64.0;
    }
}
```

### 2.2 Pass 2 — final reduction

```glsl
#version 460
layout(local_size_x = 256) in;
layout(set = 0, binding = 0, std430) readonly buffer In { float v[]; };
layout(set = 0, binding = 1, std430) writeonly buffer Out { float avg; };

shared float sm[256];

void main() {
    uint i = gl_GlobalInvocationID.x;
    uint n = v.length();
    sm[gl_LocalInvocationIndex] = (i < n) ? v[i] : 0.0;
    barrier();
    for (uint s = 128u; s > 0u; s >>= 1u) {
        if (gl_LocalInvocationIndex < s)
            sm[gl_LocalInvocationIndex] += sm[gl_LocalInvocationIndex + s];
        barrier();
    }
    if (gl_LocalInvocationIndex == 0u) avg = sm[0] / float(n);
}
```

For 1920×1080 HDR target: 240×135 groups → 32400 partials. Pass 2 reduces in
~130 invocations. Total compute ≈ negligible (~0.2ms on RTX-class).

### 2.3 CPU readback

```cpp
// per frame end
auto readback = dev.read_buffer<float>(state.avg_lum_buf);
const float la = readback.has_value() ? *readback : Settings::kSkipThreshold;
state.ev = cd::post::exposure::update_ev(la, state.ev, dt, settings);

// composite push-constant fill
cp.fx[1] = cd::post::exposure::compute_exposure_multiplier(state.ev, settings);
```

CPU readback has 1-frame latency (prev frame's HDR drives current frame's EV).
Acceptable for visual quality; auto-exposure adapts on the order of seconds
anyway.

### 2.4 Bloom threshold auto-scale

Once EV is available, bloom prefilter threshold scales:

```glsl
// inside cd::post::bloom prefilter compute / FS:
float ev_scaled_threshold = pc.params.x * exp2(-ev);   // pre-divide by exposure
// then existing soft-knee logic uses ev_scaled_threshold instead of pc.params.x
```

This way a "threshold 1.0" really means "1 stop over scene mean" regardless
of EV. The user's tuning becomes scene-agnostic.

## 3. Reddedilen alternatifler

1. **Histogram-based exposure (Lottes 2016, Unreal 4)**: 256-bin log
   histogram + percentile-driven EV. More sophisticated but ~3× compute
   + needs subgroup support. Reinhard 2002 mean is good enough for Phase 1;
   percentile mode is a future v2 upgrade.

2. **Per-pixel exposure (local tonemap)**: defeats the purpose of having
   ONE exposure that all PBR math is calibrated against. Use composite
   local-contrast enhancement instead (separate pass).

3. **Manual exposure forever**: regresses to phase 450 state. User
   explicitly objects (verbatim quote in §1).

4. **CPU readback as render dependency** (frame N reads frame N's
   reduction): forces a GPU→CPU sync mid-frame, killing pipelining.
   1-frame latency is industry-standard.

## 4. Sonuçlar (Consequences)

**Olumlu**:
- Bloom + exposure auto-scale with scene EV → user doesn't tweak per scene.
- The fixed `fx.exposure` UI slider becomes optional bias (Exposure
  Compensation ±N stops), like a real camera. Default 0 = auto.
- Same tonemap output range across day/night/dim/bright scenes →
  consistent visual quality across drag-dropped glTFs.

**Olumsuz**:
- ~0.2ms GPU pass + readback latency.
- Two extra compute shaders to maintain (cross-API SPIRV-Cross
  translation needed for D3D12/Metal backends).
- Frame-to-frame state (prev_ev) introduces test fragility — golden-image
  tests must wait N frames for EV to settle before snapshotting.

**Migration**:
- Phase 461 (workflow Wave 2) ships the GPU pass skeleton.
- Phase 462/463 will wire into composite + add bloom threshold scaling.
- Phase 450's fixed `fx.exposure=1.0` becomes the manual override slider;
  default 0.0 = auto.

## 5. İlgili kaynaklar

- Reinhard, Stark, Shirley, Ferwerda 2002. Photographic Tone
  Reproduction for Digital Images. SIGGRAPH 2002.
- Karis 2014. Tone Mapping (SIGGRAPH course notes).
- Lottes 2016. Advanced Techniques and Optimization of HDR Color
  Pipelines. GDC 2016.
- Unreal Engine 4 Eye Adaptation (open-source).
