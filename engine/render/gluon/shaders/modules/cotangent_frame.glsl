// =============================================================================
// cd::gluon — cotangent_frame.glsl
// phase1138 (SL-D wave 2, exact-text migration): lifted VERBATIM from
// hello_engine shaders/prim.frag.glsl so the preprocessed token stream
// of the consumer is unchanged (chrome_probe golden pins the move).
// Canonical unification/extension happens in the user-signed visual
// phase (ADR-20260612 §2.6).
// =============================================================================
#ifndef CD_GLUON_COTANGENT_FRAME_GLSL
#define CD_GLUON_COTANGENT_FRAME_GLSL

// Cotangent-frame from screen-space derivatives (Mikkelsen 2010).
// Avoids needing per-vertex tangents - works for any UV-mapped mesh.
mat3 cotangent_frame(vec3 N, vec3 p, vec2 uv) {
  vec3 dp1 = dFdx(p);
  vec3 dp2 = dFdy(p);
  vec2 duv1 = dFdx(uv);
  vec2 duv2 = dFdy(uv);
  vec3 dp2perp = cross(dp2, N);
  vec3 dp1perp = cross(N, dp1);
  vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
  vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;
  // phase437-black: guard against degenerate UV (identical UVs on a
  // Sponza primitive / collapsed triangle → dFdx/dFdy == 0 →
  // max(dot(T,T), dot(B,B)) == 0 → inversesqrt(0) = +Inf →
  // TBN * nm_sample = NaN). Fall back to identity TBN (N unchanged).
  float denom = max(dot(T, T), dot(B, B));
  if (denom < 1e-10) return mat3(vec3(1.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0), N);
  float invmax = inversesqrt(denom);
  return mat3(T * invmax, B * invmax, N);
}
#endif  // CD_GLUON_COTANGENT_FRAME_GLSL
