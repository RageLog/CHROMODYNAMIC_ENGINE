// =============================================================================
// cd::gluon — shadow_filtering.glsl
// P0-A wave 2 (ADR-20260613-gluon-module-manifest §2.3, row (f)
// shadow_filtering): PCF depth-compare filters + slope-scaled bias +
// cascade select.
//
// SOURCE / DERIVATION:
//   * cd_shadow_slope_bias — VERBATIM (math) from the engine's DOMINANT
//     slope-scaled bias in samples/engine/hello_engine/shaders/prim.frag.glsl
//     sample_shadow (:422): max(0.0003 · (1 - max(N·L,0)), 0.00005). The
//     phase451-csm tuned coefficients are kept byte-for-math identical.
//   * cd_pcf_shadow_3x3 — VERBATIM (math) from the SAME DOMINANT sample_shadow
//     (:404-431): perspective divide, frustum-clip → fully-lit, NDC→uv
//     (Vulkan y-down, no flip), texelSize = 1/textureSize, 3×3 hard-compare
//     loop averaged /9. This is the future migration target of prim.frag —
//     it MUST stay byte-identical, so the body is lifted verbatim and the
//     `cd_shadow_map` global was promoted to a `sampler2D` PARAMETER (GLSL
//     allows opaque sampler arguments) so the module stays self-contained.
//   * cd_pcf_shadow_5x5 — SOTA-standard 5×5 widening of the same hard-compare
//     PCF kernel (averaged /25) for a softer penumbra.
//   * cd_pcf_shadow_poisson — SOTA-standard 12-tap Poisson-disk PCF (rotated
//     by a per-fragment angle for spatial dithering of the kernel).
//   * cd_shadow_cascade_select — SOTA-standard CSM split selector: returns the
//     first cascade whose far split exceeds the view-space depth (the standard
//     "pick the tightest cascade" rule; mirrors the engine CSM split layout).
//
// RULES (ADR-20260612 §2.2): may ONLY #include math_common.glsl; pure fns.
//   Shadow filters take the depth `sampler2D` as a function parameter so the
//   module never references a global binding (GLSL opaque-type argument).
// =============================================================================
#ifndef CD_GLUON_SHADOW_FILTERING_GLSL
#define CD_GLUON_SHADOW_FILTERING_GLSL

#include <cd/gluon/math_common.glsl>

// --- Slope-scaled depth bias (prim.frag DOMINANT) ----------------------------

/// Slope-scaled shadow bias: grows with the grazing angle (1 - N·L) to fight
/// acne, floored so head-on fragments still receive a minimum offset.
/// Coefficients are the phase451-csm tuned values (slope 0.0003, floor 5e-5).
float cd_shadow_slope_bias(vec3 n, vec3 l) {
    return max(0.0003 * (1.0 - max(dot(n, l), 0.0)), 0.00005);
}

// --- 3×3 PCF (prim.frag sample_shadow VERBATIM) ------------------------------

/// 3×3 PCF hard-compare shadow factor. Returns 1.0 = fully lit, 0.0 = fully
/// occluded. `sp` is the light-space clip position; bias is slope-scaled by
/// N and L. Vulkan clip x,y ∈ [-1,1], depth ∈ [0,1]; texture v is y-down
/// (matches Vulkan clip y after perspective divide — no flip).
float cd_pcf_shadow_3x3(sampler2D shadow_map, vec4 sp, vec3 n, vec3 l) {
    vec3 p = sp.xyz / sp.w;
    if (p.x < -1.0 || p.x > 1.0 || p.y < -1.0 || p.y > 1.0 ||
        p.z < 0.0 || p.z > 1.0) return 1.0;
    vec2 uv = p.xy * 0.5 + 0.5;
    float bias = cd_shadow_slope_bias(n, l);
    float ref  = p.z - bias;
    vec2 ts = 1.0 / vec2(textureSize(shadow_map, 0));
    float s = 0.0;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            float d = texture(shadow_map, uv + vec2(float(dx), float(dy)) * ts).r;
            s += (d < ref) ? 0.0 : 1.0;
        }
    return s / 9.0;
}

// --- 5×5 PCF (SOTA widening) --------------------------------------------------

/// 5×5 PCF hard-compare shadow factor (softer penumbra than 3×3). Same
/// frustum-clip / bias / NDC→uv conventions as cd_pcf_shadow_3x3.
float cd_pcf_shadow_5x5(sampler2D shadow_map, vec4 sp, vec3 n, vec3 l) {
    vec3 p = sp.xyz / sp.w;
    if (p.x < -1.0 || p.x > 1.0 || p.y < -1.0 || p.y > 1.0 ||
        p.z < 0.0 || p.z > 1.0) return 1.0;
    vec2 uv = p.xy * 0.5 + 0.5;
    float ref = p.z - cd_shadow_slope_bias(n, l);
    vec2 ts = 1.0 / vec2(textureSize(shadow_map, 0));
    float s = 0.0;
    for (int dy = -2; dy <= 2; ++dy)
        for (int dx = -2; dx <= 2; ++dx) {
            float d = texture(shadow_map, uv + vec2(float(dx), float(dy)) * ts).r;
            s += (d < ref) ? 0.0 : 1.0;
        }
    return s / 25.0;
}

// --- Poisson-disk PCF (SOTA) -------------------------------------------------

/// 12-tap Poisson-disk PCF, the disk rotated per-fragment by `rotation`
/// (radians) to trade banding for noise. `radius` scales the disk in texels.
/// Same frustum-clip / bias / NDC→uv conventions as cd_pcf_shadow_3x3.
float cd_pcf_shadow_poisson(sampler2D shadow_map, vec4 sp, vec3 n, vec3 l,
                            float radius, float rotation) {
    vec3 p = sp.xyz / sp.w;
    if (p.x < -1.0 || p.x > 1.0 || p.y < -1.0 || p.y > 1.0 ||
        p.z < 0.0 || p.z > 1.0) return 1.0;
    vec2 uv = p.xy * 0.5 + 0.5;
    float ref = p.z - cd_shadow_slope_bias(n, l);
    vec2 ts = 1.0 / vec2(textureSize(shadow_map, 0));

    const vec2 kDisk[12] = vec2[12](
        vec2(-0.326, -0.406), vec2(-0.840, -0.074), vec2(-0.696,  0.457),
        vec2(-0.203,  0.621), vec2( 0.962, -0.195), vec2( 0.473, -0.480),
        vec2( 0.519,  0.767), vec2( 0.185, -0.893), vec2( 0.507,  0.064),
        vec2( 0.896,  0.412), vec2(-0.322, -0.933), vec2(-0.792, -0.598));

    float cs = cos(rotation);
    float sn = sin(rotation);
    mat2 rot = mat2(cs, -sn, sn, cs);

    float s = 0.0;
    for (int i = 0; i < 12; ++i) {
        vec2 o = (rot * kDisk[i]) * radius * ts;
        float d = texture(shadow_map, uv + o).r;
        s += (d < ref) ? 0.0 : 1.0;
    }
    return s / 12.0;
}

// --- Cascade select (SOTA CSM) -----------------------------------------------

/// Pick the tightest cascade for a given positive view-space depth.
/// `splits[i]` is the far view-space depth of cascade i (ascending);
/// returns the first cascade whose far split is ≥ view_depth, clamped to
/// the last cascade. `count` ≤ 4.
int cd_shadow_cascade_select(float view_depth, vec4 splits, int count) {
    for (int i = 0; i < count; ++i) {
        if (view_depth <= splits[i]) return i;
    }
    return count - 1;
}

#endif  // CD_GLUON_SHADOW_FILTERING_GLSL
