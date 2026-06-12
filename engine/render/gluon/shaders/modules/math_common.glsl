// =============================================================================
// cd::gluon — math_common.glsl
// Foundation constants + tiny helpers every other module may rely on.
// RULES (ADR-20260612-shader-library-architecture §2.2):
//   * this is the ONLY module other modules may #include;
//   * keep it dependency-free and side-effect-free (pure functions only).
// =============================================================================
#ifndef CD_GLUON_MATH_COMMON_GLSL
#define CD_GLUON_MATH_COMMON_GLSL

const float CD_PI          = 3.14159265358979323846;
const float CD_TWO_PI      = 6.28318530717958647693;
const float CD_HALF_PI     = 1.57079632679489661923;
const float CD_INV_PI      = 0.31830988618379067154;
const float CD_EPSILON     = 1e-6;

float cd_saturate(float x) { return clamp(x, 0.0, 1.0); }
vec3  cd_saturate3(vec3 v) { return clamp(v, vec3(0.0), vec3(1.0)); }

/// pow(x,5) via three multiplies — the Schlick workhorse.
float cd_pow5(float x)
{
    float x2 = x * x;
    return x2 * x2 * x;
}

float cd_max3(vec3 v) { return max(v.x, max(v.y, v.z)); }

/// Branchless orthonormal basis from a unit normal.
/// Duff et al. 2017, "Building an Orthonormal Basis, Revisited" (JCGT).
/// Matches the engine-side cd::math ONB (W8 sweeps standardised on it).
void cd_onb(vec3 n, out vec3 b1, out vec3 b2)
{
    float s = (n.z >= 0.0) ? 1.0 : -1.0;
    float a = -1.0 / (s + n.z);
    float b = n.x * n.y * a;
    b1 = vec3(1.0 + s * n.x * n.x * a, s * b, -s * n.x);
    b2 = vec3(b, s + n.y * n.y * a, -n.y);
}

#endif  // CD_GLUON_MATH_COMMON_GLSL
