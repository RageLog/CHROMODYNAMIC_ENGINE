// =============================================================================
// cd::gluon — ltc_polygon.glsl
// phase1138 (SL-D wave 2, exact-text migration): lifted VERBATIM from
// hello_engine shaders/prim.frag.glsl so the preprocessed token stream
// of the consumer is unchanged (chrome_probe golden pins the move).
// Canonical unification/extension happens in the user-signed visual
// phase (ADR-20260612 §2.6).
// =============================================================================
#ifndef CD_GLUON_LTC_POLYGON_GLSL
#define CD_GLUON_LTC_POLYGON_GLSL

// LTC polygon irradiance for area lights (#3). Lambert-only fit
// (identity inverse matrix - production wants a 64x64 LUT keyed
// by roughness/NoV). N is the surface normal at the shading
// point; corners are in world-space, relative to the shading
// point. Returns the form-factor of the polygon visible from N.
// Edge integral with atan2 - robust at parallel and anti-parallel
// configurations (the prior acos/sin form blew up near sin ~ 0 and
// produced a thin black stripe at the area-light's equatorial plane).
float cd_ltc_edge_integral(vec3 a, vec3 b) {
  float d = clamp(dot(a, b), -1.0, 1.0);
  vec3  c = cross(a, b);
  float l = length(c);
  float th = (l < 1e-6) ? 0.0 : atan(l, d);  // GLSL atan(y,x) = atan2
  return (l < 1e-6) ? 0.0 : (th / l) * c.z;
}
float cd_ltc_polygon_irradiance(vec3 N, vec3 c0, vec3 c1, vec3 c2, vec3 c3) {
  vec3 up = abs(N.y) > 0.95 ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0);
  vec3 T  = normalize(cross(up, N));
  vec3 B  = cross(N, T);
  mat3 frame = transpose(mat3(T, B, N));
  vec3 p0 = normalize(frame * c0);
  vec3 p1 = normalize(frame * c1);
  vec3 p2 = normalize(frame * c2);
  vec3 p3 = normalize(frame * c3);
  float s = cd_ltc_edge_integral(p0, p1) +
            cd_ltc_edge_integral(p1, p2) +
            cd_ltc_edge_integral(p2, p3) +
            cd_ltc_edge_integral(p3, p0);
  // max-not-abs: negative values mean the polygon is back-facing.
  // Closes the 'siyah serit' artifact at the rect's equatorial plane.
  return max(s, 0.0) / 6.28318530;
}
#endif  // CD_GLUON_LTC_POLYGON_GLSL
