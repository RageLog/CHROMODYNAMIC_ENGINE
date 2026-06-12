// =============================================================================
// cd::gluon — brdf_w8aq.glsl
// phase1135 (SL-D wave 1, exact-text migration): the W8-AQ Cook-Torrance
// helper block lifted VERBATIM from hello_engine shaders/prim.frag.glsl.
// The preprocessed token stream of every consumer stays byte-identical,
// so golden captures pin the move (chrome_probe cmp clean).
//
// NOTE: this is the TRANSITIONAL twin of brdf.glsl. The functions here
// mirror cd::material::StandardPbrMaterial's inline copy; canonical
// unification onto cd_d_ggx / cd_v_smith_ggx_correlated is the separate
// USER-SIGNED-OFF visual-change phase (ADR-20260612 §2.6) because the
// Schlick-GGX k-remap here is NOT FP-identical to height-correlated
// Smith. Do not extend this module — new code uses brdf.glsl.
// =============================================================================
#ifndef CD_GLUON_BRDF_W8AQ_GLSL
#define CD_GLUON_BRDF_W8AQ_GLSL

// W8-AQ Cook-Torrance helpers (used by tint.w == 3.0 PBR-sphere branch).
// Same equations as cd::material::StandardPbrMaterial so unified prim
// path renders metallic spheres physically identical to the dedicated
// PBR pipeline used by hello_pbr.
float D_GGX_pbr(float NoH, float a) {
  float a2 = a * a;
  float d  = (NoH * NoH) * (a2 - 1.0) + 1.0;
  return a2 / (3.14159265 * d * d + 1e-7);
}
float G_SchlickGGX_pbr(float NoV, float k) {
  return NoV / (NoV * (1.0 - k) + k + 1e-7);
}
float G_Smith_pbr(float NoV, float NoL, float roughness) {
  float r = roughness + 1.0;
  float k = (r * r) / 8.0;
  return G_SchlickGGX_pbr(NoV, k) * G_SchlickGGX_pbr(NoL, k);
}
vec3 F_Schlick_pbr(float HoV, vec3 F0) {
  return F0 + (vec3(1.0) - F0) * pow(clamp(1.0 - HoV, 0.0, 1.0), 5.0);
}
vec3 F_Schlick_roughness_pbr(float NoV, vec3 F0, float roughness) {
  vec3 ceiling = max(vec3(1.0 - roughness), F0);
  return F0 + (ceiling - F0) * pow(clamp(1.0 - NoV, 0.0, 1.0), 5.0);
}

#endif  // CD_GLUON_BRDF_W8AQ_GLSL
