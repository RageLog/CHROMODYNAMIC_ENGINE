// =============================================================================
// cd::gluon — color_grading.glsl
// P1 wave 2 (ADR-20260613-gluon-module-manifest §2.1(h) color_grading):
// the artist-facing grading primitives (lift/gamma/gain, contrast,
// saturation, white-balance, channel-mixer) the engine does NOT yet have in
// a canonical GLSL form. SOTA-standard implementation.
//
// RULES (manifest §2.2): may ONLY #include math_common.glsl. In particular
// it does NOT #include color_space.glsl even though that module defines
// cd_luminance — the luminance helper needed by cd_saturation is defined
// LOCALLY here under a conflict-free `cd_cg_*` prefix so this module composes
// with color_space in the same shader without a redefinition clash.
//
// References:
//   * Lagarde & de Rousiers 2014 — "Moving Frostbite to PBR" (the
//     lift/gamma/gain + white-balance grading model).
//   * ASC CDL (American Society of Cinematographers Color Decision List) —
//     the slope/offset/power form behind lift-gamma-gain.
//   * Bradford / CAT02 chromatic-adaptation — the LMS white-balance basis.
// =============================================================================
#ifndef CD_GLUON_COLOR_GRADING_GLSL
#define CD_GLUON_COLOR_GRADING_GLSL

#include <cd/gluon/math_common.glsl>

// Local Rec.709 luminance (manifest §2.2: do NOT include color_space.glsl;
// the cd_cg_ prefix keeps it collision-free if color_space is also pulled in
// by the consuming shader). Pure helper — not part of the public grading API.
float cd_cg_luminance(vec3 c)
{
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

/// Lift / Gamma / Gain (ASC-CDL slope-offset-power lineage). `lift` shifts
/// the shadows (additive, scaled toward white), `gain` scales the highlights
/// (multiplicative), `gamma` reshapes the midtones (per-channel power).
/// `c` is linear scene-referred RGB.
vec3 cd_lift_gamma_gain(vec3 c, vec3 lift, vec3 gamma, vec3 gain)
{
    // Lerp toward (lift) in the lows, scale by gain, then apply the
    // mid-tone power. The (2-lift) form keeps a neutral lift of 1.0.
    vec3 v = c * (vec3(1.5) - 0.5 * lift) + 0.5 * lift - 0.5;
    v = cd_saturate3(v) * gain;
    return pow(max(v, vec3(0.0)), vec3(1.0) / max(gamma, vec3(CD_EPSILON)));
}

/// Pivot-based contrast. `pivot` is the mid-grey anchor (typically ~0.18
/// linear or 0.5 for perceptual). Values above the pivot are pushed up,
/// below pushed down, by `contrast` (1 = identity).
vec3 cd_contrast(vec3 c, float contrast, float pivot)
{
    return (c - vec3(pivot)) * contrast + vec3(pivot);
}

/// Luminance-centred saturation. `saturation` of 0 collapses to greyscale,
/// 1 is identity, >1 boosts. Uses a LOCAL Rec.709 luminance (cd_cg_*) so
/// this module stays math_common-only (manifest §2.2).
vec3 cd_saturation(vec3 c, float saturation)
{
    float y = cd_cg_luminance(c);
    return mix(vec3(y), c, saturation);
}

/// White balance via temperature + tint in CAT02 LMS space (Bradford-class
/// chromatic adaptation). `temp` in [-1,1] warms (+) or cools (-); `tint`
/// in [-1,1] biases magenta (+) / green (-). `c` is linear RGB.
vec3 cd_white_balance(vec3 c, float temp, float tint)
{
    // sRGB-linear → CAT02 LMS (von Kries / CIECAM02 cone responses).
    const mat3 kRgbToLms = mat3(
        0.7328,  0.4296, -0.1624,
       -0.7036,  1.6975,  0.0061,
        0.0030,  0.0136,  0.9834);
    const mat3 kLmsToRgb = mat3(
        1.096124, -0.278869, 0.182745,
        0.454369,  0.473533, 0.072098,
       -0.009628, -0.005698, 1.015326);

    // Scale the long/short cones for temperature, medium cone for tint.
    float t = temp * 0.1;
    float g = tint * 0.1;
    vec3 lms_scale = vec3(1.0 + t, 1.0 + g, 1.0 - t);

    vec3 lms = kRgbToLms * c;
    lms *= lms_scale;
    return kLmsToRgb * lms;
}

/// Channel mixer — each output channel is a weighted sum of the input RGB
/// (rows of a 3×3 mix matrix). `r_mix`/`g_mix`/`b_mix` are the per-output
/// weight rows; (1,0,0)/(0,1,0)/(0,0,1) is identity.
vec3 cd_channel_mixer(vec3 c, vec3 r_mix, vec3 g_mix, vec3 b_mix)
{
    return vec3(dot(c, r_mix), dot(c, g_mix), dot(c, b_mix));
}

#endif  // CD_GLUON_COLOR_GRADING_GLSL
