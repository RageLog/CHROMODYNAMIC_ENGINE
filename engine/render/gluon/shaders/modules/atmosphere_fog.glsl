// =============================================================================
// cd::gluon — atmosphere_fog.glsl
// Canonical atmospheric scattering phase functions (Rayleigh + Cornette-Shanks /
// Henyey-Greenstein Mie), Beer-Lambert transmittance, and analytic exponential
// height-fog helpers (Hillaire 2016 / Frostbite).
// =============================================================================
#ifndef CD_GLUON_ATMOSPHERE_FOG_GLSL
#define CD_GLUON_ATMOSPHERE_FOG_GLSL

#include <cd/gluon/math_common.glsl>

/// Rayleigh phase function for molecular scattering:
///   P_R(cos_theta) = (3 / (16 * pi)) * (1 + cos^2(theta))
float cd_phase_rayleigh(float cos_theta)
{
    return (3.0 / (16.0 * CD_PI)) * (1.0 + cos_theta * cos_theta);
}

/// Henyey-Greenstein phase function for aerosol / fog Mie scattering.
float cd_phase_henyey_greenstein(float cos_theta, float g)
{
    float g2 = g * g;
    float denom = max(1.0 + g2 - 2.0 * g * cos_theta, 1e-4);
    return (1.0 - g2) / (4.0 * CD_PI * denom * sqrt(denom));
}

/// Cornette-Shanks phase function (physically improved Mie phase function).
float cd_phase_cornette_shanks(float cos_theta, float g)
{
    float g2 = g * g;
    float num = 3.0 * (1.0 - g2) * (1.0 + cos_theta * cos_theta);
    float denom = (8.0 * CD_PI) * (2.0 + g2) * pow(max(1.0 + g2 - 2.0 * g * cos_theta, 1e-4), 1.5);
    return num / max(denom, CD_EPSILON);
}

/// Beer-Lambert optical transmittance exp(-sigma_t * distance).
vec3 cd_beer_lambert(vec3 sigma_t, float dist)
{
    return exp(-max(sigma_t, vec3(0.0)) * max(dist, 0.0));
}

/// Analytic exponential height-fog optical depth along a ray from `cam_pos`
/// to `world_pos`, given `base_density` at `base_height` and `height_falloff`.
float cd_exponential_height_fog_optical_depth(
    vec3 cam_pos,
    vec3 world_pos,
    float base_density,
    float base_height,
    float height_falloff
)
{
    vec3 ray = world_pos - cam_pos;
    float ray_len = length(ray);
    if (ray_len <= CD_EPSILON || base_density <= 0.0)
        return 0.0;

    float h0 = cam_pos.y - base_height;
    float dh = ray.y;
    float density_at_cam = base_density * exp(-clamp(height_falloff * h0, -20.0, 20.0));

    if (abs(dh * height_falloff) > 1e-4)
    {
        float k = height_falloff * dh;
        float integral = (1.0 - exp(-clamp(k, -20.0, 20.0))) / k;
        return max(density_at_cam * ray_len * integral, 0.0);
    }
    return max(density_at_cam * ray_len, 0.0);
}

#endif  // CD_GLUON_ATMOSPHERE_FOG_GLSL
