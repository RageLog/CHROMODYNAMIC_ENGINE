// =============================================================================
// cd::gluon — ltc_standard_pbr.glsl
// SL-D wave 4 (exact-text migration): the LTC area-light helper suite
// lifted VERBATIM from the embedded fragment shader in
// cd::material::StandardPbrMaterial (kStandardPbrFS), so the consumer's
// preprocessed token stream stays byte-identical.
//
// NOTE: TRANSITIONAL twin of ltc_polygon.glsl — exact-text from
// StandardPbrMaterial (W8-AJ); the prim.frag cd_-prefixed copy stays
// separate in ltc_polygon.glsl. Canonical unification is the
// user-signed visual phase (ADR-20260612 §2.6); do not extend.
// =============================================================================
#ifndef CD_GLUON_LTC_STANDARD_PBR_GLSL
#define CD_GLUON_LTC_STANDARD_PBR_GLSL

// ---- LTC area-light helpers (cd::brdf_ltc — Heitz 2016) --------------------
// Drop-in helpers for rectangular area light integration. The math is
// canonicalised in cd::brdf::ltc::kLtcGlsl; inlined here so the PBR FS
// doesn't depend on a shader-include facility. atan2 form for stability
// near parallel/anti-parallel edge configurations.
float ltc_edge_integral(vec3 a, vec3 b) {
  float d = clamp(dot(a, b), -1.0, 1.0);
  vec3  c = cross(a, b);
  float l = length(c);
  float th = (l < 1e-6) ? 0.0 : atan(l, d);
  return (l < 1e-6) ? 0.0 : (th / l) * c.z;
}
float ltc_polygon_irradiance(vec3 N, vec3 c0, vec3 c1, vec3 c2, vec3 c3) {
  vec3 up = abs(N.y) > 0.95 ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0);
  vec3 T  = normalize(cross(up, N));
  vec3 B  = cross(N, T);
  mat3 frame = transpose(mat3(T, B, N));
  vec3 p0 = normalize(frame * c0);
  vec3 p1 = normalize(frame * c1);
  vec3 p2 = normalize(frame * c2);
  vec3 p3 = normalize(frame * c3);
  float s = ltc_edge_integral(p0, p1) +
            ltc_edge_integral(p1, p2) +
            ltc_edge_integral(p2, p3) +
            ltc_edge_integral(p3, p0);
  return max(s, 0.0) / 6.28318530;  // form factor → irradiance
}

// LTC inverse-matrix sampler (Heitz 2016 GGX) — analytic 4-term
// polynomial fit (~1% MSE vs the 64x64 LUT). Returns the sparse
// (a, b, c, d) entries of the inverse LTC matrix at (roughness, NoV).
// Same fit as cd::brdf::ltc::ltc_inverse_matrix on CPU.
vec4 ltc_inv_matrix(float roughness, float n_dot_v) {
  float r  = clamp(roughness, 0.001, 1.0);
  float nv = clamp(n_dot_v, 0.001, 1.0);
  float a  = 1.0 + r * (-0.6 + 0.5 * (1.0 - nv));
  float b  = r * (1.0 - nv) * 0.5;
  float cm = 1.0 + r * (-0.4);
  float d  = r * nv * -0.3;
  return vec4(a, b, cm, d);
}
// Transform a tangent-space vec3 by the sparse LTC inverse matrix M^-1:
//   M^-1 = | a 0 b |
//          | 0 c 0 |
//          | d 0 1 |
vec3 ltc_M_transform(vec4 M, vec3 v) {
  return vec3(M.x * v.x + M.y * v.z,
              M.z * v.y,
              M.w * v.x + v.z);
}
// Specular form factor over the polygon, transformed through M^-1.
float ltc_polygon_specular(vec3 N, vec3 c0, vec3 c1, vec3 c2, vec3 c3,
                           float roughness, float NoV) {
  vec3 up = abs(N.y) > 0.95 ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0);
  vec3 T  = normalize(cross(up, N));
  vec3 B  = cross(N, T);
  mat3 frame = transpose(mat3(T, B, N));
  vec3 p0 = frame * c0;
  vec3 p1 = frame * c1;
  vec3 p2 = frame * c2;
  vec3 p3 = frame * c3;
  vec4 M  = ltc_inv_matrix(roughness, NoV);
  p0 = normalize(ltc_M_transform(M, p0));
  p1 = normalize(ltc_M_transform(M, p1));
  p2 = normalize(ltc_M_transform(M, p2));
  p3 = normalize(ltc_M_transform(M, p3));
  float s = ltc_edge_integral(p0, p1) +
            ltc_edge_integral(p1, p2) +
            ltc_edge_integral(p2, p3) +
            ltc_edge_integral(p3, p0);
  return max(s, 0.0) / 6.28318530;
}

#endif  // CD_GLUON_LTC_STANDARD_PBR_GLSL
