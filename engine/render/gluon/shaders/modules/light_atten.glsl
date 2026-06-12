// =============================================================================
// cd::gluon — light_atten.glsl
// phase1138 (SL-D wave 2, exact-text migration): lifted VERBATIM from
// hello_engine shaders/prim.frag.glsl so the preprocessed token stream
// of the consumer is unchanged (chrome_probe golden pins the move).
// Canonical unification/extension happens in the user-signed visual
// phase (ADR-20260612 §2.6).
// =============================================================================
#ifndef CD_GLUON_LIGHT_ATTEN_GLSL
#define CD_GLUON_LIGHT_ATTEN_GLSL

// Frostbite windowed inverse-square attenuation.
float distance_atten(float d, float range) {
  if (range <= 0.0) return 0.0;
  float ratio = d / range;
  float w = clamp(1.0 - ratio*ratio*ratio*ratio, 0.0, 1.0);
  return (w * w) / (d * d + 0.01);
}
#endif  // CD_GLUON_LIGHT_ATTEN_GLSL
