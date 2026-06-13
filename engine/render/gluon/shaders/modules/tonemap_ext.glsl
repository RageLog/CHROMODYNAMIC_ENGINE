// =============================================================================
// cd::gluon — tonemap_ext.glsl
// P1 wave 2 (ADR-20260613-gluon-module-manifest §2.1(h) tonemap_ext):
// the AgX display transform — the real target of the prim.frag op=3 "AGX"
// placeholder. SEPARATE extension module: tonemap.glsl already carries
// Reinhard / ACES(Narkowicz+fitted) / Uchimura; this module adds AgX only
// (manifest §2.2 forbids module→module include — AgX stands alone here).
//
// All operators map linear scene-referred RGB to display-referred [0,1] and
// are gamma-agnostic (the caller applies the output transfer function).
//
// References:
//   * Troy Sobotka — AgX (the open-source filmic display transform).
//   * Benjamin Wrensch / Blender 4.0 minimal AgX fit — the AgX-base
//     in/out mat3 + log2 encode + 6th-order sigmoid used here.
// =============================================================================
#ifndef CD_GLUON_TONEMAP_EXT_GLSL
#define CD_GLUON_TONEMAP_EXT_GLSL

#include <cd/gluon/math_common.glsl>

// AgX-base inset matrix (linear sRGB → AgX working space).
const mat3 CD_AGX_MAT = mat3(
    0.842479062253094,  0.0423282422610123, 0.0423756549057051,
    0.0784335999999992, 0.878468636469772,  0.0784336,
    0.0792237451477643, 0.0791661274605434, 0.879142973793104);

// AgX-base outset matrix (AgX working space → linear sRGB; inverse inset).
const mat3 CD_AGX_MAT_INV = mat3(
     1.19687900512017,   -0.0528968517574562, -0.0529716355144438,
    -0.0980208811401368,  1.15190312990417,   -0.0980434501171241,
    -0.0990297440797205, -0.0989611768448433,  1.15107367264116);

/// 6th-order polynomial sigmoid approximating the AgX contrast curve
/// (Blender minimal fit). `x` is the AgX-log2-encoded value in [0,1].
vec3 cd_agx_default_contrast(vec3 x)
{
    vec3 x2 = x * x;
    vec3 x4 = x2 * x2;
    return  + 15.5     * x4 * x2
            - 40.14    * x4 * x
            + 31.96    * x4
            - 6.868    * x2 * x
            + 0.4298   * x2
            + 0.1191   * x
            - 0.00232;
}

/// AgX display transform (Sobotka / Blender minimal fit): AgX-base inset →
/// log2 encode over a fixed [-12.47, +4.026] EV window → contrast sigmoid →
/// AgX-base outset. Output is display-referred [0,1] linear (apply the
/// transfer function afterwards).
vec3 cd_tonemap_agx(vec3 c)
{
    const float kMinEv = -12.47393;
    const float kMaxEv =   4.026069;

    vec3 v = CD_AGX_MAT * max(c, vec3(0.0));
    v = clamp(log2(max(v, vec3(1e-10))), kMinEv, kMaxEv);
    v = (v - kMinEv) / (kMaxEv - kMinEv);
    v = cd_agx_default_contrast(v);
    v = CD_AGX_MAT_INV * v;
    return cd_saturate3(v);
}

/// "Punchy" AgX variant — applies a per-channel power (gamma) plus a
/// saturation boost around luma after the base transform, the popular
/// look-grade companion to plain AgX.
vec3 cd_tonemap_agx_punchy(vec3 c)
{
    const float kMinEv = -12.47393;
    const float kMaxEv =   4.026069;

    vec3 v = CD_AGX_MAT * max(c, vec3(0.0));
    v = clamp(log2(max(v, vec3(1e-10))), kMinEv, kMaxEv);
    v = (v - kMinEv) / (kMaxEv - kMinEv);
    v = cd_agx_default_contrast(v);

    // Punchy look: gamma 1.35 + saturation 1.4 around Rec.709 luma.
    const vec3 kLuma = vec3(0.2126, 0.7152, 0.0722);
    v = pow(cd_saturate3(v), vec3(1.0 / 1.35));
    float luma = dot(v, kLuma);
    v = luma + 1.4 * (v - luma);

    v = CD_AGX_MAT_INV * v;
    return cd_saturate3(v);
}

#endif  // CD_GLUON_TONEMAP_EXT_GLSL
