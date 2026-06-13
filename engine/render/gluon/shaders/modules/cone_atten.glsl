// =============================================================================
// cd::gluon — cone_atten.glsl
// phase1151 (SL-D wave 5, exact-text migration): spot-light cone falloff
// lifted VERBATIM from cd::material::LitPbrMaterial's embedded fragment
// GLSL so the preprocessed token stream of the consumer is unchanged.
//
// Distinct from hello_engine prim.frag's spot path (which uses a
// smoothstep(cos_out, cos_in, cos_b) falloff) — that is a DIFFERENT
// function, not a dedupe pair, so this module is LitPbr-only for now.
// Canonical unification of the two spot models is the user-signed
// visual phase (ADR-20260612 §2.6).
// =============================================================================
#ifndef CD_GLUON_CONE_ATTEN_GLSL
#define CD_GLUON_CONE_ATTEN_GLSL

float cone_atten(float cos_theta, float cos_inner, float cos_outer) {
  if (cos_theta >= cos_inner) return 1.0;
  if (cos_theta <= cos_outer) return 0.0;
  float t = (cos_theta - cos_outer) / max(1e-5, cos_inner - cos_outer);
  return t * t;
}

#endif  // CD_GLUON_CONE_ATTEN_GLSL
