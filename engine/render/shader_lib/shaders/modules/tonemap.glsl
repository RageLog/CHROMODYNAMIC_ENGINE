// =============================================================================
// cd::shader_lib — tonemap.glsl
// Canonical tonemap operators (the phase1128 inventory counted operator
// copies across 34 mention sites). All operators map linear scene-referred
// RGB to display-referred [0,1] and are deliberately gamma-agnostic — the
// caller applies the output transfer function.
//
// References: Reinhard 2002; Narkowicz 2016 (ACES fit); Hill/Day ACES
// fitted matrices (UE4/Unity SRP lineage); Uchimura 2017 (GT tonemap).
// =============================================================================
#ifndef CD_SL_TONEMAP_GLSL
#define CD_SL_TONEMAP_GLSL

#include <cd/shader_lib/math_common.glsl>

/// Classic Reinhard, per-channel.
vec3 cd_tonemap_reinhard(vec3 c)
{
    return c / (vec3(1.0) + c);
}

/// Extended Reinhard with configurable white point (Lwhite).
vec3 cd_tonemap_reinhard_ext(vec3 c, float l_white)
{
    float w2 = max(l_white * l_white, CD_EPSILON);
    return c * (vec3(1.0) + c / w2) / (vec3(1.0) + c);
}

/// Narkowicz 2016 ACES filmic approximation — the cheap, ubiquitous fit.
vec3 cd_tonemap_aces_narkowicz(vec3 c)
{
    const float a = 2.51;
    const float b = 0.03;
    const float d = 2.43;
    const float e = 0.59;
    const float f = 0.14;
    return cd_saturate3((c * (a * c + b)) / (c * (d * c + e) + f));
}

/// Hill/Day fitted ACES (RRT+ODT matrices) — closer to reference ACES
/// than the Narkowicz fit at a few more ALU.
vec3 cd_tonemap_aces_fitted(vec3 c)
{
    const mat3 kInput = mat3(
        0.59719, 0.07600, 0.02840,
        0.35458, 0.90834, 0.13383,
        0.04823, 0.01566, 0.83777);
    const mat3 kOutput = mat3(
         1.60475, -0.10208, -0.00327,
        -0.53108,  1.10813, -0.07276,
        -0.07367, -0.00605,  1.07602);
    vec3 v = kInput * c;
    vec3 a = v * (v + 0.0245786) - 0.000090537;
    vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
    return cd_saturate3(kOutput * (a / b));
}

/// Uchimura 2017 "Gran Turismo" operator with the published defaults
/// (P=1, a=1, m=0.22, l=0.4, c=1.33, b=0).
vec3 cd_tonemap_uchimura(vec3 x)
{
    const float P = 1.0;
    const float a = 1.0;
    const float m = 0.22;
    const float l = 0.4;
    const float c = 1.33;
    const float b = 0.0;

    float l0 = ((P - m) * l) / a;
    float S0 = m + l0;
    float S1 = m + a * l0;
    float C2 = (a * P) / (P - S1);
    float CP = -C2 / P;

    vec3 w0 = vec3(1.0) - smoothstep(vec3(0.0), vec3(m), x);
    vec3 w2 = step(vec3(m + l0), x);
    vec3 w1 = vec3(1.0) - w0 - w2;

    vec3 T = m * pow(x / m, vec3(c)) + vec3(b);
    vec3 S = vec3(P) - (P - S1) * exp(CP * (x - S0));
    vec3 L = vec3(m) + a * (x - vec3(m));

    return T * w0 + L * w1 + S * w2;
}

#endif  // CD_SL_TONEMAP_GLSL
