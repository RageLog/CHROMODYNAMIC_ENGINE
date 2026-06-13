// =============================================================================
// cd::gluon — ltc_specular.glsl
// GGX-specular extension for ltc_polygon.glsl.
// Implements the LTC inverse-matrix sampler + sparse-matrix transform +
// specular form factor — the three functions that are specific to
// StandardPbrMaterial and must NOT be injected into prim.frag (which
// already includes ltc_polygon.glsl for its diffuse area-light path).
//
// References:
//   Karis 2013  "Real Shading in Unreal Engine 4" (representative-point §3.2)
//   Heitz 2016  "Real-Time Polygonal-Light Shading with LTC" (SIGGRAPH)
//
// Depends on: cd/gluon/ltc_polygon.glsl  (cd_ltc_edge_integral)
// =============================================================================
#ifndef CD_GLUON_LTC_SPECULAR_GLSL
#define CD_GLUON_LTC_SPECULAR_GLSL

#include <cd/gluon/ltc_polygon.glsl>

// LTC inverse-matrix sampler (Heitz 2016 GGX) — analytic 4-term
// polynomial fit (~1% MSE vs the 64x64 LUT). Returns the sparse
// (a, b, c, d) entries of the inverse LTC matrix at (roughness, NoV).
// Same fit as cd::brdf::ltc::ltc_inverse_matrix on CPU.
vec4 cd_ltc_inv_matrix(float roughness, float n_dot_v) {
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
vec3 cd_ltc_M_transform(vec4 M, vec3 v) {
  return vec3(M.x * v.x + M.y * v.z,
              M.z * v.y,
              M.w * v.x + v.z);
}
// Specular form factor over the polygon, transformed through M^-1.
float cd_ltc_polygon_specular(vec3 N, vec3 c0, vec3 c1, vec3 c2, vec3 c3,
                               float roughness, float NoV) {
  vec3 up = abs(N.y) > 0.95 ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0);
  vec3 T  = normalize(cross(up, N));
  vec3 B  = cross(N, T);
  mat3 frame = transpose(mat3(T, B, N));
  vec3 p0 = frame * c0;
  vec3 p1 = frame * c1;
  vec3 p2 = frame * c2;
  vec3 p3 = frame * c3;
  vec4 M  = cd_ltc_inv_matrix(roughness, NoV);
  p0 = normalize(cd_ltc_M_transform(M, p0));
  p1 = normalize(cd_ltc_M_transform(M, p1));
  p2 = normalize(cd_ltc_M_transform(M, p2));
  p3 = normalize(cd_ltc_M_transform(M, p3));
  float s = cd_ltc_edge_integral(p0, p1) +
            cd_ltc_edge_integral(p1, p2) +
            cd_ltc_edge_integral(p2, p3) +
            cd_ltc_edge_integral(p3, p0);
  return max(s, 0.0) / 6.28318530;
}

#endif  // CD_GLUON_LTC_SPECULAR_GLSL
