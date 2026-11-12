// =============================================================================
// CHROMODYNAMIC PLAYER -- apps/player/ShowcaseRenderer3D.cpp
// Real Vulkan 1.3 Depth-Buffered 3D Cook-Torrance GGX PBR & Octopath-Style
// HD-2D Hybrid (3D Diorama + Depth-Tested Pixel-Art Sprite Cards) Renderer.
// =============================================================================
#include "ShowcaseRenderer3D.hpp"

#include <cd/asset/Primitives.hpp>
#include <cd/element_core/Element.hpp>
#include <cd/rhi/Barriers.hpp>
#include <cd/rhi/BlendPresets.hpp>
#include <cd/rhi/DepthStencilPresets.hpp>
#include <cd/rhi/Pipeline.hpp>
#include <cd/rhi/RasterStatePresets.hpp>
#include <cd/shader/Compiler.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <span>
#include <string_view>

namespace cd::player
{

namespace
{

struct alignas(16) SceneLightingUbo
{
    float view_proj[16] {};
    float cam_pos[4] {};      // xyz = eye, w = sim_time
    float cam_forward[4] {};  // xyz = forward, w = aspect
    float cam_right[4] {};    // xyz = right, w = tan_half_fov
    float cam_up[4] {};       // xyz = up, w = hd2d_mode (1.0 or 0.0)
    float sun_dir[4] {};      // xyz = normalized to-sun, w = sun_intensity
    float sun_color[4] {};    // rgb = sun color, w = ambient_intensity
    float sky_zenith[4] {};   // rgb = zenith color, w = exposure
    float sky_horizon[4] {};  // rgb = horizon color, w = ground_y
    float point_pos_rad[4][4] {};
    float point_col_int[4][4] {};
};

struct PbrPush
{
    float model[16] {};
    float base_color[4] {};    // rgb = albedo, a = pattern_mode (0=solid, 1=ashlar_stone, 2=studio_grid)
    float mat_params[4] {};    // x = metallic, y = roughness, z = emissive_strength, w = rim_boost
    float extra_params[4] {};  // rgb = emissive_color, w = uv_scale
};
static_assert(sizeof(PbrPush) <= 128U, "PbrPush must fit 128B Vulkan minimum push constant guarantee");

struct Sprite3DPush
{
    float model[16] {};
    float uv_rect[4] {};      // u0, v0, u1, v1
    float tint_params[4] {};  // rgb = tint, w = emissive_boost
};
static_assert(sizeof(Sprite3DPush) <= 128U, "Sprite3DPush must fit 128B Vulkan push constant limit");

constexpr const char* kSkyVS = R"glsl(
#version 450
layout(location = 0) out vec2 v_ndc;
void main()
{
    vec2 pos = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    v_ndc = pos * 2.0 - 1.0;
    gl_Position = vec4(v_ndc, 0.9999, 1.0);
}
)glsl";

constexpr const char* kSkyFS = R"glsl(
#version 450
layout(location = 0) in vec2 v_ndc;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform SceneUbo
{
    mat4 u_view_proj;
    vec4 u_cam_pos;
    vec4 u_cam_forward;
    vec4 u_cam_right;
    vec4 u_cam_up;
    vec4 u_sun_dir;
    vec4 u_sun_color;
    vec4 u_sky_zenith;
    vec4 u_sky_horizon;
    vec4 u_point_pos_rad[4];
    vec4 u_point_col_int[4];
};

vec3 aces_tonemap(vec3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main()
{
    float aspect = u_cam_forward.w;
    float tan_hfov = u_cam_right.w;
    vec3 rd = normalize(
        u_cam_forward.xyz +
        u_cam_right.xyz * (v_ndc.x * aspect * tan_hfov) +
        u_cam_up.xyz * (-v_ndc.y * tan_hfov));

    float up_t = clamp(rd.y * 0.5 + 0.5, 0.0, 1.0);
    float horizon_band = exp(-abs(rd.y) * 5.5);
    vec3 sky = mix(u_sky_horizon.rgb * 0.55, u_sky_zenith.rgb, pow(clamp(max(rd.y, 0.0), 0.0, 1.0), 0.55));
    sky = mix(sky, u_sky_horizon.rgb, horizon_band * 0.85);

    vec3 sun_d = normalize(u_sun_dir.xyz);
    float mu = max(dot(rd, sun_d), 0.0);
    sky += u_sun_color.rgb * (pow(mu, 12.0) * 0.22 + pow(mu, 96.0) * 0.65);
    float sun_disc = smoothstep(0.9982, 0.9994, mu);
    sky += u_sun_color.rgb * sun_disc * 3.8;

    // Subtle twilight stars in HD-2D mode
    if (u_cam_up.w > 0.5 && rd.y > 0.08)
    {
        vec2 star_cell = floor(rd.xz / max(rd.y + 0.25, 0.25) * 90.0);
        float h = fract(sin(dot(star_cell, vec2(127.1, 311.7))) * 43758.5453);
        if (h > 0.992)
        {
            float twinkle = 0.6 + 0.4 * sin(u_cam_pos.w * 3.0 + h * 40.0);
            sky += vec3(0.85, 0.92, 1.0) * (h - 0.992) * 110.0 * twinkle * clamp(rd.y * 2.0, 0.0, 1.0);
        }
    }

    // Subtle distant mountain silhouette near horizon
    float ridge_angle = atan(rd.z, rd.x);
    float mtn_h = 0.025 + 0.032 * sin(ridge_angle * 5.0 + 0.7) + 0.018 * sin(ridge_angle * 13.0 - 1.2);
    if (rd.y < mtn_h && rd.y > -0.18)
    {
        vec3 mtn_col = mix(u_sky_zenith.rgb * 0.32, u_sky_horizon.rgb * 0.48, 0.5);
        float edge = smoothstep(mtn_h, mtn_h - 0.006, rd.y);
        sky = mix(sky, mtn_col, edge * 0.82);
    }

    vec3 mapped = aces_tonemap(sky * u_sky_zenith.w);
    out_color = vec4(pow(mapped, vec3(1.0 / 2.2)), 1.0);
}
)glsl";

constexpr const char* kPbrVS = R"glsl(
#version 450
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;
layout(location = 3) in vec3 a_color;

layout(location = 0) out vec3 v_world_pos;
layout(location = 1) out vec3 v_normal;
layout(location = 2) out vec2 v_uv;
layout(location = 3) out vec3 v_vcolor;

layout(set = 0, binding = 0) uniform SceneUbo
{
    mat4 u_view_proj;
    vec4 u_cam_pos;
    vec4 u_cam_forward;
    vec4 u_cam_right;
    vec4 u_cam_up;
    vec4 u_sun_dir;
    vec4 u_sun_color;
    vec4 u_sky_zenith;
    vec4 u_sky_horizon;
    vec4 u_point_pos_rad[4];
    vec4 u_point_col_int[4];
};

layout(push_constant) uniform Push
{
    mat4 u_model;
    vec4 u_base_color;
    vec4 u_mat_params;
    vec4 u_extra_params;
};

void main()
{
    vec4 wp = u_model * vec4(a_pos, 1.0);
    v_world_pos = wp.xyz;
    mat3 nmat = transpose(inverse(mat3(u_model)));
    v_normal = normalize(nmat * a_normal);
    v_uv = a_uv;
    v_vcolor = a_color;
    gl_Position = u_view_proj * wp;
}
)glsl";

constexpr const char* kPbrFS = R"glsl(
#version 450
layout(location = 0) in vec3 v_world_pos;
layout(location = 1) in vec3 v_normal;
layout(location = 2) in vec2 v_uv;
layout(location = 3) in vec3 v_vcolor;

layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform SceneUbo
{
    mat4 u_view_proj;
    vec4 u_cam_pos;
    vec4 u_cam_forward;
    vec4 u_cam_right;
    vec4 u_cam_up;
    vec4 u_sun_dir;
    vec4 u_sun_color;
    vec4 u_sky_zenith;
    vec4 u_sky_horizon;
    vec4 u_point_pos_rad[4];
    vec4 u_point_col_int[4];
};

layout(push_constant) uniform Push
{
    mat4 u_model;
    vec4 u_base_color;
    vec4 u_mat_params;
    vec4 u_extra_params;
};

const float PI = 3.14159265359;

vec3 aces_tonemap(vec3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

float d_ggx(float ndoth, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float denom = (ndoth * ndoth) * (a2 - 1.0) + 1.0;
    return a2 / max(PI * denom * denom, 1e-5);
}

float g_schlick_ggx(float ndotv, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return ndotv / max(ndotv * (1.0 - k) + k, 1e-5);
}

float g_smith(float ndotv, float ndotl, float roughness)
{
    return g_schlick_ggx(ndotv, roughness) * g_schlick_ggx(ndotl, roughness);
}

vec3 f_schlick(float cos_theta, vec3 f0)
{
    return f0 + (1.0 - f0) * pow(clamp(1.0 - cos_theta, 0.0, 1.0), 5.0);
}

vec3 eval_brdf(vec3 N, vec3 V, vec3 L, vec3 radiance, vec3 albedo, float metallic, float roughness)
{
    float ndotl = max(dot(N, L), 0.0);
    if (ndotl <= 0.0) return vec3(0.0);
    vec3 H = normalize(V + L);
    float ndotv = max(dot(N, V), 1e-4);
    float ndoth = max(dot(N, H), 0.0);
    float vdoth = max(dot(H, V), 0.0);

    vec3 f0 = mix(vec3(0.04), albedo, metallic);
    float D = d_ggx(ndoth, roughness);
    float G = g_smith(ndotv, ndotl, roughness);
    vec3  F = f_schlick(vdoth, f0);

    vec3 spec = (D * G * F) / max(4.0 * ndotv * ndotl, 1e-4);
    vec3 kd = (vec3(1.0) - F) * (1.0 - metallic);
    return (kd * albedo / PI + spec) * radiance * ndotl;
}

void main()
{
    vec3 albedo = u_base_color.rgb;
    float metallic = clamp(u_mat_params.x, 0.0, 1.0);
    float roughness = clamp(u_mat_params.y, 0.045, 1.0);
    vec3 N = normalize(v_normal);
    float pattern = u_base_color.a;

    if (pattern > 1.5)
    {
        // Pure3D Studio Polished Slate Checkerboard + Metric Grid
        vec2 gp = v_world_pos.xz * max(u_extra_params.w, 1.0);
        vec2 cell = floor(gp);
        float chk = mod(cell.x + cell.y, 2.0);
        albedo = mix(albedo * 0.72, albedo * 1.16, chk);
        vec2 grid_d = abs(fract(gp - 0.5) - 0.5) / max(fwidth(gp), vec2(1e-3));
        float line = 1.0 - clamp(min(grid_d.x, grid_d.y), 0.0, 1.0);
        float fade = exp(-length(v_world_pos.xz) * 0.065);
        albedo = mix(albedo, vec3(0.22, 0.42, 0.72), line * 0.38 * fade);
        roughness = mix(0.24, 0.36, chk);
    }
    else if (pattern > 0.5)
    {
        // HD-2D Chiseled Courtyard Ashlar Stone Blocks & Mortar
        vec2 sp = (abs(N.y) > 0.65) ? v_world_pos.xz : vec2(v_world_pos.x + v_world_pos.z, v_world_pos.y);
        sp *= max(u_extra_params.w, 1.5);
        float row = floor(sp.y);
        sp.x += mod(row, 2.0) * 0.5;
        vec2 bid = floor(sp);
        vec2 f = fract(sp);
        float h = fract(sin(dot(bid, vec2(41.3, 289.1))) * 43758.5453);
        float mortar = step(0.055, f.x) * step(0.075, f.y) * step(f.x, 0.945) * step(f.y, 0.925);
        vec3 stone_var = albedo * (0.82 + 0.34 * h);
        albedo = mix(albedo * 0.34, stone_var, mortar);
        roughness = mix(0.88, clamp(roughness - 0.12 * h, 0.25, 0.92), mortar);
        vec3 bevel = normalize(vec3((0.5 - f.x) * (1.0 - mortar) * 0.35, 1.0, (0.5 - f.y) * (1.0 - mortar) * 0.35));
        if (abs(N.y) > 0.65) N = normalize(mix(N, bevel, 0.35));
    }

    vec3 V = normalize(u_cam_pos.xyz - v_world_pos);
    if (dot(N, V) < -0.15) N = -N;

    vec3 Lo = eval_brdf(N, V, normalize(u_sun_dir.xyz), u_sun_color.rgb * u_sun_dir.w, albedo, metallic, roughness);

    for (int i = 0; i < 4; ++i)
    {
        float rad = u_point_pos_rad[i].w;
        float inten = u_point_col_int[i].w;
        if (rad <= 0.01 || inten <= 0.001) continue;
        vec3 to_l = u_point_pos_rad[i].xyz - v_world_pos;
        float dist = length(to_l);
        if (dist >= rad) continue;
        float att = pow(clamp(1.0 - dist / rad, 0.0, 1.0), 2.0) / (1.0 + 0.18 * dist * dist);
        Lo += eval_brdf(N, V, to_l / max(dist, 1e-4), u_point_col_int[i].rgb * (inten * att), albedo, metallic, roughness);
    }

    // Hemisphere Sky/Ground IBL + Specular Environment Reflection
    float hemi = N.y * 0.5 + 0.5;
    vec3 sky_amb = mix(u_sky_horizon.rgb * 0.38, u_sky_zenith.rgb * 0.95, hemi) * u_sun_color.w;
    vec3 f0 = mix(vec3(0.04), albedo, metallic);
    vec3 F_ibl = f_schlick(max(dot(N, V), 0.0), f0);
    vec3 kd_ibl = (1.0 - F_ibl) * (1.0 - metallic);
    vec3 R = reflect(-V, N);
    float r_up = clamp(R.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 env_spec = mix(u_sky_horizon.rgb * 0.55, u_sky_zenith.rgb * 1.45, pow(r_up, 1.4));
    float sun_refl = pow(max(dot(R, normalize(u_sun_dir.xyz)), 0.0), mix(128.0, 8.0, roughness));
    env_spec += u_sun_color.rgb * sun_refl * (1.0 - roughness * 0.75) * 1.25;

    float ground_ao = clamp(0.58 + 0.42 * clamp((v_world_pos.y - u_sky_horizon.w) * 0.85, 0.0, 1.0), 0.45, 1.0);
    vec3 ambient = (kd_ibl * albedo * sky_amb + F_ibl * env_spec * (1.0 - roughness * 0.55) * u_sun_color.w) * ground_ao;

    // Fresnel rim accent + emissive
    float rim = pow(1.0 - max(dot(N, V), 0.0), 3.5) * u_mat_params.w;
    vec3 emissive = u_extra_params.rgb * u_mat_params.z + u_sky_horizon.rgb * rim * 0.35;

    // Subtle distance atmospheric perspective & HD-2D tilt-shift vignette
    vec3 hdr = ambient + Lo + emissive;
    float cam_dist = length(v_world_pos - u_cam_pos.xyz);
    float fog_f = 1.0 - exp(-max(cam_dist - 8.0, 0.0) * (u_cam_up.w > 0.5 ? 0.032 : 0.014));
    hdr = mix(hdr, u_sky_horizon.rgb * 0.65, clamp(fog_f, 0.0, 0.65));

    vec3 mapped = aces_tonemap(hdr * u_sky_zenith.w);
    out_color = vec4(pow(mapped, vec3(1.0 / 2.2)), 1.0);
}
)glsl";

constexpr const char* kSprite3DVS = R"glsl(
#version 450
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;
layout(location = 3) in vec3 a_color;

layout(location = 0) out vec3 v_world_pos;
layout(location = 1) out vec3 v_normal;
layout(location = 2) out vec2 v_uv;

layout(set = 0, binding = 0) uniform SceneUbo
{
    mat4 u_view_proj;
    vec4 u_cam_pos;
    vec4 u_cam_forward;
    vec4 u_cam_right;
    vec4 u_cam_up;
    vec4 u_sun_dir;
    vec4 u_sun_color;
    vec4 u_sky_zenith;
    vec4 u_sky_horizon;
    vec4 u_point_pos_rad[4];
    vec4 u_point_col_int[4];
};

layout(push_constant) uniform Push
{
    mat4 u_model;
    vec4 u_uv_rect;
    vec4 u_tint_params;
};

void main()
{
    vec4 wp = u_model * vec4(a_pos, 1.0);
    v_world_pos = wp.xyz;
    v_normal = normalize(mat3(u_model) * a_normal);
    v_uv = mix(u_uv_rect.xy, u_uv_rect.zw, a_uv);
    gl_Position = u_view_proj * wp;
}
)glsl";

constexpr const char* kSprite3DFS = R"glsl(
#version 450
layout(location = 0) in vec3 v_world_pos;
layout(location = 1) in vec3 v_normal;
layout(location = 2) in vec2 v_uv;

layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform SceneUbo
{
    mat4 u_view_proj;
    vec4 u_cam_pos;
    vec4 u_cam_forward;
    vec4 u_cam_right;
    vec4 u_cam_up;
    vec4 u_sun_dir;
    vec4 u_sun_color;
    vec4 u_sky_zenith;
    vec4 u_sky_horizon;
    vec4 u_point_pos_rad[4];
    vec4 u_point_col_int[4];
};

layout(set = 0, binding = 1) uniform sampler2D u_atlas;

layout(push_constant) uniform Push
{
    mat4 u_model;
    vec4 u_uv_rect;
    vec4 u_tint_params;
};

vec3 aces_tonemap(vec3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main()
{
    vec4 texel = texture(u_atlas, v_uv);
    if (texel.a < 0.45)
    {
        discard;
    }

    // Decode sRGB pixel-art texel to linear for physically consistent 3D lighting
    vec3 albedo = pow(texel.rgb, vec3(2.2)) * u_tint_params.rgb;
    vec3 N = normalize(v_normal);
    vec3 L_sun = normalize(u_sun_dir.xyz);

    // Wrap-diffuse so pixel-art characters stay readable on both lit and shadow sides
    float wrap_sun = clamp((dot(N, L_sun) + 0.45) / 1.45, 0.0, 1.0);
    vec3 light_acc = mix(u_sky_horizon.rgb * 0.42, u_sky_zenith.rgb * 0.95, 0.55) * u_sun_color.w;
    light_acc += u_sun_color.rgb * (u_sun_dir.w * 0.55 * wrap_sun);

    for (int i = 0; i < 4; ++i)
    {
        float rad = u_point_pos_rad[i].w;
        float inten = u_point_col_int[i].w;
        if (rad <= 0.01 || inten <= 0.001) continue;
        vec3 to_l = u_point_pos_rad[i].xyz - v_world_pos;
        float dist = length(to_l);
        if (dist >= rad) continue;
        float att = pow(clamp(1.0 - dist / rad, 0.0, 1.0), 1.75) / (1.0 + 0.12 * dist * dist);
        float wrap_pt = clamp((dot(N, to_l / max(dist, 1e-4)) + 0.55) / 1.55, 0.0, 1.0);
        light_acc += u_point_col_int[i].rgb * (inten * att * wrap_pt);
    }

    vec3 hdr = albedo * light_acc + albedo * u_tint_params.w;
    vec3 mapped = aces_tonemap(hdr * u_sky_zenith.w);
    out_color = vec4(pow(mapped, vec3(1.0 / 2.2)), texel.a);
}
)glsl";

[[nodiscard]] cd::asset::PrimitiveMesh make_upright_sprite_quad()
{
    cd::asset::PrimitiveMesh m {};
    using cd::asset::primitives_detail::make_v;
    using cd::asset::primitives_detail::push_quad_indices;
    m.vertices.push_back(make_v(-0.5F, 0.0F, 0.0F, 0.0F, 0.22F, 0.975F, 0.0F, 1.0F));
    m.vertices.push_back(make_v( 0.5F, 0.0F, 0.0F, 0.0F, 0.22F, 0.975F, 1.0F, 1.0F));
    m.vertices.push_back(make_v( 0.5F, 1.0F, 0.0F, 0.0F, 0.22F, 0.975F, 1.0F, 0.0F));
    m.vertices.push_back(make_v(-0.5F, 1.0F, 0.0F, 0.0F, 0.22F, 0.975F, 0.0F, 0.0F));
    push_quad_indices(m.indices, 0U, 1U, 2U, 3U);
    return m;
}

void mat4_identity(float out[16]) noexcept
{
    std::memset(out, 0, 16U * sizeof(float));
    out[0]  = 1.0F;
    out[5]  = 1.0F;
    out[10] = 1.0F;
    out[15] = 1.0F;
}

void mat4_mul(const float a[16], const float b[16], float out[16]) noexcept
{
    float r[16] {};
    for (int col = 0; col < 4; ++col)
    {
        for (int row = 0; row < 4; ++row)
        {
            r[col * 4 + row] =
                a[0 * 4 + row] * b[col * 4 + 0] +
                a[1 * 4 + row] * b[col * 4 + 1] +
                a[2 * 4 + row] * b[col * 4 + 2] +
                a[3 * 4 + row] * b[col * 4 + 3];
        }
    }
    std::memcpy(out, r, sizeof(r));
}

void make_trs_y(
    float tx, float ty, float tz,
    float yaw_rad,
    float sx, float sy, float sz,
    float out[16]) noexcept
{
    const float cy = std::cos(yaw_rad);
    const float sy_ang = std::sin(yaw_rad);
    std::memset(out, 0, 16U * sizeof(float));
    out[0]  =  cy * sx;
    out[2]  = -sy_ang * sx;
    out[5]  =  sy;
    out[8]  =  sy_ang * sz;
    out[10] =  cy * sz;
    out[12] =  tx;
    out[13] =  ty;
    out[14] =  tz;
    out[15] =  1.0F;
}

void make_look_at_vk(
    const std::array<float, 3>& eye,
    const std::array<float, 3>& target,
    float                       aspect,
    float                       fov_y_rad,
    float                       z_near,
    float                       z_far,
    float                       out_vp[16],
    std::array<float, 3>&       out_fwd,
    std::array<float, 3>&       out_right,
    std::array<float, 3>&       out_up) noexcept
{
    float fx = target[0] - eye[0];
    float fy = target[1] - eye[1];
    float fz = target[2] - eye[2];
    const float fl = std::max(1.0e-5F, std::sqrt(fx * fx + fy * fy + fz * fz));
    fx /= fl; fy /= fl; fz /= fl;

    // Right = normalize(cross(f, world_up(0,1,0)))
    float rx = -fz;
    float ry =  0.0F;
    float rz =  fx;
    const float rl = std::max(1.0e-5F, std::sqrt(rx * rx + rz * rz));
    rx /= rl; rz /= rl;

    // Up = cross(r, f)
    const float ux = ry * fz - rz * fy;
    const float uy = rz * fx - rx * fz;
    const float uz = rx * fy - ry * fx;

    out_fwd   = { fx, fy, fz };
    out_right = { rx, ry, rz };
    out_up    = { ux, uy, uz };

    float view[16] {};
    mat4_identity(view);
    view[0]  =  rx; view[4]  =  ry; view[8]  =  rz;
    view[1]  =  ux; view[5]  =  uy; view[9]  =  uz;
    view[2]  = -fx; view[6]  = -fy; view[10] = -fz;
    view[12] = -(rx * eye[0] + ry * eye[1] + rz * eye[2]);
    view[13] = -(ux * eye[0] + uy * eye[1] + uz * eye[2]);
    view[14] =  (fx * eye[0] + fy * eye[1] + fz * eye[2]);

    float proj[16] {};
    const float f = 1.0F / std::tan(fov_y_rad * 0.5F);
    proj[0]  = f / std::max(0.1F, aspect);
    proj[5]  = -f;  // Vulkan Y-down NDC flip
    proj[10] = z_far / (z_near - z_far);
    proj[11] = -1.0F;
    proj[14] = (z_near * z_far) / (z_near - z_far);

    mat4_mul(proj, view, out_vp);
}

[[nodiscard]] bool compile_glsl_module(
    cd::rhi::IDevice&        device,
    cd::shader::ICompiler&   compiler,
    std::string_view         glsl,
    cd::shader::ShaderStage  compile_stage,
    cd::rhi::ShaderStage     rhi_stage,
    std::string_view         debug_name,
    cd::rhi::ShaderModuleHandle& out_handle)
{
    cd::shader::CompileDesc cd_desc {};
    cd_desc.source      = glsl;
    cd_desc.stage       = compile_stage;
    cd_desc.lang        = cd::shader::ShaderLanguage::kGlsl;
    cd_desc.source_name = debug_name;
    auto compiled = compiler.compile(cd_desc);
    if (!compiled.has_value())
    {
        return false;
    }

    cd::rhi::ShaderModuleDesc smd {};
    smd.stage       = rhi_stage;
    smd.code        = compiled->spirv.data();
    smd.code_size   = compiled->spirv.size() * sizeof(std::uint32_t);
    smd.entry_point = "main";
    smd.debug_name  = debug_name;
    auto sm_r = device.create_shader_module(smd);
    if (!sm_r.has_value())
    {
        return false;
    }
    out_handle = *sm_r;
    return true;
}

}  // namespace

bool ShowcaseRenderer3D::ensure_depth_target(cd::rhi::Extent2D extent)
{
    if (device_ == nullptr || extent.width == 0U || extent.height == 0U)
    {
        return false;
    }
    if (depth_target_.view.is_valid() &&
        depth_target_.extent.width == extent.width &&
        depth_target_.extent.height == extent.height)
    {
        return true;
    }
    depth_initialized_on_gpu_ = false;
    return cd::framegraph::create_depth_target(
        *device_, extent, cd::rhi::Format::kD32Float, depth_target_);
}

bool ShowcaseRenderer3D::init(
    cd::rhi::IDevice&                   device,
    cd::rhi::Format                     color_format,
    cd::rhi::Extent2D                   initial_extent,
    cd::rhi::TextureViewHandle          atlas_view,
    cd::rhi::SamplerHandle              atlas_sampler,
    const cd::render2d::TextureAtlas2D& cpu_atlas)
{
    destroy();
    device_       = &device;
    color_format_ = color_format;
    cpu_atlas_    = cpu_atlas;

    auto compiler = cd::shader::make_glslang_compiler();
    if (!compiler)
    {
        return false;
    }
    if (!atlas_view.is_valid() || !atlas_sampler.is_valid())
    {
        return false;
    }
    if (!ensure_depth_target(initial_extent))
    {
        return false;
    }

    // 1. Upload procedural 3D meshes
    mesh_sphere_   = cd::render::upload_mesh(device, cd::asset::make_sphere(24, 40));
    mesh_cube_     = cd::render::upload_mesh(device, cd::asset::make_cube());
    mesh_cylinder_ = cd::render::upload_mesh(device, cd::asset::make_cylinder(28));
    mesh_torus_    = cd::render::upload_mesh(device, cd::asset::make_torus(0.36F, 0.11F, 36, 18));
    mesh_cone_     = cd::render::upload_mesh(device, cd::asset::make_cone(24));
    mesh_quad_     = cd::render::upload_mesh(device, make_upright_sprite_quad());
    if (!mesh_sphere_.vb.is_valid() || !mesh_cube_.vb.is_valid() || !mesh_quad_.vb.is_valid())
    {
        destroy();
        return false;
    }

    // 2. Descriptor set layout (binding 0 = UBO, binding 1 = pixel-art atlas sampler)
    const std::array<cd::rhi::DescriptorSetLayoutBinding, 2> bindings {
        cd::rhi::DescriptorSetLayoutBinding {
            0U,
            cd::rhi::DescriptorType::kUniformBuffer,
            1U,
            cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
            false },
        cd::rhi::DescriptorSetLayoutBinding {
            1U,
            cd::rhi::DescriptorType::kCombinedImageSampler,
            1U,
            cd::rhi::ShaderStage::kFragment,
            false },
    };
    cd::rhi::DescriptorSetLayoutDesc dsl_desc {};
    dsl_desc.bindings = bindings;
    auto dsl_r = device.create_descriptor_set_layout(dsl_desc);
    if (!dsl_r.has_value())
    {
        destroy();
        return false;
    }
    scene_dsl_ = *dsl_r;

    for (std::size_t i = 0U; i < kUboRingSize; ++i)
    {
        cd::rhi::BufferDesc bd {};
        bd.size   = sizeof(SceneLightingUbo);
        bd.usage  = cd::rhi::BufferUsage::kUniform | cd::rhi::BufferUsage::kTransferDst;
        bd.memory = cd::rhi::MemoryUsage::kCpuToGpu;
        auto buf_r = device.create_buffer(bd);
        if (!buf_r.has_value())
        {
            destroy();
            return false;
        }
        ubo_ring_.at(i) = *buf_r;

        auto ds_r = device.allocate_descriptor_set(scene_dsl_);
        if (!ds_r.has_value())
        {
            destroy();
            return false;
        }
        scene_ds_ring_.at(i) = *ds_r;

        cd::rhi::DescriptorWrite w_ubo {};
        w_ubo.binding      = 0U;
        w_ubo.type         = cd::rhi::DescriptorType::kUniformBuffer;
        w_ubo.buffer       = ubo_ring_.at(i);
        w_ubo.buffer_range = sizeof(SceneLightingUbo);

        cd::rhi::DescriptorWrite w_tex {};
        w_tex.binding = 1U;
        w_tex.type    = cd::rhi::DescriptorType::kCombinedImageSampler;
        w_tex.view    = atlas_view;
        w_tex.sampler = atlas_sampler;

        const std::array<cd::rhi::DescriptorWrite, 2> writes { w_ubo, w_tex };
        if (!device.update_descriptor_set(scene_ds_ring_.at(i), writes).has_value())
        {
            destroy();
            return false;
        }
    }

    // 3. Compile inline GLSL shaders
    if (!compile_glsl_module(device, *compiler, kSkyVS, cd::shader::ShaderStage::kVertex, cd::rhi::ShaderStage::kVertex, "player_sky.vert", sky_vs_) ||
        !compile_glsl_module(device, *compiler, kSkyFS, cd::shader::ShaderStage::kFragment, cd::rhi::ShaderStage::kFragment, "player_sky.frag", sky_fs_) ||
        !compile_glsl_module(device, *compiler, kPbrVS, cd::shader::ShaderStage::kVertex, cd::rhi::ShaderStage::kVertex, "player_pbr.vert", pbr_vs_) ||
        !compile_glsl_module(device, *compiler, kPbrFS, cd::shader::ShaderStage::kFragment, cd::rhi::ShaderStage::kFragment, "player_pbr.frag", pbr_fs_) ||
        !compile_glsl_module(device, *compiler, kSprite3DVS, cd::shader::ShaderStage::kVertex, cd::rhi::ShaderStage::kVertex, "player_sprite3d.vert", sprite_vs_) ||
        !compile_glsl_module(device, *compiler, kSprite3DFS, cd::shader::ShaderStage::kFragment, cd::rhi::ShaderStage::kFragment, "player_sprite3d.frag", sprite_fs_))
    {
        destroy();
        return false;
    }

    const std::array<cd::rhi::DescriptorSetLayoutHandle, 1> dsls { scene_dsl_ };
    const std::array<cd::rhi::Format, 1> color_fmts { color_format_ };
    const std::array<cd::rhi::BlendAttachmentState, 1> opaque_blends {
        cd::rhi::BlendAttachmentState { .blend_enable = false }
    };
    const std::array<cd::rhi::BlendAttachmentState, 1> alpha_blends {
        cd::rhi::blend_alpha()
    };

    const std::array<cd::rhi::VertexBinding, 1> v_binds {
        cd::rhi::VertexBinding { 0U, static_cast<std::uint32_t>(sizeof(cd::asset::PrimitiveVertex)), false }
    };
    const std::array<cd::rhi::VertexAttribute, 4> v_attrs {
        cd::rhi::VertexAttribute { 0U, 0U, cd::rhi::Format::kRGB32Float, static_cast<std::uint32_t>(offsetof(cd::asset::PrimitiveVertex, pos)) },
        cd::rhi::VertexAttribute { 1U, 0U, cd::rhi::Format::kRGB32Float, static_cast<std::uint32_t>(offsetof(cd::asset::PrimitiveVertex, normal)) },
        cd::rhi::VertexAttribute { 2U, 0U, cd::rhi::Format::kRG32Float,  static_cast<std::uint32_t>(offsetof(cd::asset::PrimitiveVertex, uv)) },
        cd::rhi::VertexAttribute { 3U, 0U, cd::rhi::Format::kRGB32Float, static_cast<std::uint32_t>(offsetof(cd::asset::PrimitiveVertex, color)) },
    };

    // 4. Sky pipeline (fullscreen triangle, depth disabled)
    {
        cd::rhi::PipelineLayoutDesc pld {};
        pld.set_layouts = dsls;
        auto pl_r = device.create_pipeline_layout(pld);
        if (!pl_r.has_value()) { destroy(); return false; }
        sky_layout_ = *pl_r;

        cd::rhi::GraphicsPipelineDesc gpd {};
        gpd.layout                   = sky_layout_;
        gpd.vertex_shader            = sky_vs_;
        gpd.fragment_shader          = sky_fs_;
        gpd.topology                 = cd::rhi::PrimitiveTopology::kTriangleList;
        gpd.raster                   = cd::rhi::raster_solid_none();
        gpd.depth_stencil            = cd::rhi::depth_disabled();
        gpd.blend_attachments        = opaque_blends;
        gpd.samples                  = cd::rhi::SampleCount::k1;
        gpd.color_attachment_formats = color_fmts;
        gpd.depth_attachment_format  = cd::rhi::Format::kD32Float;
        auto gp_r = device.create_graphics_pipeline(gpd);
        if (!gp_r.has_value()) { destroy(); return false; }
        sky_pipeline_ = *gp_r;
    }

    // 5. PBR pipeline (depth test + write ON)
    {
        const std::array<cd::rhi::PushConstantRange, 1> pcr {
            cd::rhi::PushConstantRange {
                cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
                0U,
                static_cast<std::uint32_t>(sizeof(PbrPush)) }
        };
        cd::rhi::PipelineLayoutDesc pld {};
        pld.set_layouts    = dsls;
        pld.push_constants = pcr;
        auto pl_r = device.create_pipeline_layout(pld);
        if (!pl_r.has_value()) { destroy(); return false; }
        pbr_layout_ = *pl_r;

        cd::rhi::GraphicsPipelineDesc gpd {};
        gpd.layout                   = pbr_layout_;
        gpd.vertex_shader            = pbr_vs_;
        gpd.fragment_shader          = pbr_fs_;
        gpd.vertex_bindings          = v_binds;
        gpd.vertex_attributes        = v_attrs;
        gpd.topology                 = cd::rhi::PrimitiveTopology::kTriangleList;
        gpd.raster                   = cd::rhi::raster_solid_none();
        gpd.depth_stencil            = cd::rhi::DepthStencilState {
            .depth_test    = true,
            .depth_write   = true,
            .depth_compare = cd::rhi::CompareOp::kLessEqual,
            .stencil_test  = false };
        gpd.blend_attachments        = opaque_blends;
        gpd.samples                  = cd::rhi::SampleCount::k1;
        gpd.color_attachment_formats = color_fmts;
        gpd.depth_attachment_format  = cd::rhi::Format::kD32Float;
        auto gp_r = device.create_graphics_pipeline(gpd);
        if (!gp_r.has_value()) { destroy(); return false; }
        pbr_pipeline_ = *gp_r;
    }

    // 6. HD-2D 3D Pixel-Art Sprite Card pipeline (alpha-cutout + depth test/write ON)
    {
        const std::array<cd::rhi::PushConstantRange, 1> pcr {
            cd::rhi::PushConstantRange {
                cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
                0U,
                static_cast<std::uint32_t>(sizeof(Sprite3DPush)) }
        };
        cd::rhi::PipelineLayoutDesc pld {};
        pld.set_layouts    = dsls;
        pld.push_constants = pcr;
        auto pl_r = device.create_pipeline_layout(pld);
        if (!pl_r.has_value()) { destroy(); return false; }
        sprite_layout_ = *pl_r;

        cd::rhi::GraphicsPipelineDesc gpd {};
        gpd.layout                   = sprite_layout_;
        gpd.vertex_shader            = sprite_vs_;
        gpd.fragment_shader          = sprite_fs_;
        gpd.vertex_bindings          = v_binds;
        gpd.vertex_attributes        = v_attrs;
        gpd.topology                 = cd::rhi::PrimitiveTopology::kTriangleList;
        gpd.raster                   = cd::rhi::raster_solid_none();
        gpd.depth_stencil            = cd::rhi::DepthStencilState {
            .depth_test    = true,
            .depth_write   = true,
            .depth_compare = cd::rhi::CompareOp::kLessEqual,
            .stencil_test  = false };
        gpd.blend_attachments        = alpha_blends;
        gpd.samples                  = cd::rhi::SampleCount::k1;
        gpd.color_attachment_formats = color_fmts;
        gpd.depth_attachment_format  = cd::rhi::Format::kD32Float;
        auto gp_r = device.create_graphics_pipeline(gpd);
        if (!gp_r.has_value()) { destroy(); return false; }
        sprite_pipeline_ = *gp_r;
    }

    initialized_ = true;
    return true;
}

void ShowcaseRenderer3D::destroy() noexcept
{
    if (device_ != nullptr)
    {
        if (sprite_pipeline_.is_valid()) device_->destroy_graphics_pipeline(sprite_pipeline_);
        if (sprite_layout_.is_valid())   device_->destroy_pipeline_layout(sprite_layout_);
        if (sprite_vs_.is_valid())       device_->destroy_shader_module(sprite_vs_);
        if (sprite_fs_.is_valid())       device_->destroy_shader_module(sprite_fs_);

        if (pbr_pipeline_.is_valid())    device_->destroy_graphics_pipeline(pbr_pipeline_);
        if (pbr_layout_.is_valid())      device_->destroy_pipeline_layout(pbr_layout_);
        if (pbr_vs_.is_valid())          device_->destroy_shader_module(pbr_vs_);
        if (pbr_fs_.is_valid())          device_->destroy_shader_module(pbr_fs_);

        if (sky_pipeline_.is_valid())    device_->destroy_graphics_pipeline(sky_pipeline_);
        if (sky_layout_.is_valid())      device_->destroy_pipeline_layout(sky_layout_);
        if (sky_vs_.is_valid())          device_->destroy_shader_module(sky_vs_);
        if (sky_fs_.is_valid())          device_->destroy_shader_module(sky_fs_);

        for (auto& ds : scene_ds_ring_)
        {
            if (ds.is_valid()) device_->destroy_descriptor_set(ds);
            ds = {};
        }
        for (auto& ub : ubo_ring_)
        {
            if (ub.is_valid()) device_->destroy_buffer(ub);
            ub = {};
        }
        if (scene_dsl_.is_valid()) device_->destroy_descriptor_set_layout(scene_dsl_);

        cd::render::destroy_mesh(*device_, mesh_quad_);
        cd::render::destroy_mesh(*device_, mesh_cone_);
        cd::render::destroy_mesh(*device_, mesh_torus_);
        cd::render::destroy_mesh(*device_, mesh_cylinder_);
        cd::render::destroy_mesh(*device_, mesh_cube_);
        cd::render::destroy_mesh(*device_, mesh_sphere_);

        depth_target_.destroy(*device_);
    }

    scene_dsl_                = {};
    sky_vs_                   = {};
    sky_fs_                   = {};
    sky_layout_               = {};
    sky_pipeline_             = {};
    pbr_vs_                   = {};
    pbr_fs_                   = {};
    pbr_layout_               = {};
    pbr_pipeline_             = {};
    sprite_vs_                = {};
    sprite_fs_                = {};
    sprite_layout_            = {};
    sprite_pipeline_          = {};
    depth_initialized_on_gpu_ = false;
    initialized_              = false;
    device_                   = nullptr;
}

bool ShowcaseRenderer3D::record(
    cd::rhi::ICommandBuffer&         cmd,
    cd::rhi::TextureViewHandle       swapchain_view,
    cd::rhi::Extent2D                extent,
    const cd::element_core::Stage&   stage,
    const ShowcaseCamera3D&          cam,
    bool                             hd2d_mode)
{
    if (!initialized_ || device_ == nullptr || !swapchain_view.is_valid())
    {
        return false;
    }
    if (!ensure_depth_target(extent))
    {
        return false;
    }

    if (!depth_initialized_on_gpu_)
    {
        const std::array<cd::rhi::TextureBarrier, 1> db {
            cd::rhi::TextureBarrier {
                .texture = depth_target_.image,
                .from    = cd::rhi::ResourceState::kUndefined,
                .to      = cd::rhi::ResourceState::kDepthWrite,
                .range   = { 0U, 1U, 0U, 1U } }
        };
        cmd.barrier({}, db);
        depth_initialized_on_gpu_ = true;
    }

    // 1. Build camera & lighting UBO
    const float aspect    = static_cast<float>(extent.width) / static_cast<float>(std::max(1U, extent.height));
    const float fov_y_rad = hd2d_mode ? 0.58F : 0.72F;  // Telephoto diorama lens for HD-2D!
    const float orbit_yaw = hd2d_mode
        ? (0.14F * std::sin(cam.sim_time * 0.45F) + (cam.yaw_rad - 0.48F))
        : (cam.yaw_rad + cam.sim_time * 0.22F);
    const float pitch     = hd2d_mode ? std::clamp(cam.pitch_rad + 0.08F, 0.28F, 0.78F) : cam.pitch_rad;
    const float dist      = hd2d_mode ? 13.8F : cam.distance;

    const std::array<float, 3> target = hd2d_mode
        ? std::array<float, 3> { 0.0F, 0.35F, -0.8F }
        : std::array<float, 3> { 0.0F, 0.35F,  0.0F };
    const std::array<float, 3> eye {
        target[0] + std::sin(orbit_yaw) * std::cos(pitch) * dist,
        target[1] + std::sin(pitch) * dist,
        target[2] + std::cos(orbit_yaw) * std::cos(pitch) * dist,
    };

    SceneLightingUbo ubo {};
    std::array<float, 3> fwd {}, right {}, up {};
    make_look_at_vk(eye, target, aspect, fov_y_rad, 0.15F, 120.0F, ubo.view_proj, fwd, right, up);

    ubo.cam_pos[0] = eye[0]; ubo.cam_pos[1] = eye[1]; ubo.cam_pos[2] = eye[2]; ubo.cam_pos[3] = cam.sim_time;
    ubo.cam_forward[0] = fwd[0]; ubo.cam_forward[1] = fwd[1]; ubo.cam_forward[2] = fwd[2]; ubo.cam_forward[3] = aspect;
    ubo.cam_right[0] = right[0]; ubo.cam_right[1] = right[1]; ubo.cam_right[2] = right[2]; ubo.cam_right[3] = std::tan(fov_y_rad * 0.5F);
    ubo.cam_up[0] = up[0]; ubo.cam_up[1] = up[1]; ubo.cam_up[2] = up[2]; ubo.cam_up[3] = hd2d_mode ? 1.0F : 0.0F;

    if (hd2d_mode)
    {
        // Warm golden-twilight HD-2D atmosphere + 4 flickering 3D torch lanterns
        ubo.sun_dir[0] = -0.52F; ubo.sun_dir[1] = 0.36F; ubo.sun_dir[2] = -0.77F; ubo.sun_dir[3] = 1.65F;
        ubo.sun_color[0] = 1.0F; ubo.sun_color[1] = 0.72F; ubo.sun_color[2] = 0.45F; ubo.sun_color[3] = 0.75F;
        ubo.sky_zenith[0] = 0.06F; ubo.sky_zenith[1] = 0.10F; ubo.sky_zenith[2] = 0.24F; ubo.sky_zenith[3] = 1.18F;
        ubo.sky_horizon[0] = 0.56F; ubo.sky_horizon[1] = 0.30F; ubo.sky_horizon[2] = 0.22F; ubo.sky_horizon[3] = -1.0F;

        const float flk = 1.0F + 0.08F * std::sin(cam.sim_time * 9.5F);
        const std::array<std::array<float, 4>, 4> torch_pos {{
            { -3.6F, 0.35F,  2.1F, 8.5F },
            {  3.6F, 0.35F,  2.1F, 8.5F },
            { -3.6F, 1.10F, -2.6F, 9.0F },
            {  0.0F, 1.85F, -4.4F, 9.5F },
        }};
        const std::array<std::array<float, 4>, 4> torch_col {{
            { 1.0F, 0.58F, 0.18F, 3.6F * flk },
            { 1.0F, 0.58F, 0.18F, 3.6F * flk },
            { 1.0F, 0.62F, 0.22F, 3.4F * flk },
            { 0.25F, 0.92F, 1.0F, 3.8F },
        }};
        for (std::size_t i = 0U; i < 4U; ++i)
        {
            std::memcpy(ubo.point_pos_rad[i], torch_pos.at(i).data(), 4U * sizeof(float));
            std::memcpy(ubo.point_col_int[i], torch_col.at(i).data(), 4U * sizeof(float));
        }
    }
    else
    {
        // Crisp PBR Calibration & Gameplay Director 3D Studio Lighting
        ubo.sun_dir[0] = 0.48F; ubo.sun_dir[1] = 0.62F; ubo.sun_dir[2] = -0.62F; ubo.sun_dir[3] = 2.65F;
        ubo.sun_color[0] = 1.0F; ubo.sun_color[1] = 0.94F; ubo.sun_color[2] = 0.84F; ubo.sun_color[3] = 0.92F;
        ubo.sky_zenith[0] = 0.09F; ubo.sky_zenith[1] = 0.20F; ubo.sky_zenith[2] = 0.44F; ubo.sky_zenith[3] = 1.12F;
        ubo.sky_horizon[0] = 0.58F; ubo.sky_horizon[1] = 0.72F; ubo.sky_horizon[2] = 0.92F; ubo.sky_horizon[3] = -1.0F;

        const std::array<std::array<float, 4>, 4> lights_pos {{
            { -4.5F, 2.6F,  3.5F, 14.0F },
            {  4.5F, 2.6F,  3.5F, 14.0F },
            {  0.0F, 3.2F, -4.5F, 15.0F },
            {  0.0F, 0.8F,  0.0F,  9.0F },
        }};
        const std::array<std::array<float, 4>, 4> lights_col {{
            { 0.35F, 0.78F, 1.0F,  2.8F },
            { 1.0F,  0.65F, 0.30F, 2.8F },
            { 0.75F, 0.85F, 1.0F,  2.2F },
            { 0.25F, 0.92F, 0.85F, 1.6F },
        }};
        for (std::size_t i = 0U; i < 4U; ++i)
        {
            std::memcpy(ubo.point_pos_rad[i], lights_pos.at(i).data(), 4U * sizeof(float));
            std::memcpy(ubo.point_col_int[i], lights_col.at(i).data(), 4U * sizeof(float));
        }
    }

    const std::size_t slot = ring_cursor_ % kUboRingSize;
    ++ring_cursor_;
    (void)device_->upload_buffer(
        ubo_ring_.at(slot),
        0U,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(&ubo), sizeof(ubo)));

    // 2. Begin 3D Color + D32Float Depth Render Pass
    std::array<cd::rhi::ColorAttachmentInfo, 1> color_att {
        cd::rhi::ColorAttachmentInfo {
            .view        = swapchain_view,
            .load_op     = cd::rhi::LoadOp::kClear,
            .store_op    = cd::rhi::StoreOp::kStore,
            .clear_color = { .f32 = { 0.04F, 0.06F, 0.10F, 1.0F } } }
    };
    cd::rhi::DepthStencilAttachmentInfo depth_att {};
    depth_att.view        = depth_target_.view;
    depth_att.depth_load  = cd::rhi::LoadOp::kClear;
    depth_att.depth_store = cd::rhi::StoreOp::kStore;
    depth_att.clear.depth = 1.0F;

    cd::rhi::RenderPassBeginInfo rp {};
    rp.render_area       = cd::rhi::Rect2D { { 0, 0 }, extent };
    rp.color_attachments = color_att;
    rp.depth_stencil     = &depth_att;

    cmd.begin_render_pass(rp);
    cmd.set_viewport(cd::rhi::Viewport {
        0.0F, 0.0F,
        static_cast<float>(extent.width),
        static_cast<float>(extent.height),
        0.0F, 1.0F });
    cmd.set_scissor(cd::rhi::Rect2D { { 0, 0 }, extent });

    // Pass A: Fullscreen Atmospheric Sky
    cmd.bind_graphics_pipeline(sky_pipeline_);
    cmd.bind_descriptor_set(0U, scene_ds_ring_.at(slot));
    cmd.draw(3U, 1U, 0U, 0U);

    // Helper for drawing a 3D PBR mesh
    cmd.bind_graphics_pipeline(pbr_pipeline_);
    cmd.bind_descriptor_set(0U, scene_ds_ring_.at(slot));

    const auto draw_pbr = [&](const cd::render::GpuMesh& mesh,
                              float tx, float ty, float tz,
                              float yaw_r,
                              float sx, float sy, float sz,
                              std::array<float, 3> albedo,
                              float metallic,
                              float roughness,
                              float pattern_mode = 0.0F,
                              float uv_scale     = 1.0F,
                              std::array<float, 3> emissive = { 0.0F, 0.0F, 0.0F },
                              float emissive_str = 0.0F) {
        if (!mesh.vb.is_valid() || !mesh.ib.is_valid() || mesh.index_count == 0U)
        {
            return;
        }
        PbrPush push {};
        make_trs_y(tx, ty, tz, yaw_r, sx, sy, sz, push.model);
        push.base_color[0]   = albedo[0];
        push.base_color[1]   = albedo[1];
        push.base_color[2]   = albedo[2];
        push.base_color[3]   = pattern_mode;
        push.mat_params[0]   = metallic;
        push.mat_params[1]   = roughness;
        push.mat_params[2]   = emissive_str;
        push.mat_params[3]   = hd2d_mode ? 0.35F : 0.18F;
        push.extra_params[0] = emissive[0];
        push.extra_params[1] = emissive[1];
        push.extra_params[2] = emissive[2];
        push.extra_params[3] = uv_scale;

        cmd.bind_vertex_buffer(0U, mesh.vb, 0U);
        cmd.bind_index_buffer(mesh.ib, 0U, mesh.index_type);
        cmd.push_constants(
            pbr_layout_,
            cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
            0U,
            static_cast<std::uint32_t>(sizeof(PbrPush)),
            &push);
        cmd.draw_indexed(mesh.index_count, 1U, 0U, 0, 0U);
    };

    if (hd2d_mode)
    {
        // =====================================================================
        // HD-2D 3D Architectural Diorama (Stone Courtyard + Citadel + Pillars)
        // =====================================================================
        // Main chiseled stone courtyard slab
        draw_pbr(mesh_cube_, 0.0F, -1.25F, 0.0F, 0.0F, 22.0F, 0.50F, 18.0F,
                 { 0.30F, 0.33F, 0.40F }, 0.05F, 0.68F, 1.0F, 1.35F);

        // Raised stone citadel terrace & broad stairs in background
        draw_pbr(mesh_cube_, 0.0F, -0.62F, -4.6F, 0.0F, 16.0F, 0.80F, 5.2F,
                 { 0.34F, 0.36F, 0.44F }, 0.06F, 0.62F, 1.0F, 1.4F);
        draw_pbr(mesh_cube_, 0.0F, -0.82F, -1.75F, 0.0F, 5.2F, 0.38F, 0.9F,
                 { 0.38F, 0.40F, 0.48F }, 0.06F, 0.58F, 1.0F, 1.4F);
        draw_pbr(mesh_cube_, 0.0F, -0.58F, -2.35F, 0.0F, 5.2F, 0.38F, 0.9F,
                 { 0.38F, 0.40F, 0.48F }, 0.06F, 0.58F, 1.0F, 1.4F);

        // 3D Stone Archway framing the swirling pixel-art Goal Portal
        draw_pbr(mesh_cube_, -1.65F, 0.95F, -4.8F, 0.0F, 0.72F, 2.5F, 0.72F,
                 { 0.36F, 0.38F, 0.46F }, 0.08F, 0.55F, 1.0F, 1.5F);
        draw_pbr(mesh_cube_,  1.65F, 0.95F, -4.8F, 0.0F, 0.72F, 2.5F, 0.72F,
                 { 0.36F, 0.38F, 0.46F }, 0.08F, 0.55F, 1.0F, 1.5F);
        draw_pbr(mesh_cube_,  0.0F,  2.35F, -4.8F, 0.0F, 4.35F, 0.52F, 0.88F,
                 { 0.42F, 0.44F, 0.52F }, 0.12F, 0.48F, 1.0F, 1.5F);
        draw_pbr(mesh_torus_, 0.0F,  2.75F, -4.8F, cam.sim_time * 0.9F, 1.35F, 1.35F, 1.35F,
                 { 0.95F, 0.76F, 0.32F }, 0.92F, 0.20F, 0.0F, 1.0F, { 0.20F, 0.85F, 1.0F }, 0.35F);

        // 4 Carved 3D Courtyard Columns + Bronze Lantern Caps
        const std::array<std::array<float, 3>, 4> cols {{
            { -4.2F, -1.0F,  2.2F },
            {  4.2F, -1.0F,  2.2F },
            { -4.2F, -0.22F, -2.8F },
            {  4.2F, -0.22F, -2.8F },
        }};
        for (const auto& c : cols)
        {
            draw_pbr(mesh_cube_,     c[0], c[1] + 0.18F, c[2], 0.0F, 0.95F, 0.36F, 0.95F, { 0.36F, 0.38F, 0.45F }, 0.08F, 0.55F, 1.0F, 1.5F);
            draw_pbr(mesh_cylinder_, c[0], c[1] + 1.35F, c[2], 0.0F, 0.62F, 2.00F, 0.62F, { 0.44F, 0.46F, 0.54F }, 0.10F, 0.48F, 1.0F, 1.6F);
            draw_pbr(mesh_cube_,     c[0], c[1] + 2.42F, c[2], 0.0F, 0.92F, 0.24F, 0.92F, { 0.82F, 0.64F, 0.28F }, 0.85F, 0.26F);
            draw_pbr(mesh_sphere_,   c[0], c[1] + 2.74F, c[2], 0.0F, 0.32F, 0.32F, 0.32F,
                     { 1.0F, 0.72F, 0.28F }, 0.1F, 0.2F, 0.0F, 1.0F, { 1.0F, 0.58F, 0.16F }, 2.4F);
        }

        // Low castle parapet battlements on left/right flanks
        draw_pbr(mesh_cube_, -7.6F, -0.25F, -0.5F, 0.0F, 0.9F, 1.5F, 13.0F, { 0.28F, 0.30F, 0.38F }, 0.05F, 0.70F, 1.0F, 1.3F);
        draw_pbr(mesh_cube_,  7.6F, -0.25F, -0.5F, 0.0F, 0.9F, 1.5F, 13.0F, { 0.28F, 0.30F, 0.38F }, 0.05F, 0.70F, 1.0F, 1.3F);

        // =====================================================================
        // Pass B (HD-2D): Upright 2D Pixel-Art Sprite Cards in 3D Depth Space!
        // =====================================================================
        const float inv_w = 1.0F / static_cast<float>(std::max(1U, cpu_atlas_.width()));
        const float inv_h = 1.0F / static_cast<float>(std::max(1U, cpu_atlas_.height()));
        const auto region_uv = [&](std::string_view name) noexcept -> std::array<float, 4> {
            if (const auto* r = cpu_atlas_.find_region(name); r != nullptr)
            {
                const float eu = 0.25F * inv_w;
                const float ev = 0.25F * inv_h;
                return { r->uv.u0 + eu, r->uv.v0 + ev, r->uv.u1 - eu, r->uv.v1 - ev };
            }
            return { 0.0F, 0.0F, 1.0F, 1.0F };
        };

        struct Card3D
        {
            std::string_view     region;
            float                x;
            float                y;
            float                z;
            float                w;
            float                h;
            bool                 flip_x;
            float                emissive_boost;
            bool                 ground_shadow;
        };

        const std::uint32_t walk_phase = static_cast<std::uint32_t>(cam.sim_time * 5.0F) % 4U;
        const std::string_view hero_reg =
            (walk_phase == 1U) ? "hero_walk_0" : ((walk_phase == 3U) ? "hero_walk_1" : "hero_idle");

        const std::array<Card3D, 16> cards {{
            // Foreground Party: Playable Cyber-Knight Hero & Arcane Mage Companion
            { hero_reg,          -0.85F, -1.00F,  2.35F, 1.45F, 1.65F, false, 0.08F, true  },
            { "mage_companion",  -2.25F, -1.00F,  2.85F, 1.38F, 1.58F, false, 0.12F, true  },
            // Courtyard Patrol Enemies (Skeleton Guards & Hover Drones)
            { "skeleton_guard",   1.75F, -1.00F,  1.65F, 1.45F, 1.65F, true,  0.04F, true  },
            { "skeleton_guard",  -1.95F, -0.22F, -3.15F, 1.42F, 1.60F, false, 0.04F, true  },
            { "enemy_drone",      2.55F, -0.45F + 0.14F * std::sin(cam.sim_time * 3.2F), -0.45F, 1.25F, 1.25F, true, 0.22F, true },
            // Citadel Goal Portal + Treasure Chest + Golden Key + Crates
            { "portal_swirl",     0.00F, -0.22F, -4.65F, 2.05F, 2.25F, false, 0.95F, false },
            { "chest_gold",       2.35F, -0.22F, -3.55F, 1.25F, 1.15F, false, 0.18F, true  },
            { "key_golden",       0.00F, -0.75F + 0.08F * std::sin(cam.sim_time * 3.8F),  0.35F, 0.95F, 0.95F, false, 0.45F, true },
            { "crate_wood",      -2.95F, -1.00F,  0.45F, 1.05F, 1.05F, false, 0.00F, true  },
            // Lit Iron Torch Braziers next to 3D Stone Pillars
            { "torch_brazier",   -3.45F, -1.00F,  2.25F, 1.05F, 1.22F, false, 0.85F, true  },
            { "torch_brazier",    3.45F, -1.00F,  2.25F, 1.05F, 1.22F, false, 0.85F, true  },
            { "torch_brazier",   -3.45F, -0.22F, -2.65F, 1.05F, 1.22F, false, 0.85F, true  },
            // Pixel-Art Pine Trees flanking the 3D stone battlements
            { "tree_pine",       -5.85F, -1.00F,  0.65F, 2.25F, 2.75F, false, 0.02F, true  },
            { "tree_pine",        5.85F, -1.00F,  0.65F, 2.25F, 2.75F, true,  0.02F, true  },
            { "tree_pine",       -5.75F, -0.22F, -4.15F, 2.35F, 2.85F, false, 0.02F, true  },
            { "tree_pine",        5.75F, -0.22F, -4.15F, 2.35F, 2.85F, true,  0.02F, true  },
        }};

        // Contact shadow discs under upright 2D characters on the 3D stone floor
        for (const auto& c : cards)
        {
            if (!c.ground_shadow) continue;
            const float floor_y = (c.z < -2.4F) ? -0.21F : -0.99F;
            draw_pbr(mesh_cylinder_, c.x, floor_y, c.z, 0.0F, c.w * 0.72F, 0.02F, c.w * 0.46F,
                     { 0.03F, 0.03F, 0.05F }, 0.0F, 0.95F);
        }

        // Floating 3D golden embers around torches & portal
        for (std::uint32_t e = 0U; e < 12U; ++e)
        {
            const float fi    = static_cast<float>(e);
            const float phase = std::fmod(cam.sim_time * 0.65F + fi * 0.31F, 1.0F);
            const float ang   = fi * 1.618F + cam.sim_time * 0.9F;
            const float ex    = std::cos(ang) * (2.2F + 1.8F * std::sin(fi));
            const float ez    = std::sin(ang) * 2.4F - 0.8F;
            const float ey    = -0.5F + phase * 2.8F;
            const std::array<float, 3> ecol = (e % 3U == 0U)
                ? std::array<float, 3> { 0.25F, 0.92F, 1.0F }
                : std::array<float, 3> { 1.0F, 0.68F, 0.22F };
            draw_pbr(mesh_sphere_, ex, ey, ez, 0.0F, 0.09F, 0.09F, 0.09F, ecol, 0.0F, 0.2F, 0.0F, 1.0F, ecol, 3.2F);
        }

        cmd.bind_graphics_pipeline(sprite_pipeline_);
        cmd.bind_descriptor_set(0U, scene_ds_ring_.at(slot));
        cmd.bind_vertex_buffer(0U, mesh_quad_.vb, 0U);
        cmd.bind_index_buffer(mesh_quad_.ib, 0U, mesh_quad_.index_type);

        for (const auto& c : cards)
        {
            Sprite3DPush sp {};
            make_trs_y(c.x, c.y, c.z, orbit_yaw, c.w, c.h, 1.0F, sp.model);
            auto uv = region_uv(c.region);
            if (c.flip_x) std::swap(uv[0], uv[2]);
            sp.uv_rect[0]     = uv[0];
            sp.uv_rect[1]     = uv[1];
            sp.uv_rect[2]     = uv[2];
            sp.uv_rect[3]     = uv[3];
            sp.tint_params[0] = 1.0F;
            sp.tint_params[1] = 1.0F;
            sp.tint_params[2] = 1.0F;
            sp.tint_params[3] = c.emissive_boost;

            cmd.push_constants(
                sprite_layout_,
                cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
                0U,
                static_cast<std::uint32_t>(sizeof(Sprite3DPush)),
                &sp);
            cmd.draw_indexed(mesh_quad_.index_count, 1U, 0U, 0, 0U);
        }
    }
    else
    {
        // =====================================================================
        // Pure3D Cook-Torrance GGX PBR Material & Stage Element Showcase
        // =====================================================================
        // Expansive polished slate studio floor with 1m metric grid
        draw_pbr(mesh_cube_, 0.0F, -1.25F, 0.0F, 0.0F, 34.0F, 0.50F, 34.0F,
                 { 0.16F, 0.19F, 0.25F }, 0.12F, 0.28F, 2.0F, 1.0F);

        // Central two-tier turntable plinth + glowing cyan trim ring
        draw_pbr(mesh_cylinder_, 0.0F, -0.90F, 0.0F, 0.0F, 4.4F, 0.20F, 4.4F,
                 { 0.22F, 0.26F, 0.34F }, 0.65F, 0.24F);
        draw_pbr(mesh_cylinder_, 0.0F, -0.74F, 0.0F, 0.0F, 3.2F, 0.16F, 3.2F,
                 { 0.30F, 0.35F, 0.44F }, 0.78F, 0.18F);
        draw_pbr(mesh_torus_, 0.0F, -0.66F, 0.0F, 0.0F, 4.55F, 0.35F, 4.55F,
                 { 0.20F, 0.85F, 1.0F }, 0.2F, 0.2F, 0.0F, 1.0F, { 0.18F, 0.82F, 1.0F }, 1.8F);

        // Hero Centerpiece: Mirror Chrome Sphere inside Rotating Brushed-Gold Torus Gyro
        draw_pbr(mesh_sphere_, 0.0F, 0.52F, 0.0F, 0.0F, 1.65F, 1.65F, 1.65F,
                 { 0.94F, 0.96F, 0.99F }, 1.0F, 0.06F);
        draw_pbr(mesh_torus_, 0.0F, 0.52F, 0.0F, cam.sim_time * 1.1F, 3.4F, 3.4F, 3.4F,
                 { 1.0F, 0.78F, 0.32F }, 1.0F, 0.18F);

        // PBR Material Calibration Ring (driven by Stage elements + reference PBR swatches)
        struct SwatchSpec
        {
            std::array<float, 3> albedo;
            float                metallic;
            float                roughness;
        };
        static constexpr std::array<SwatchSpec, 6> kSwatches {{
            { { 1.00F, 0.78F, 0.30F }, 1.00F, 0.14F },  // Gold
            { { 0.92F, 0.16F, 0.20F }, 0.04F, 0.12F },  // Glossy Crimson Dielectric
            { { 0.96F, 0.54F, 0.34F }, 1.00F, 0.26F },  // Brushed Copper
            { { 0.20F, 0.62F, 0.98F }, 0.88F, 0.18F },  // Anodized Cobalt
            { { 0.94F, 0.94F, 0.92F }, 0.02F, 0.22F },  // Ceramic Ivory
            { { 0.22F, 0.88F, 0.58F }, 0.75F, 0.24F },  // Emerald Alloy
        }};

        const auto& children = stage.root().children();
        const std::size_t count = std::max<std::size_t>(6U, std::min<std::size_t>(children.size(), 8U));
        for (std::size_t i = 0U; i < count; ++i)
        {
            const float ang = (static_cast<float>(i) / static_cast<float>(count)) * 6.2831853F + 0.35F;
            const float rx  = std::cos(ang) * 4.35F;
            const float rz  = std::sin(ang) * 4.35F;
            const auto& sw  = kSwatches.at(i % kSwatches.size());

            // Pedestal column
            draw_pbr(mesh_cylinder_, rx, -0.52F, rz, 0.0F, 0.92F, 0.96F, 0.92F,
                     { 0.28F, 0.32F, 0.40F }, 0.35F, 0.42F);

            // Stage-driven mesh variation
            const float bob = 0.06F * std::sin(cam.sim_time * 2.2F + static_cast<float>(i));
            if (i < children.size() && children.at(i))
            {
                const std::string_view cn = children.at(i)->class_name();
                if (cn.find("Light") != std::string_view::npos || cn.find("Sun") != std::string_view::npos)
                {
                    draw_pbr(mesh_sphere_, rx, 0.56F + bob, rz, 0.0F, 0.92F, 0.92F, 0.92F,
                             { 1.0F, 0.86F, 0.42F }, 0.2F, 0.15F, 0.0F, 1.0F, { 1.0F, 0.78F, 0.30F }, 1.8F);
                    continue;
                }
                if (cn.find("Agent") != std::string_view::npos || cn.find("Pilot") != std::string_view::npos)
                {
                    draw_pbr(mesh_cone_, rx, 0.58F + bob, rz, cam.sim_time, 0.95F, 1.25F, 0.95F,
                             sw.albedo, sw.metallic, sw.roughness);
                    continue;
                }
            }
            draw_pbr(mesh_sphere_, rx, 0.56F + bob, rz, 0.0F, 1.05F, 1.05F, 1.05F,
                     sw.albedo, sw.metallic, sw.roughness);
        }

        // Architectural background pillars framing the studio horizon
        for (int p = -2; p <= 2; ++p)
        {
            if (p == 0) continue;
            const float px = static_cast<float>(p) * 3.2F;
            draw_pbr(mesh_cube_, px, 0.65F, -8.2F, 0.0F, 0.85F, 3.3F, 0.85F,
                     { 0.32F, 0.36F, 0.45F }, 0.25F, 0.45F);
        }
    }

    cmd.end_render_pass();
    return true;
}

}  // namespace cd::player
