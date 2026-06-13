// =============================================================================
// cd::gluon — color_space.glsl
// P0-A wave 2 (ADR-20260613-gluon-module-manifest §2.3, row (a) color_space):
// linear/sRGB transfer, luminance, RGB↔YCoCg, Kelvin→RGB, exposure/EV.
//
// SOURCE / DERIVATION:
//   * cd_srgb_to_linear / cd_linear_to_srgb — VERBATIM (math) from the
//     engine's DOMINANT exact-sRGB copy in
//     engine/ui/widgets/include/cd/ui/widgets/ColorPicker.hpp:49-67
//     (consumed by ColorPicker.cpp:84 srgb_to_linear / :105 linear_to_srgb).
//     Same breakpoints 0.04045 / 0.0031308, same 12.92 / 1.055 / 0.055 / 2.4
//     constants — a future migration is a pure rename, pixel-identical.
//   * cd_linear_srgb_to_rec2020 — VERBATIM (math) from the DOMINANT GLSL
//     copy in engine/render/hdr_display/include/cd/hdr_display/HdrDisplay.hpp:90
//     (kHdrGlsl cd_linear_srgb_to_rec2020 — Rec.709→Rec.2020 primaries mat3).
//   * cd_cct_to_linear_rgb — VERBATIM (math) from the DOMINANT CPU copy in
//     engine/render/light/include/cd/light/ColorTemperature.hpp:40-87
//     (CIE-1931 CCT→xy piecewise fit + Lindbloom XYZ→linear-sRGB; clamp
//     1000-15000 K, max(0) gamut guard). Renamed cd_ only.
//   * cd_luminance / cd_luminance_601 — SOTA-standard (no DOMINANT GLSL copy:
//     manifest §1.3 lists luminance only as scattered CPU FLIP/SSIM). Rec.709
//     (0.2126, 0.7152, 0.0722) + Rec.601 (0.299, 0.587, 0.114) primaries.
//   * cd_rgb_to_ycocg / cd_ycocg_to_rgb — SOTA-standard (no engine copy —
//     manifest §1.3 grep BOŞ). Reversible-lifting YCoCg (TAA/edge-aware blur).
//   * cd_exposure / cd_ev100_to_exposure — SOTA-standard exposure helpers
//     (linear * 2^EV; EV100→exposure = 1/(1.2·2^EV) saturation-based).
//
// RULES (ADR-20260612 §2.2): may ONLY #include math_common.glsl; pure fns.
// =============================================================================
#ifndef CD_GLUON_COLOR_SPACE_GLSL
#define CD_GLUON_COLOR_SPACE_GLSL

#include <cd/gluon/math_common.glsl>

// --- sRGB transfer (exact piecewise; ColorPicker DOMINANT) -------------------

/// Linearise a single sRGB channel (inverse gamma, exact piecewise).
float cd_srgb_to_linear(float c) {
    return (c <= 0.04045) ? (c / 12.92)
                          : pow((c + 0.055) / 1.055, 2.4);
}

/// Apply the sRGB gamma to a single linear channel (exact piecewise).
float cd_linear_to_srgb(float c) {
    c = cd_saturate(c);
    return (c <= 0.0031308) ? (c * 12.92)
                            : (1.055 * pow(c, 1.0 / 2.4) - 0.055);
}

/// Per-channel sRGB→linear over an RGB triple.
vec3 cd_srgb_to_linear3(vec3 c) {
    return vec3(cd_srgb_to_linear(c.r),
                cd_srgb_to_linear(c.g),
                cd_srgb_to_linear(c.b));
}

/// Per-channel linear→sRGB over an RGB triple.
vec3 cd_linear_to_srgb3(vec3 c) {
    return vec3(cd_linear_to_srgb(c.r),
                cd_linear_to_srgb(c.g),
                cd_linear_to_srgb(c.b));
}

// --- Wide-gamut primaries (HdrDisplay DOMINANT) ------------------------------

/// Linear Rec.709 (sRGB primaries) → linear Rec.2020 primaries.
vec3 cd_linear_srgb_to_rec2020(vec3 c) {
    return mat3(0.6274, 0.0691, 0.0164,
                0.3293, 0.9195, 0.0880,
                0.0433, 0.0114, 0.8956) * c;
}

// --- Luminance ---------------------------------------------------------------

/// Rec.709 relative luminance of a linear RGB colour.
float cd_luminance(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

/// Rec.601 (NTSC) luma of an RGB colour.
float cd_luminance_601(vec3 c) {
    return dot(c, vec3(0.299, 0.587, 0.114));
}

// --- RGB ↔ YCoCg (reversible lifting) ----------------------------------------

/// RGB → YCoCg (luma, orange-chroma, green-chroma); the reversible
/// lifting form used for edge-aware / TAA chroma clamping.
vec3 cd_rgb_to_ycocg(vec3 c) {
    float co = c.r - c.b;
    float t  = c.b + co * 0.5;
    float cg = c.g - t;
    float y  = t + cg * 0.5;
    return vec3(y, co, cg);
}

/// YCoCg → RGB (exact inverse of cd_rgb_to_ycocg).
vec3 cd_ycocg_to_rgb(vec3 ycocg) {
    float t = ycocg.x - ycocg.z * 0.5;
    float g = ycocg.z + t;
    float b = t - ycocg.y * 0.5;
    float r = b + ycocg.y;
    return vec3(r, g, b);
}

// --- Colour temperature (Kelvin → linear sRGB; ColorTemperature DOMINANT) ----

/// Correlated colour temperature (Kelvin) → linear sRGB. CIE-1931 xy
/// piecewise fit then Lindbloom XYZ→linear-sRGB; clamped 1000-15000 K,
/// negative-gamut channels clamped to 0.
vec3 cd_cct_to_linear_rgb(float kelvin) {
    float T  = clamp(kelvin, 1000.0, 15000.0);
    float T2 = T * T;
    float T3 = T2 * T;

    float x;
    if (T <= 4000.0)
        x = -0.2661239e9 / T3 - 0.2343589e6 / T2 + 0.8776956e3 / T + 0.179910;
    else
        x = -3.0258469e9 / T3 + 2.1070379e6 / T2 + 0.2226347e3 / T + 0.240390;

    float x2 = x * x;
    float x3 = x2 * x;
    float y;
    if (T <= 2222.0)
        y = -1.1063814  * x3 - 1.34811020 * x2 + 2.18555832 * x - 0.20219683;
    else if (T <= 4000.0)
        y = -0.9549476  * x3 - 1.37418593 * x2 + 2.09137015 * x - 0.16748867;
    else
        y =  3.0817580  * x3 - 5.87338670 * x2 + 3.75112997 * x - 0.37001483;

    float X = x / max(1e-6, y);
    float Z = (1.0 - x - y) / max(1e-6, y);

    float r =  3.2404542 * X - 1.5371385 - 0.4985314 * Z;
    float g = -0.9692660 * X + 1.8760108 + 0.0415560 * Z;
    float b =  0.0556434 * X - 0.2040259 + 1.0572252 * Z;

    return max(vec3(0.0), vec3(r, g, b));
}

// --- Exposure / EV -----------------------------------------------------------

/// Apply a linear exposure scale expressed in stops (EV): c · 2^ev.
vec3 cd_exposure(vec3 c, float ev) {
    return c * exp2(ev);
}

/// Convert an EV100 value to a linear exposure multiplier
/// (saturation-based: 1 / (1.2 · 2^EV100)), the Lagarde/Frostbite form.
float cd_ev100_to_exposure(float ev100) {
    return 1.0 / (1.2 * exp2(ev100));
}

#endif  // CD_GLUON_COLOR_SPACE_GLSL
