// =============================================================================
// cd::gluon — ibl_sampling.glsl
// P0-A wave 2 (ADR-20260613-gluon-module-manifest §2.3, row (d) ibl_sampling):
// RUNTIME image-based-lighting evaluation helpers. The IBL BAKE is NOT
// regenerated here (ADR-20260612 §2.6) — these are GPU-side runtime evals
// only (LUT-free env-BRDF, roughness→mip, SH irradiance).
//
// SOURCE / DERIVATION:
//   * cd_env_brdf_approx — SOTA-standard analytic split-sum env-BRDF
//     (Karis "mobile" / Lazarov 2013 "Getting More Physical in Call of Duty:
//     Black Ops 2"): the DFG/scale-bias fit that avoids the 2D BRDF LUT.
//     No DOMINANT GLSL copy exists (manifest §1.3: the engine bakes a real
//     BrdfLut on the CPU in ibl/BrdfLut.hpp; this is the LUT-free runtime
//     alternative for the new GLSL path). Returns vec2(scale, bias) so the
//     caller forms specular = F0·scale + bias, matching the BrdfLut.hpp:11-12
//     env_brdf.x / env_brdf.y consumption contract.
//   * cd_specular_ibl — SOTA convenience wrapper folding F0 with the analytic
//     scale/bias (same algebra as PrefilteredSpecular.hpp:12 specular form).
//   * cd_roughness_to_mip — VERBATIM (math) from the engine's DOMINANT
//     prefilter mip mapping in
//     engine/render/ibl/include/cd/ibl/PrefilteredSpecular.hpp:10
//     (mip = roughness · (num_mips - 1)). Pure rename; bake untouched.
//   * cd_sh9_irradiance — VERBATIM (math) from the engine's DOMINANT SH9
//     diffuse-irradiance eval in
//     engine/world/scene/include/cd/scene/LightProbe.hpp:34-57 (Ramamoorthi &
//     Hanrahan diffuse-only L2 basis, constants 0.282095 / 0.488603 /
//     1.092548 / 0.315392 / 0.546274). 9 RGB coefficients passed as an array.
//
// RULES (ADR-20260612 §2.2): may ONLY #include math_common.glsl; pure fns.
//   IBL bake is NOT regenerated (§2.6) — runtime helpers only.
// =============================================================================
#ifndef CD_GLUON_IBL_SAMPLING_GLSL
#define CD_GLUON_IBL_SAMPLING_GLSL

#include <cd/gluon/math_common.glsl>

// --- Analytic split-sum env-BRDF (Karis mobile / Lazarov, LUT-free) ----------

/// Analytic environment-BRDF (DFG) approximation returning vec2(scale, bias)
/// such that the specular IBL term is F0·scale + bias. Avoids the 2D BRDF
/// LUT. `nov` = N·V, `roughness` is perceptual (Lazarov 2013 fit).
vec2 cd_env_brdf_approx(float roughness, float nov) {
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572,  0.022);
    const vec4 c1 = vec4( 1.0,  0.0425,  1.04,  -0.04);
    vec4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * nov)) * r.x + r.y;
    return vec2(-1.04, 1.04) * a004 + r.zw;
}

/// Specular IBL term from the analytic env-BRDF: F0·scale + bias applied to
/// the prefiltered radiance. `prefiltered` is the roughness-mip env sample.
vec3 cd_specular_ibl(vec3 f0, float roughness, float nov, vec3 prefiltered) {
    vec2 ab = cd_env_brdf_approx(roughness, nov);
    return prefiltered * (f0 * ab.x + ab.y);
}

// --- Prefiltered-specular mip selection (PrefilteredSpecular DOMINANT) -------

/// Map a perceptual roughness [0,1] to a prefiltered-cube mip level for a
/// chain of `num_mips` levels (mip 0 = mirror, mip N-1 = roughness 1).
/// Matches PrefilteredSpecular.hpp: mip = roughness · (num_mips - 1).
float cd_roughness_to_mip(float roughness, float num_mips) {
    return cd_saturate(roughness) * (num_mips - 1.0);
}

// --- SH9 diffuse irradiance (LightProbe DOMINANT) ----------------------------

/// Evaluate SH9 (L2) diffuse irradiance from 9 RGB coefficients in unit
/// direction `n`. Ramamoorthi & Hanrahan diffuse-only basis — constants
/// byte-for-math identical to cd::scene::evaluate (LightProbe.hpp:38-46).
vec3 cd_sh9_irradiance(vec3 sh[9], vec3 n) {
    float y00  = 0.282095;
    float y1m1 = 0.488603 * n.y;
    float y10  = 0.488603 * n.z;
    float y11  = 0.488603 * n.x;
    float y2m2 = 1.092548 * n.x * n.y;
    float y2m1 = 1.092548 * n.y * n.z;
    float y20  = 0.315392 * (3.0 * n.z * n.z - 1.0);
    float y21  = 1.092548 * n.x * n.z;
    float y22  = 0.546274 * (n.x * n.x - n.y * n.y);

    return sh[0] * y00
         + sh[1] * y1m1
         + sh[2] * y10
         + sh[3] * y11
         + sh[4] * y2m2
         + sh[5] * y2m1
         + sh[6] * y20
         + sh[7] * y21
         + sh[8] * y22;
}

#endif  // CD_GLUON_IBL_SAMPLING_GLSL
