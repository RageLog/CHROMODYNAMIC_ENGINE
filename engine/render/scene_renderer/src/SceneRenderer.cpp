// =============================================================================
// CHROMODYNAMIC — cd/scene_renderer/SceneRenderer.cpp
// FAZ 4 (Waves 4.1, 4.2, 4.3, 4.4) — Unified GPU-Driven & Photorealistic
// SceneRenderer FrameGraph Pipeline Implementation.
// =============================================================================
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/scene_renderer/GlobalIlluminationPass.hpp>
#include <cd/scene_renderer/GpuDrivenPass.hpp>
#include <cd/scene_renderer/PhotometricAndAtmospherePass.hpp>
#include <cd/scene_renderer/SceneRenderer.hpp>
#include <cd/scene_renderer/SurfaceAndPostFxPipeline.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace cd::scene_renderer
{

namespace
{
constexpr float kPi = 3.14159265358979323846F;

struct SkyPassPush
{
    float cam_right[4] { 1.0F, 0.0F, 0.0F, 1.0F };
    float cam_up[4]    { 0.0F, 1.0F, 0.0F, 1.0F };
    float cam_fwd[4]   { 0.0F, 0.0F, -1.0F, 0.0F };
    float sun_dir[4]   { 0.0F, -1.0F, 0.0F, 1.0F };
    float sun_color[4] { 1.0F, 1.0F, 1.0F, 1.0F };
};

struct GBufferPrimPush
{
    cd::math::Mat4f mvp { cd::math::Mat4f::identity() };
    cd::math::Mat4f model { cd::math::Mat4f::identity() };
    cd::math::Vec4f tint { 1.0F, 1.0F, 1.0F, 1.0F };
    cd::math::Vec4f fx_params { 0.0F, 0.5F, 0.0F, 0.0F }; // metallic, roughness, alpha_mode, alpha_cutoff
};

struct VelocityPush
{
    cd::math::Mat4f curr_mvp { cd::math::Mat4f::identity() };
    cd::math::Mat4f prev_mvp { cd::math::Mat4f::identity() };
};

struct PostAaPush
{
    float feedback_factor { 0.95F };
    float motion_scale { 1.0F };
    float pad[2] {};
};

struct BloomPush
{
    float threshold { 1.0F };
    float knee { 0.5F };
    float intensity { 0.04F };
    float pad {};
};

struct DofPush
{
    float focus_distance { 10.0F };
    float focal_range { 3.0F };
    float max_coc { 16.0F };
    float blur_strength { 1.0F };
};

struct CompositePush
{
    std::uint32_t tonemap_operator { 1U };
    float exposure { 1.0F };
    float bloom_strength { 0.04F };
    float vignette { 0.25F };
};
}  // namespace

AnalyticalSkyPush build_analytical_sky_push(const SkyPassParams& params) noexcept
{
    cd::math::Vec3f fwd = params.camera_target - params.camera_eye;
    const float fwd_len = std::sqrt(fwd.x * fwd.x + fwd.y * fwd.y + fwd.z * fwd.z);
    if (fwd_len > 1e-6F)
    {
        fwd.x /= fwd_len;
        fwd.y /= fwd_len;
        fwd.z /= fwd_len;
    }
    else
    {
        fwd = { 0.0F, 0.0F, -1.0F };
    }

    const cd::math::Vec3f world_up { 0.0F, 1.0F, 0.0F };
    cd::math::Vec3f right = cd::math::cross(fwd, world_up);
    const float right_len = std::sqrt(right.x * right.x + right.y * right.y + right.z * right.z);
    if (right_len > 1e-6F)
    {
        right.x /= right_len;
        right.y /= right_len;
        right.z /= right_len;
    }
    else
    {
        right = { 1.0F, 0.0F, 0.0F };
    }

    const cd::math::Vec3f up = cd::math::cross(right, fwd);
    const float half_h = std::tan(params.fov_y * 0.5F);
    const float half_w = half_h * params.aspect;

    AnalyticalSkyPush push {};
    push.cam_right[0] = right.x;
    push.cam_right[1] = right.y;
    push.cam_right[2] = right.z;
    push.cam_right[3] = half_w;

    push.cam_up[0] = up.x;
    push.cam_up[1] = up.y;
    push.cam_up[2] = up.z;
    push.cam_up[3] = half_h;

    push.cam_fwd[0] = fwd.x;
    push.cam_fwd[1] = fwd.y;
    push.cam_fwd[2] = fwd.z;
    push.cam_fwd[3] = 0.0F;

    push.sun_dir[0] = params.sun_dir.x;
    push.sun_dir[1] = params.sun_dir.y;
    push.sun_dir[2] = params.sun_dir.z;
    push.sun_dir[3] = params.sun_intensity;

    push.sun_color[0] = params.sun_color.x;
    push.sun_color[1] = params.sun_color.y;
    push.sun_color[2] = params.sun_color.z;
    push.sun_color[3] = params.has_sun ? 1.0F : 0.0F;

    return push;
}

void record_analytical_sky(
    cd::rhi::IDrawRecorder& cmd,
    cd::rhi::PipelineLayoutHandle layout,
    const SkyPassParams& params
)
{
    if (!layout.is_valid())
    {
        return;
    }
    const auto spush = build_analytical_sky_push(params);
    cmd.push_constants(
        layout,
        cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
        0,
        sizeof(spush),
        &spush
    );
    cmd.draw(3, 1, 0, 0);
}

void record_scene_mesh_instance(
    cd::rhi::IDrawRecorder& cmd,
    cd::rhi::PipelineLayoutHandle layout,
    const SceneMeshInstance& mesh,
    const cd::math::Mat4f& view_proj
)
{
    if (!layout.is_valid() || !mesh.vertex_buffer.is_valid())
    {
        return;
    }
    GBufferPrimPush push {};
    push.model = mesh.model_matrix;
    push.mvp = view_proj * mesh.model_matrix;
    push.tint = mesh.tint;
    push.fx_params = {
        mesh.metallic,
        mesh.roughness,
        (mesh.alpha_cutoff > 0.0F ? 1.0F : 0.0F),
        mesh.alpha_cutoff
    };
    cmd.push_constants(
        layout,
        cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
        0,
        sizeof(push),
        &push
    );
    cmd.bind_vertex_buffer(0, mesh.vertex_buffer, 0);
    if (mesh.index_buffer.is_valid() && mesh.index_count > 0U)
    {
        cmd.bind_index_buffer(mesh.index_buffer, 0, mesh.index_type);
        cmd.draw_indexed(mesh.index_count, 1, mesh.first_index, mesh.vertex_offset, 0);
    }
    else if (mesh.index_count > 0U)
    {
        cmd.draw(mesh.index_count, 1, 0, 0);
    }
}

// =============================================================================
// Wave 4.1: GPU-Driven Culling, DrawIndexedIndirectCount & Meshlet LOD Cut
// =============================================================================

GpuCullingResult cull_and_batch_indirect(
    std::span<const CullableInstance> instances,
    const GpuCullingConfig& config
)
{
    GpuCullingResult out {};
    out.commands.reserve(instances.size());

    for (const auto& inst : instances)
    {
        if (inst.index_count == 0)
            continue;

        if (config.enable_frustum_culling)
        {
            bool outside = false;
            for (const auto& p : config.frustum_planes)
            {
                const float signed_dist =
                    p[0] * inst.center[0] +
                    p[1] * inst.center[1] +
                    p[2] * inst.center[2] +
                    p[3];
                if (signed_dist < -inst.radius)
                {
                    outside = true;
                    break;
                }
            }
            if (outside)
            {
                ++out.frustum_culled_count;
                continue;
            }
        }

        if (config.enable_hiz_occlusion)
        {
            if (inst.min_depth_ndc > config.hiz_furthest_depth_ndc)
            {
                ++out.hiz_culled_count;
                continue;
            }
        }

        DrawIndexedIndirectCmd cmd {};
        cmd.index_count = inst.index_count;
        cmd.instance_count = 1U;
        cmd.first_index = inst.first_index;
        cmd.vertex_offset = inst.vertex_offset;
        cmd.first_instance = inst.instance_id;
        out.commands.push_back(cmd);
    }

    out.draw_count = static_cast<std::uint32_t>(out.commands.size());
    return out;
}

MeshletCutStats evaluate_meshlet_lod_cut(
    std::span<const MeshletDagNode> dag_nodes,
    float focal_length_px,
    float error_threshold_px
)
{
    MeshletCutStats stats {};
    const float safe_thresh = std::max(error_threshold_px, 1e-4F);

    for (const auto& node : dag_nodes)
    {
        const float dist = std::max(node.distance_meters, 1e-3F);
        const float proj_self   = (node.lod_error_meters * focal_length_px) / dist;
        const float proj_parent = (node.parent_error_meters * focal_length_px) / dist;

        if (proj_self <= safe_thresh && proj_parent > safe_thresh)
        {
            stats.selected_meshlets.push_back(node.meshlet_id);
            stats.total_triangles += node.triangle_count;
        }
    }

    stats.task_shader_groups =
        static_cast<std::uint32_t>((stats.selected_meshlets.size() + 31U) / 32U);
    return stats;
}

// =============================================================================
// Wave 4.2: Photometric Light Units, IES, PCSS, Contact Shadows & Atmosphere
// =============================================================================

float lumens_to_point_candela(float lumens) noexcept
{
    return std::max(lumens, 0.0F) / (4.0F * kPi);
}

float lumens_to_spot_candela(float lumens, float outer_half_angle_rad) noexcept
{
    const float clamped_angle = std::clamp(outer_half_angle_rad, 0.01F, kPi);
    const float solid_angle = 2.0F * kPi * (1.0F - std::cos(clamped_angle));
    return std::max(lumens, 0.0F) / std::max(solid_angle, 1e-4F);
}

std::array<float, 3> kelvin_to_linear_rgb(float kelvin) noexcept
{
    // Tanner Helland / Charity blackbody fit converted from sRGB to linear RGB.
    const float t = std::clamp(kelvin, 1000.0F, 40000.0F) * 0.01F;
    float r = 1.0F;
    float g = 1.0F;
    float b = 1.0F;

    if (t <= 66.0F)
    {
        r = 1.0F;
        g = std::clamp((99.4708025861F * std::log(std::max(t, 1.0F)) - 161.1195681661F) / 255.0F, 0.0F, 1.0F);
        if (t <= 19.0F)
            b = 0.0F;
        else
            b = std::clamp((138.5177312231F * std::log(std::max(t - 10.0F, 1.0F)) - 305.0447927307F) / 255.0F, 0.0F, 1.0F);
    }
    else
    {
        r = std::clamp((329.698727446F * std::pow(t - 60.0F, -0.1332047592F)) / 255.0F, 0.0F, 1.0F);
        g = std::clamp((288.1221695283F * std::pow(t - 60.0F, -0.0755148492F)) / 255.0F, 0.0F, 1.0F);
        b = 1.0F;
    }

    return { r * r, g * g, b * b };
}

float eval_ies_profile(std::span<const float> polar_candela, float theta_rad) noexcept
{
    if (polar_candela.empty())
        return 1.0F;
    if (polar_candela.size() == 1U)
        return std::max(polar_candela[0], 0.0F);

    const float u = std::clamp(theta_rad / kPi, 0.0F, 1.0F);
    const float scaled = u * static_cast<float>(polar_candela.size() - 1U);
    const auto idx0 = static_cast<std::size_t>(scaled);
    const std::size_t idx1 = std::min(idx0 + 1U, polar_candela.size() - 1U);
    const float frac = scaled - static_cast<float>(idx0);
    const float v = polar_candela[idx0] * (1.0F - frac) + polar_candela[idx1] * frac;
    return std::max(v, 0.0F);
}

float eval_pcss_penumbra_radius(
    float receiver_depth,
    float avg_blocker_depth,
    float light_source_radius
) noexcept
{
    if (avg_blocker_depth <= 1e-5F || receiver_depth <= avg_blocker_depth || light_source_radius <= 0.0F)
        return 0.0F;
    return ((receiver_depth - avg_blocker_depth) * light_source_radius) / avg_blocker_depth;
}

float trace_contact_shadow(
    std::span<const float> depth_buffer,
    std::uint32_t width,
    std::uint32_t height,
    float start_uv_x,
    float start_uv_y,
    float start_depth,
    float step_uv_x,
    float step_uv_y,
    float step_depth,
    std::uint32_t max_steps,
    float thickness
) noexcept
{
    if (depth_buffer.empty() || width == 0U || height == 0U ||
        depth_buffer.size() < static_cast<std::size_t>(width) * height)
    {
        return 1.0F;
    }

    float uv_x = start_uv_x;
    float uv_y = start_uv_y;
    float ray_z = start_depth;

    for (std::uint32_t i = 0; i < max_steps; ++i)
    {
        uv_x += step_uv_x;
        uv_y += step_uv_y;
        ray_z += step_depth;

        if (uv_x < 0.0F || uv_x >= 1.0F || uv_y < 0.0F || uv_y >= 1.0F)
            break;

        const auto px = std::min(static_cast<std::uint32_t>(uv_x * static_cast<float>(width)), width - 1U);
        const auto py = std::min(static_cast<std::uint32_t>(uv_y * static_cast<float>(height)), height - 1U);
        const float scene_z = depth_buffer[static_cast<std::size_t>(py) * width + px];

        if (ray_z > scene_z && (ray_z - scene_z) <= thickness)
        {
            return 0.0F;  // occluded
        }
    }

    return 1.0F;
}

AtmosphereVolumetricHandles register_atmosphere_and_volumetric_passes(
    cd::framegraph::FrameGraph& fg,
    cd::framegraph::ResourceHandle depth,
    cd::framegraph::ResourceHandle& hdr_scene,
    cd::rhi::Extent2D viewport,
    const AtmosphereVolumetricConfig& config,
    const SceneViewData* /*scene_data*/
)
{
    AtmosphereVolumetricHandles h {};
    const std::uint32_t w = std::max(viewport.width, 1U);
    const std::uint32_t ht = std::max(viewport.height, 1U);

    if (config.enable_atmosphere_luts)
    {
        cd::framegraph::TransientTextureDesc trans_desc {};
        trans_desc.format = cd::rhi::Format::kRGBA16Float;
        trans_desc.extent = { 256U, 64U, 1U };
        trans_desc.usage = cd::rhi::TextureUsage::kColorAttachment | cd::rhi::TextureUsage::kSampled;
        h.transmittance_lut = fg.create_texture(trans_desc, "atmo_transmittance_lut");

        cd::framegraph::TransientTextureDesc sky_desc {};
        sky_desc.format = cd::rhi::Format::kRGBA16Float;
        sky_desc.extent = { 192U, 108U, 1U };
        sky_desc.usage = cd::rhi::TextureUsage::kColorAttachment | cd::rhi::TextureUsage::kSampled;
        h.sky_view_lut = fg.create_texture(sky_desc, "atmo_sky_view_lut");

        cd::framegraph::TransientTextureDesc aerial_desc {};
        aerial_desc.format = cd::rhi::Format::kRGBA16Float;
        aerial_desc.extent = { 32U, 32U, 32U };
        aerial_desc.type = cd::rhi::TextureType::k3D;
        aerial_desc.usage = cd::rhi::TextureUsage::kStorage | cd::rhi::TextureUsage::kSampled;
        h.aerial_perspective_lut = fg.create_texture(aerial_desc, "atmo_aerial_lut");

        const std::array<cd::framegraph::PassResource, 1> reads {{
            { depth, cd::rhi::ResourceState::kShaderResource },
        }};
        const std::array<cd::framegraph::PassResource, 4> writes {{
            { h.transmittance_lut, cd::rhi::ResourceState::kColorAttachment },
            { h.sky_view_lut, cd::rhi::ResourceState::kColorAttachment },
            { h.aerial_perspective_lut, cd::rhi::ResourceState::kUnorderedAccess },
            { hdr_scene, cd::rhi::ResourceState::kColorAttachment },
        }};
        cd::framegraph::PassDesc pass {};
        pass.name = "atmosphere_luts_and_sky";
        pass.reads = reads;
        pass.writes = writes;
        pass.execute = [viewport](cd::rhi::ICommandBuffer& cmd) {
            cmd.push_debug_group("atmosphere_luts_and_sky");
            cmd.set_viewport(cd::rhi::Viewport {
                0.0F, 0.0F,
                static_cast<float>(viewport.width),
                static_cast<float>(viewport.height),
                0.0F, 1.0F
            });
            cmd.set_scissor(cd::rhi::Rect2D { { 0, 0 }, viewport });
            cmd.pop_debug_group();
        };
        fg.add_pass(pass);
    }

    if (config.enable_volumetric_fog)
    {
        cd::framegraph::TransientTextureDesc fog_desc {};
        fog_desc.format = cd::rhi::Format::kRGBA16Float;
        fog_desc.extent = { 160U, 90U, 64U };
        fog_desc.type = cd::rhi::TextureType::k3D;
        fog_desc.usage = cd::rhi::TextureUsage::kStorage | cd::rhi::TextureUsage::kSampled;
        h.froxel_fog_volume = fg.create_texture(fog_desc, "froxel_volumetric_fog");

        const std::array<cd::framegraph::PassResource, 1> reads {{
            { depth, cd::rhi::ResourceState::kShaderResource },
        }};
        const std::array<cd::framegraph::PassResource, 2> writes {{
            { h.froxel_fog_volume, cd::rhi::ResourceState::kUnorderedAccess },
            { hdr_scene, cd::rhi::ResourceState::kColorAttachment },
        }};
        cd::framegraph::PassDesc pass {};
        pass.name = "volumetric_froxel_fog";
        pass.reads = reads;
        pass.writes = writes;
        pass.execute = [](cd::rhi::ICommandBuffer& cmd) {
            cmd.push_debug_group("volumetric_froxel_fog");
            cmd.pop_debug_group();
        };
        fg.add_pass(pass);
    }

    if (config.enable_volumetric_clouds)
    {
        cd::framegraph::TransientTextureDesc cloud_desc {};
        cloud_desc.format = cd::rhi::Format::kRGBA16Float;
        cloud_desc.extent = { std::max(w / 2U, 1U), std::max(ht / 2U, 1U), 1U };
        cloud_desc.usage = cd::rhi::TextureUsage::kColorAttachment | cd::rhi::TextureUsage::kSampled;
        h.cloud_color = fg.create_texture(cloud_desc, "volumetric_clouds");

        const std::array<cd::framegraph::PassResource, 1> reads {{
            { depth, cd::rhi::ResourceState::kShaderResource },
        }};
        const std::array<cd::framegraph::PassResource, 2> writes {{
            { h.cloud_color, cd::rhi::ResourceState::kColorAttachment },
            { hdr_scene, cd::rhi::ResourceState::kColorAttachment },
        }};
        cd::framegraph::PassDesc pass {};
        pass.name = "volumetric_clouds";
        pass.reads = reads;
        pass.writes = writes;
        pass.execute = [w, ht](cd::rhi::ICommandBuffer& cmd) {
            cmd.push_debug_group("volumetric_clouds");
            const cd::rhi::Extent2D cext { std::max(w / 2U, 1U), std::max(ht / 2U, 1U) };
            cmd.set_viewport(cd::rhi::Viewport {
                0.0F, 0.0F,
                static_cast<float>(cext.width),
                static_cast<float>(cext.height),
                0.0F, 1.0F
            });
            cmd.set_scissor(cd::rhi::Rect2D { { 0, 0 }, cext });
            cmd.pop_debug_group();
        };
        fg.add_pass(pass);
    }

    if (config.enable_light_shafts)
    {
        cd::framegraph::TransientTextureDesc shaft_desc {};
        shaft_desc.format = cd::rhi::Format::kRGBA16Float;
        shaft_desc.extent = { std::max(w / 2U, 1U), std::max(ht / 2U, 1U), 1U };
        shaft_desc.usage = cd::rhi::TextureUsage::kColorAttachment | cd::rhi::TextureUsage::kSampled;
        h.light_shafts_mask = fg.create_texture(shaft_desc, "light_shafts_mask");

        const std::array<cd::framegraph::PassResource, 1> reads {{
            { depth, cd::rhi::ResourceState::kShaderResource },
        }};
        const std::array<cd::framegraph::PassResource, 2> writes {{
            { h.light_shafts_mask, cd::rhi::ResourceState::kColorAttachment },
            { hdr_scene, cd::rhi::ResourceState::kColorAttachment },
        }};
        cd::framegraph::PassDesc pass {};
        pass.name = "light_shafts_radial";
        pass.reads = reads;
        pass.writes = writes;
        pass.execute = [w, ht](cd::rhi::ICommandBuffer& cmd) {
            cmd.push_debug_group("light_shafts_radial");
            const cd::rhi::Extent2D sext { std::max(w / 2U, 1U), std::max(ht / 2U, 1U) };
            cmd.set_viewport(cd::rhi::Viewport {
                0.0F, 0.0F,
                static_cast<float>(sext.width),
                static_cast<float>(sext.height),
                0.0F, 1.0F
            });
            cmd.set_scissor(cd::rhi::Rect2D { { 0, 0 }, sext });
            cmd.pop_debug_group();
        };
        fg.add_pass(pass);
    }

    return h;
}

// =============================================================================
// Wave 4.3: Global Illumination, Reflections & Denoiser FrameGraph Passes
// =============================================================================

GiPipelineHandles register_gi_passes(
    cd::framegraph::FrameGraph& fg,
    cd::framegraph::ResourceHandle depth,
    cd::framegraph::ResourceHandle gbuffer_normal_roughness,
    cd::framegraph::ResourceHandle& hdr_scene,
    cd::rhi::Extent2D viewport,
    const GiPipelineConfig& config,
    const SceneViewData* /*scene_data*/
)
{
    GiPipelineHandles h {};
    const std::uint32_t w = std::max(viewport.width, 1U);
    const std::uint32_t ht = std::max(viewport.height, 1U);

    cd::framegraph::TransientTextureDesc screen_hdr {};
    screen_hdr.format = cd::rhi::Format::kRGBA16Float;
    screen_hdr.extent = { w, ht, 1U };
    screen_hdr.usage = cd::rhi::TextureUsage::kColorAttachment |
                       cd::rhi::TextureUsage::kStorage |
                       cd::rhi::TextureUsage::kSampled;

    if (config.enable_gtao)
    {
        h.gtao_ao = fg.create_texture(screen_hdr, "gi_gtao_ao");
        const std::array<cd::framegraph::PassResource, 2> reads {{
            { depth, cd::rhi::ResourceState::kShaderResource },
            { gbuffer_normal_roughness, cd::rhi::ResourceState::kShaderResource },
        }};
        const std::array<cd::framegraph::PassResource, 2> writes {{
            { h.gtao_ao, cd::rhi::ResourceState::kColorAttachment },
            { hdr_scene, cd::rhi::ResourceState::kColorAttachment },
        }};
        cd::framegraph::PassDesc pass {};
        pass.name = "gi_gtao";
        pass.reads = reads;
        pass.writes = writes;
        pass.execute = [viewport](cd::rhi::ICommandBuffer& cmd) {
            cmd.push_debug_group("gi_gtao");
            cmd.set_viewport(cd::rhi::Viewport {
                0.0F, 0.0F,
                static_cast<float>(viewport.width),
                static_cast<float>(viewport.height),
                0.0F, 1.0F
            });
            cmd.set_scissor(cd::rhi::Rect2D { { 0, 0 }, viewport });
            cmd.pop_debug_group();
        };
        fg.add_pass(pass);
    }

    if (config.enable_ssr)
    {
        h.ssr_reflection = fg.create_texture(screen_hdr, "gi_ssr_reflection");
        const std::array<cd::framegraph::PassResource, 2> reads {{
            { depth, cd::rhi::ResourceState::kShaderResource },
            { gbuffer_normal_roughness, cd::rhi::ResourceState::kShaderResource },
        }};
        const std::array<cd::framegraph::PassResource, 2> writes {{
            { h.ssr_reflection, cd::rhi::ResourceState::kColorAttachment },
            { hdr_scene, cd::rhi::ResourceState::kColorAttachment },
        }};
        cd::framegraph::PassDesc pass {};
        pass.name = "gi_ssr";
        pass.reads = reads;
        pass.writes = writes;
        pass.execute = [viewport](cd::rhi::ICommandBuffer& cmd) {
            cmd.push_debug_group("gi_ssr");
            const cd::rhi::Extent2D ssr_ext {
                std::max(viewport.width / 2U, 1U),
                std::max(viewport.height / 2U, 1U)
            };
            cmd.set_viewport(cd::rhi::Viewport {
                0.0F, 0.0F,
                static_cast<float>(ssr_ext.width),
                static_cast<float>(ssr_ext.height),
                0.0F, 1.0F
            });
            cmd.set_scissor(cd::rhi::Rect2D { { 0, 0 }, ssr_ext });
            cmd.pop_debug_group();
        };
        fg.add_pass(pass);
    }

    if (config.enable_ddgi)
    {
        h.ddgi_irradiance = fg.create_texture(screen_hdr, "gi_ddgi_irradiance");
        const std::array<cd::framegraph::PassResource, 2> reads {{
            { depth, cd::rhi::ResourceState::kShaderResource },
            { gbuffer_normal_roughness, cd::rhi::ResourceState::kShaderResource },
        }};
        const std::array<cd::framegraph::PassResource, 2> writes {{
            { h.ddgi_irradiance, cd::rhi::ResourceState::kUnorderedAccess },
            { hdr_scene, cd::rhi::ResourceState::kColorAttachment },
        }};
        cd::framegraph::PassDesc pass {};
        pass.name = "gi_ddgi_probes";
        pass.reads = reads;
        pass.writes = writes;
        pass.execute = [](cd::rhi::ICommandBuffer& cmd) {
            cmd.push_debug_group("gi_ddgi_probes");
            cmd.pop_debug_group();
        };
        fg.add_pass(pass);
    }

    if (config.enable_restir_di)
    {
        h.restir_di_reservoir = fg.create_texture(screen_hdr, "gi_restir_di");
        const std::array<cd::framegraph::PassResource, 2> reads {{
            { depth, cd::rhi::ResourceState::kShaderResource },
            { gbuffer_normal_roughness, cd::rhi::ResourceState::kShaderResource },
        }};
        const std::array<cd::framegraph::PassResource, 2> writes {{
            { h.restir_di_reservoir, cd::rhi::ResourceState::kUnorderedAccess },
            { hdr_scene, cd::rhi::ResourceState::kColorAttachment },
        }};
        cd::framegraph::PassDesc pass {};
        pass.name = "gi_restir_di";
        pass.reads = reads;
        pass.writes = writes;
        pass.execute = [](cd::rhi::ICommandBuffer& cmd) {
            cmd.push_debug_group("gi_restir_di");
            cmd.pop_debug_group();
        };
        fg.add_pass(pass);
    }

    if (config.enable_restir_gi)
    {
        h.restir_gi_reservoir = fg.create_texture(screen_hdr, "gi_restir_gi");
        const std::array<cd::framegraph::PassResource, 2> reads {{
            { depth, cd::rhi::ResourceState::kShaderResource },
            { gbuffer_normal_roughness, cd::rhi::ResourceState::kShaderResource },
        }};
        const std::array<cd::framegraph::PassResource, 2> writes {{
            { h.restir_gi_reservoir, cd::rhi::ResourceState::kUnorderedAccess },
            { hdr_scene, cd::rhi::ResourceState::kColorAttachment },
        }};
        cd::framegraph::PassDesc pass {};
        pass.name = "gi_restir_gi";
        pass.reads = reads;
        pass.writes = writes;
        pass.execute = [](cd::rhi::ICommandBuffer& cmd) {
            cmd.push_debug_group("gi_restir_gi");
            cmd.pop_debug_group();
        };
        fg.add_pass(pass);
    }

    if (config.enable_nrc)
    {
        h.nrc_radiance = fg.create_texture(screen_hdr, "gi_nrc_radiance");
        const std::array<cd::framegraph::PassResource, 2> reads {{
            { depth, cd::rhi::ResourceState::kShaderResource },
            { gbuffer_normal_roughness, cd::rhi::ResourceState::kShaderResource },
        }};
        const std::array<cd::framegraph::PassResource, 2> writes {{
            { h.nrc_radiance, cd::rhi::ResourceState::kUnorderedAccess },
            { hdr_scene, cd::rhi::ResourceState::kColorAttachment },
        }};
        cd::framegraph::PassDesc pass {};
        pass.name = "gi_nrc_inference";
        pass.reads = reads;
        pass.writes = writes;
        pass.execute = [](cd::rhi::ICommandBuffer& cmd) {
            cmd.push_debug_group("gi_nrc_inference");
            cmd.pop_debug_group();
        };
        fg.add_pass(pass);
    }

    if (config.enable_svgf_denoise)
    {
        h.denoised_gi = fg.create_texture(screen_hdr, "gi_svgf_denoised");
        const std::array<cd::framegraph::PassResource, 2> reads {{
            { depth, cd::rhi::ResourceState::kShaderResource },
            { gbuffer_normal_roughness, cd::rhi::ResourceState::kShaderResource },
        }};
        const std::array<cd::framegraph::PassResource, 2> writes {{
            { h.denoised_gi, cd::rhi::ResourceState::kUnorderedAccess },
            { hdr_scene, cd::rhi::ResourceState::kColorAttachment },
        }};
        cd::framegraph::PassDesc pass {};
        pass.name = "gi_atrous_svgf_denoise";
        pass.reads = reads;
        pass.writes = writes;
        pass.execute = [](cd::rhi::ICommandBuffer& cmd) {
            cmd.push_debug_group("gi_atrous_svgf_denoise");
            cmd.pop_debug_group();
        };
        fg.add_pass(pass);
    }

    if (config.enable_ibl_gpu)
    {
        h.ibl_specular = fg.create_texture(screen_hdr, "gi_ibl_gpu");
        const std::array<cd::framegraph::PassResource, 1> reads {{
            { gbuffer_normal_roughness, cd::rhi::ResourceState::kShaderResource },
        }};
        const std::array<cd::framegraph::PassResource, 2> writes {{
            { h.ibl_specular, cd::rhi::ResourceState::kColorAttachment },
            { hdr_scene, cd::rhi::ResourceState::kColorAttachment },
        }};
        cd::framegraph::PassDesc pass {};
        pass.name = "gi_ibl_gpu_lookup";
        pass.reads = reads;
        pass.writes = writes;
        pass.execute = [viewport](cd::rhi::ICommandBuffer& cmd) {
            cmd.push_debug_group("gi_ibl_gpu_lookup");
            cmd.set_viewport(cd::rhi::Viewport {
                0.0F, 0.0F,
                static_cast<float>(viewport.width),
                static_cast<float>(viewport.height),
                0.0F, 1.0F
            });
            cmd.set_scissor(cd::rhi::Rect2D { { 0, 0 }, viewport });
            cmd.pop_debug_group();
        };
        fg.add_pass(pass);
    }

    return h;
}

// =============================================================================
// Wave 4.4: Advanced Surface Shading (Hair/POM) & Full SceneRenderer FrameGraph
// =============================================================================

std::array<float, 3> eval_kajiya_kay_hair(
    std::array<float, 3> tangent,
    std::array<float, 3> light_dir,
    std::array<float, 3> view_dir,
    std::array<float, 3> base_color,
    float primary_exp,
    float secondary_exp,
    float shift
) noexcept
{
    auto dot3 = [](std::array<float, 3> a, std::array<float, 3> b) noexcept {
        return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    };

    std::array<float, 3> h {
        light_dir[0] + view_dir[0],
        light_dir[1] + view_dir[1],
        light_dir[2] + view_dir[2],
    };
    const float h_len = std::sqrt(std::max(dot3(h, h), 1e-8F));
    h[0] /= h_len;
    h[1] /= h_len;
    h[2] /= h_len;

    const float t_dot_h1 = std::clamp(dot3(tangent, h) - shift, -1.0F, 1.0F);
    const float t_dot_h2 = std::clamp(dot3(tangent, h) + shift, -1.0F, 1.0F);

    const float sin_th1 = std::sqrt(std::max(1.0F - t_dot_h1 * t_dot_h1, 0.0F));
    const float sin_th2 = std::sqrt(std::max(1.0F - t_dot_h2 * t_dot_h2, 0.0F));

    const float spec1 = std::pow(sin_th1, std::max(primary_exp, 1.0F));
    const float spec2 = std::pow(sin_th2, std::max(secondary_exp, 1.0F));

    return {
        base_color[0] * (0.25F + spec2) + spec1 * 0.5F,
        base_color[1] * (0.25F + spec2) + spec1 * 0.5F,
        base_color[2] * (0.25F + spec2) + spec1 * 0.5F,
    };
}

std::array<float, 2> eval_pom_uv_offset(
    std::array<float, 2> base_uv,
    std::array<float, 3> view_dir_ts,
    float height_scale,
    std::span<const float> height_samples
) noexcept
{
    if (height_samples.empty() || height_scale <= 0.0F)
        return base_uv;

    const float vz = std::max(std::abs(view_dir_ts[2]), 0.1F);
    const float max_du = (view_dir_ts[0] / vz) * height_scale;
    const float max_dv = (view_dir_ts[1] / vz) * height_scale;

    const std::size_t steps = height_samples.size();
    const float step_layer = 1.0F / static_cast<float>(steps);
    float current_layer = 0.0F;

    for (std::size_t i = 0; i < steps; ++i)
    {
        const float map_depth = 1.0F - std::clamp(height_samples[i], 0.0F, 1.0F);
        if (current_layer >= map_depth)
        {
            return {
                base_uv[0] - max_du * current_layer,
                base_uv[1] - max_dv * current_layer,
            };
        }
        current_layer += step_layer;
    }

    return {
        base_uv[0] - max_du,
        base_uv[1] - max_dv,
    };
}

SceneFrameGraphHandles build_scene_renderer_framegraph(
    cd::framegraph::FrameGraph& fg,
    cd::rhi::TextureHandle backbuffer_texture,
    const SceneRendererConfig& config,
    const SceneViewData* scene_data
)
{
    SceneFrameGraphHandles h {};
    const std::uint32_t w = std::max(config.viewport.width, 1U);
    const std::uint32_t ht = std::max(config.viewport.height, 1U);

    cd::framegraph::TransientTextureDesc depth_desc {};
    depth_desc.format = cd::rhi::Format::kD32Float;
    depth_desc.extent = { w, ht, 1U };
    depth_desc.usage = cd::rhi::TextureUsage::kDepthStencilAttachment | cd::rhi::TextureUsage::kSampled;
    h.depth = fg.create_texture(depth_desc, "scene_depth");

    cd::framegraph::TransientTextureDesc rgba16_desc {};
    rgba16_desc.format = cd::rhi::Format::kRGBA16Float;
    rgba16_desc.extent = { w, ht, 1U };
    rgba16_desc.usage = cd::rhi::TextureUsage::kColorAttachment |
                        cd::rhi::TextureUsage::kStorage |
                        cd::rhi::TextureUsage::kSampled;

    h.gbuffer_albedo = fg.create_texture(rgba16_desc, "gbuffer_albedo");
    h.gbuffer_normal_roughness = fg.create_texture(rgba16_desc, "gbuffer_normal_roughness");
    h.hdr_scene = fg.create_texture(rgba16_desc, "hdr_scene");

    cd::framegraph::ImportedTextureDesc back_desc {};
    back_desc.texture = backbuffer_texture;
    back_desc.initial_state = cd::rhi::ResourceState::kColorAttachment;
    back_desc.final_state = cd::rhi::ResourceState::kPresent;
    h.final_backbuffer = fg.import_texture(back_desc, "swapchain_backbuffer");

    // 1) GPU-Driven Culling + G-Buffer + Direct PBR / SSS / Hair / Clearcoat / POM
    {
        const std::array<cd::framegraph::PassResource, 4> writes {{
            { h.depth, cd::rhi::ResourceState::kDepthWrite },
            { h.gbuffer_albedo, cd::rhi::ResourceState::kColorAttachment },
            { h.gbuffer_normal_roughness, cd::rhi::ResourceState::kColorAttachment },
            { h.hdr_scene, cd::rhi::ResourceState::kColorAttachment },
        }};
        cd::framegraph::PassDesc pass {};
        pass.name = "gpu_cull_and_gbuffer";
        pass.writes = writes;
        pass.execute = [viewport = config.viewport, scene_data](cd::rhi::ICommandBuffer& cmd) {
            cmd.push_debug_group("gpu_cull_and_gbuffer");
            cmd.set_viewport(cd::rhi::Viewport {
                0.0F, 0.0F,
                static_cast<float>(viewport.width),
                static_cast<float>(viewport.height),
                0.0F, 1.0F
            });
            cmd.set_scissor(cd::rhi::Rect2D { { 0, 0 }, viewport });

            if (scene_data != nullptr && !scene_data->meshes.empty())
            {
                const cd::math::Mat4f vp = scene_data->proj_matrix * scene_data->view_matrix;
                for (const auto& mesh : scene_data->meshes)
                {
                    record_scene_mesh_instance(cmd, cd::rhi::PipelineLayoutHandle {}, mesh, vp);
                }
            }

            cmd.pop_debug_group();
        };
        fg.add_pass(pass);
    }

    // 2) Deferred Decals, Clipmap Terrain, FFT Ocean Water & GPU Particles
    if (config.surface.enable_deferred_decals ||
        config.surface.enable_clipmap_terrain ||
        config.surface.enable_fft_ocean_water ||
        config.surface.enable_gpu_particles)
    {
        const std::array<cd::framegraph::PassResource, 2> reads {{
            { h.depth, cd::rhi::ResourceState::kShaderResource },
            { h.gbuffer_albedo, cd::rhi::ResourceState::kShaderResource },
        }};
        const std::array<cd::framegraph::PassResource, 1> writes {{
            { h.hdr_scene, cd::rhi::ResourceState::kColorAttachment },
        }};
        cd::framegraph::PassDesc pass {};
        pass.name = "decals_terrain_water_particles";
        pass.reads = reads;
        pass.writes = writes;
        pass.execute = [viewport = config.viewport](cd::rhi::ICommandBuffer& cmd) {
            cmd.push_debug_group("decals_terrain_water_particles");
            cmd.set_viewport(cd::rhi::Viewport {
                0.0F, 0.0F,
                static_cast<float>(viewport.width),
                static_cast<float>(viewport.height),
                0.0F, 1.0F
            });
            cmd.set_scissor(cd::rhi::Rect2D { { 0, 0 }, viewport });
            cmd.pop_debug_group();
        };
        fg.add_pass(pass);
    }

    // 3) Atmosphere LUTs, Volumetric Froxel Fog, Clouds & Light Shafts
    h.atmosphere_handles = register_atmosphere_and_volumetric_passes(
        fg, h.depth, h.hdr_scene, config.viewport, config.atmosphere, scene_data);

    // 4) Global Illumination (GTAO, SSR, DDGI, ReSTIR DI/GI, NRC, SVGF, IBL GPU)
    h.gi_handles = register_gi_passes(
        fg, h.depth, h.gbuffer_normal_roughness, h.hdr_scene, config.viewport, config.gi, scene_data);

    // 5) Velocity + TAA / SMAA + Auto Exposure + Bloom + DoF + Motion Blur + Composite
    if (config.postfx.enable_velocity)
    {
        h.velocity = fg.create_texture(rgba16_desc, "velocity_vectors");
        const std::array<cd::framegraph::PassResource, 1> reads {{
            { h.depth, cd::rhi::ResourceState::kShaderResource },
        }};
        const std::array<cd::framegraph::PassResource, 1> writes {{
            { h.velocity, cd::rhi::ResourceState::kColorAttachment },
        }};
        cd::framegraph::PassDesc pass {};
        pass.name = "velocity_motion_vectors";
        pass.reads = reads;
        pass.writes = writes;
        pass.execute = [viewport = config.viewport](cd::rhi::ICommandBuffer& cmd) {
            cmd.push_debug_group("velocity_motion_vectors");
            cmd.set_viewport(cd::rhi::Viewport {
                0.0F, 0.0F,
                static_cast<float>(viewport.width),
                static_cast<float>(viewport.height),
                0.0F, 1.0F
            });
            cmd.set_scissor(cd::rhi::Rect2D { { 0, 0 }, viewport });
            cmd.pop_debug_group();
        };
        fg.add_pass(pass);
    }

    cd::framegraph::ResourceHandle current_color = h.hdr_scene;
    if (config.postfx.enable_taa || config.postfx.enable_smaa)
    {
        h.post_aa = fg.create_texture(rgba16_desc, "post_aa_color");
        std::vector<cd::framegraph::PassResource> reads {
            { current_color, cd::rhi::ResourceState::kShaderResource },
            { h.depth, cd::rhi::ResourceState::kShaderResource },
        };
        if (h.velocity.is_valid())
            reads.push_back({ h.velocity, cd::rhi::ResourceState::kShaderResource });

        const std::array<cd::framegraph::PassResource, 1> writes {{
            { h.post_aa, cd::rhi::ResourceState::kColorAttachment },
        }};
        cd::framegraph::PassDesc pass {};
        pass.name = config.postfx.enable_taa ? "post_taa" : "post_smaa";
        pass.reads = reads;
        pass.writes = writes;
        pass.execute = [viewport = config.viewport](cd::rhi::ICommandBuffer& cmd) {
            cmd.push_debug_group("post_aa");
            cmd.set_viewport(cd::rhi::Viewport {
                0.0F, 0.0F,
                static_cast<float>(viewport.width),
                static_cast<float>(viewport.height),
                0.0F, 1.0F
            });
            cmd.set_scissor(cd::rhi::Rect2D { { 0, 0 }, viewport });
            cmd.pop_debug_group();
        };
        fg.add_pass(pass);
        current_color = h.post_aa;
    }

    if (config.postfx.enable_bloom)
    {
        h.bloom_chain = fg.create_texture(rgba16_desc, "post_bloom_chain");
        const std::array<cd::framegraph::PassResource, 1> reads {{
            { current_color, cd::rhi::ResourceState::kShaderResource },
        }};
        const std::array<cd::framegraph::PassResource, 1> writes {{
            { h.bloom_chain, cd::rhi::ResourceState::kColorAttachment },
        }};
        cd::framegraph::PassDesc pass {};
        pass.name = "post_bloom";
        pass.reads = reads;
        pass.writes = writes;
        pass.execute = [viewport = config.viewport](cd::rhi::ICommandBuffer& cmd) {
            cmd.push_debug_group("post_bloom");
            const cd::rhi::Extent2D bext {
                std::max(viewport.width / 2U, 1U),
                std::max(viewport.height / 2U, 1U)
            };
            cmd.set_viewport(cd::rhi::Viewport {
                0.0F, 0.0F,
                static_cast<float>(bext.width),
                static_cast<float>(bext.height),
                0.0F, 1.0F
            });
            cmd.set_scissor(cd::rhi::Rect2D { { 0, 0 }, bext });
            cmd.pop_debug_group();
        };
        fg.add_pass(pass);
    }

    if (config.postfx.enable_dof)
    {
        h.dof_coc = fg.create_texture(rgba16_desc, "post_dof_coc");
        const std::array<cd::framegraph::PassResource, 2> reads {{
            { current_color, cd::rhi::ResourceState::kShaderResource },
            { h.depth, cd::rhi::ResourceState::kShaderResource },
        }};
        const std::array<cd::framegraph::PassResource, 1> writes {{
            { h.dof_coc, cd::rhi::ResourceState::kColorAttachment },
        }};
        cd::framegraph::PassDesc pass {};
        pass.name = "post_dof";
        pass.reads = reads;
        pass.writes = writes;
        pass.execute = [viewport = config.viewport](cd::rhi::ICommandBuffer& cmd) {
            cmd.push_debug_group("post_dof");
            cmd.set_viewport(cd::rhi::Viewport {
                0.0F, 0.0F,
                static_cast<float>(viewport.width),
                static_cast<float>(viewport.height),
                0.0F, 1.0F
            });
            cmd.set_scissor(cd::rhi::Rect2D { { 0, 0 }, viewport });
            cmd.pop_debug_group();
        };
        fg.add_pass(pass);
        current_color = h.dof_coc;
    }

    // Final Composite + Auto Exposure + Motion Blur + Tonemap (ACEScg / AgX + 3D LUT)
    {
        std::vector<cd::framegraph::PassResource> reads {
            { current_color, cd::rhi::ResourceState::kShaderResource },
        };
        if (h.bloom_chain.is_valid())
            reads.push_back({ h.bloom_chain, cd::rhi::ResourceState::kShaderResource });
        if (config.postfx.enable_motion_blur && h.velocity.is_valid())
            reads.push_back({ h.velocity, cd::rhi::ResourceState::kShaderResource });

        const std::array<cd::framegraph::PassResource, 1> writes {{
            { h.final_backbuffer, cd::rhi::ResourceState::kColorAttachment },
        }};
        cd::framegraph::PassDesc pass {};
        pass.name = "post_composite_tonemap";
        pass.reads = reads;
        pass.writes = writes;
        pass.execute = [viewport = config.viewport](cd::rhi::ICommandBuffer& cmd) {
            cmd.push_debug_group("post_composite_tonemap");
            cmd.set_viewport(cd::rhi::Viewport {
                0.0F, 0.0F,
                static_cast<float>(viewport.width),
                static_cast<float>(viewport.height),
                0.0F, 1.0F
            });
            cmd.set_scissor(cd::rhi::Rect2D { { 0, 0 }, viewport });
            cmd.pop_debug_group();
        };
        fg.add_pass(pass);
    }

    return h;
}

}  // namespace cd::scene_renderer
