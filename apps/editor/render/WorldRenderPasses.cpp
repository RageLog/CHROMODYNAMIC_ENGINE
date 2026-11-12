// =============================================================================
// CHROMODYNAMIC STUDIO -- apps/editor/render/WorldRenderPasses.cpp
// =============================================================================
#include "render/WorldRenderPasses.hpp"

#include "SponzaFixtures.hpp"
#include "StudioCliOptions.hpp"
#include "StudioPbrSphereGrid.hpp"

#include <cd/app_common/CameraMath3D.hpp>
#include <cd/camera/Camera.hpp>
#include <cd/concurrency/ParallelFor.hpp>
#include <cd/material/AnalyticalSkyMaterial.hpp>
#include <cd/math/Matrix.hpp>
#include <cd/math/Transform.hpp>
#include <cd/post/bloom/Bloom.hpp>
#include <cd/post/composite/Composite.hpp>
#include <cd/post/taa/Taa.hpp>
#include <cd/render/PlanarShadow.hpp>
#include <cd/rhi/Barriers.hpp>
#include <cd/scene_renderer/SceneRenderer.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <span>
#include <string>
#include <vector>

namespace cd::studio::render
{

namespace
{

using cd::studio_rt::LightUboGpu;
using cd::studio_rt::pack_light_slot;
using cd::studio_rt::PrimPush;

struct HdrPassAttach
{
    std::array<cd::rhi::ColorAttachmentInfo, 4> color {};
    cd::rhi::DepthStencilAttachmentInfo         depth {};
};

void hdr_pass_barriers(cd::rhi::ICommandBuffer& cmd,
                       std::uint32_t frame_idx,
                       const cd::framegraph::ColorTarget& hdr_target,
                       const cd::framegraph::ColorTarget& gbuf_normal,
                       const cd::framegraph::ColorTarget& gbuf_albedo,
                       const cd::framegraph::ColorTarget& gbuf_mr)
{
    const cd::rhi::ResourceState prev_state =
        (frame_idx == 0U) ? cd::rhi::ResourceState::kUndefined : cd::rhi::ResourceState::kShaderResource;
    const std::array<cd::rhi::TextureBarrier, 4> hb {
        cd::rhi::TextureBarrier { .texture = hdr_target.image,
                                  .from    = prev_state,
                                  .to      = cd::rhi::ResourceState::kColorAttachment,
                                  .range   = { 0, 1, 0, 1 } },
        cd::rhi::TextureBarrier { .texture = gbuf_normal.image,
                                  .from    = prev_state,
                                  .to      = cd::rhi::ResourceState::kColorAttachment,
                                  .range   = { 0, 1, 0, 1 } },
        cd::rhi::TextureBarrier { .texture = gbuf_albedo.image,
                                  .from    = prev_state,
                                  .to      = cd::rhi::ResourceState::kColorAttachment,
                                  .range   = { 0, 1, 0, 1 } },
        cd::rhi::TextureBarrier { .texture = gbuf_mr.image,
                                  .from    = prev_state,
                                  .to      = cd::rhi::ResourceState::kColorAttachment,
                                  .range   = { 0, 1, 0, 1 } },
    };
    cmd.barrier({}, hb);
}

[[nodiscard]] HdrPassAttach hdr_pass_attach(const cd::framegraph::ColorTarget& hdr_target,
                                            const cd::framegraph::ColorTarget& gbuf_normal,
                                            const cd::framegraph::ColorTarget& gbuf_albedo,
                                            const cd::framegraph::ColorTarget& gbuf_mr,
                                            const cd::framegraph::DepthTarget& depth) noexcept
{
    HdrPassAttach a {};
    a.color = {
        cd::rhi::ColorAttachmentInfo { .view        = hdr_target.view,
                                       .load_op     = cd::rhi::LoadOp::kClear,
                                       .store_op    = cd::rhi::StoreOp::kStore,
                                       .clear_color = { .f32 = { 1.0F, 0.0F, 1.0F, 1.0F } } },
        cd::rhi::ColorAttachmentInfo { .view        = gbuf_normal.view,
                                       .load_op     = cd::rhi::LoadOp::kClear,
                                       .store_op    = cd::rhi::StoreOp::kStore,
                                       .clear_color = { .f32 = { 0.0F, 0.0F, 0.0F, 0.0F } } },
        cd::rhi::ColorAttachmentInfo { .view        = gbuf_albedo.view,
                                       .load_op     = cd::rhi::LoadOp::kClear,
                                       .store_op    = cd::rhi::StoreOp::kStore,
                                       .clear_color = { .f32 = { 0.0F, 0.0F, 0.0F, 0.0F } } },
        cd::rhi::ColorAttachmentInfo { .view        = gbuf_mr.view,
                                       .load_op     = cd::rhi::LoadOp::kClear,
                                       .store_op    = cd::rhi::StoreOp::kStore,
                                       .clear_color = { .f32 = { 0.0F, 1.0F, 0.0F, 0.0F } } },
    };
    a.depth.view        = depth.view;
    a.depth.depth_load  = cd::rhi::LoadOp::kClear;
    a.depth.depth_store = cd::rhi::StoreOp::kStore;
    a.depth.clear.depth = 1.0F;
    return a;
}

}  // namespace

SunLight resolve_sun_light(const std::vector<LightRow>& lights) noexcept
{
    SunLight s {};
    s.ambient_w = cd::studio_rt::golden::enabled() ? 0.0F : 0.22F;
    for (const auto& lrow : lights)
    {
        if (!lrow.enabled || lrow.light.intensity <= 0.001F)
            continue;
        if (lrow.light.type != cd::light::LightType::kDirectional)
            continue;
        s.dir = lrow.light.direction;
        s.col = lrow.light.color;
        const float raw_i = lrow.light.intensity;
        if (cd::studio_rt::golden::enabled())
        {
            s.strength  = std::min(2.5F, raw_i / 80000.0F);
            s.ambient_w = 0.18F;
        }
        else
        {
            s.strength = (raw_i <= 250.0F)
                ? std::clamp(raw_i * 0.22F, 0.05F, 2.5F)
                : std::clamp(raw_i / 24000.0F, 0.05F, 2.5F);
            s.ambient_w = 0.26F;
        }
        s.has_sun = true;
        break;
    }
    return s;
}

void fill_prim_push_shared(PrimPush& pp,
                           const cd_studio::StudioRenderFx& fx,
                           const SunLight& sun,
                           const cd::camera::Camera& cam) noexcept
{
    pp.sun_dir[0]    = sun.dir.x;
    pp.sun_dir[1]    = sun.dir.y;
    pp.sun_dir[2]    = sun.dir.z;
    pp.sun_dir[3]    = sun.strength;
    pp.sun_color[0]  = sun.col.x;
    pp.sun_color[1]  = sun.col.y;
    pp.sun_color[2]  = sun.col.z;
    pp.sun_color[3]  = sun.ambient_w;
    pp.fx_params[0]  = static_cast<float>(fx.tonemap_op);
    pp.fx_params[2]  = fx.gtao_strength;
    pp.fx_params[3]  = fx.bloom_strength;
    pp.fx_params2[0] = fx.smaa_strength;
    pp.fx_params2[1] = fx.motion_blur;
    pp.fx_params2[2] = fx.taa_amount;
    pp.fx_params2[3] = fx.dof_strength;
    pp.fx_params3[0] = fx.fog_density;
    pp.fx_params3[1] = fx.aerial_perspective;
    pp.fx_params3[2] = fx.clouds_coverage;
    pp.fx_params3[3] = fx.light_shafts;
    pp.camera_pos[0] = cam.eye.x;
    pp.camera_pos[1] = cam.eye.y;
    pp.camera_pos[2] = cam.eye.z;
    pp.camera_pos[3] = 0.0F;
}

void upload_multi_light_ubo(cd::rhi::IDevice& device,
                            cd::rhi::BufferHandle lights_ubo,
                            const std::vector<LightRow>& lights,
                            cd::core::CounterTable& counters)
{
    LightUboGpu ubo {};
    ubo.count = 0;
    for (const auto& lrow : lights)
    {
        if (!lrow.enabled)
            continue;
        if (ubo.count >= cd::studio_rt::kMaxLights)
            break;
        if (!pack_light_slot(ubo.slots[ubo.count], lrow.light))
            continue;
        ++ubo.count;
    }
    static LightUboGpu           s_prev_ubo {};
    static cd::rhi::BufferHandle s_prev_buf {};
    if (lights_ubo != s_prev_buf || std::memcmp(&ubo, &s_prev_ubo, sizeof(ubo)) != 0)
    {
        (void)device.upload_buffer(
            lights_ubo,
            0,
            std::span<const std::byte>(reinterpret_cast<const std::byte*>(&ubo), sizeof(ubo)));
        s_prev_ubo = ubo;
        s_prev_buf = lights_ubo;
    }
    counters.set("lights_active", ubo.count);
}

void draw_sky_pass(cd::rhi::IDrawRecorder& cmd,
                   const cd::camera::Camera& cam,
                   float aspect,
                   const SunLight& sun,
                   cd::material::Material& sky_material)
{
    sky_material.apply(cmd);
    cd::scene_renderer::SkyPassParams sky_params {};
    sky_params.camera_eye = cam.eye;
    sky_params.camera_target = cam.target;
    sky_params.fov_y = cam.fov_y;
    sky_params.aspect = aspect;
    sky_params.sun_dir = sun.dir;
    sky_params.sun_color = sun.col;
    sky_params.sun_intensity = sun.strength;
    sky_params.has_sun = cd::studio_rt::golden::enabled() || sun.has_sun;
    cd::scene_renderer::record_analytical_sky(cmd, sky_material.pipeline_layout(), sky_params);
}

void draw_planar_shadows(StudioEngineState& s,
                         cd::rhi::IDrawRecorder& cmd,
                         const SunLight& sun,
                         float floor_y,
                         float shadow_lift,
                         const cd::math::Mat4f& vp)
{
    if (!(sun.strength > 1e-4F) || !(sun.dir.y < -1e-3F))
        return;

    const auto S = cd::render::make_planar_shadow_matrix(sun.dir, floor_y, shadow_lift);
    PrimPush sp {};
    sp.tint[0] = 0.04F;
    sp.tint[1] = 0.04F;
    sp.tint[2] = 0.05F;
    sp.tint[3] = 0.0F;

    std::vector<PrimPush>     plan_push(s.entities.size());
    std::vector<std::uint8_t> plan_valid(s.entities.size(), 0U);
    auto prep_planar_one = [&](std::size_t i)
    {
        const auto& ent = s.entities[i];
        if (!ent.visible || ent.kind == PrimitiveKind::kSponza)
            return;
        const auto& mesh = s.mesh_for(ent.kind);
        if (!mesh.vb.is_valid())
            return;
        const auto* lt = s.scene.local(ent.handle);
        if (lt == nullptr)
            return;
        if ((lt->value.scale.x >= 10.0F && lt->value.scale.z >= 10.0F) ||
            ent.name.find("Floor") != std::string::npos)
            return;
        const auto model        = cd::math::to_mat4(lt->value);
        const auto shadow_model = S * model;
        PrimPush&  dst          = plan_push[i];
        dst                     = sp;
        dst.mvp                 = vp * shadow_model;
        dst.model               = shadow_model;
        plan_valid[i]           = 1U;
    };

    if (s.entities.size() >= 256U)
        cd::concurrency::parallel_for(std::size_t { 0 }, s.entities.size(), prep_planar_one);
    else
    {
        for (std::size_t i = 0; i < s.entities.size(); ++i)
            prep_planar_one(i);
    }

    for (std::size_t i = 0; i < s.entities.size(); ++i)
    {
        if (plan_valid[i] == 0U)
            continue;
        const auto& ent  = s.entities[i];
        const auto& mesh = s.mesh_for(ent.kind);
        cmd.bind_vertex_buffer(0, mesh.vb, 0);
        cmd.bind_index_buffer(mesh.ib, 0, mesh.index_type);
        cmd.push_constants(
            s.materials.prim.pipeline_layout(),
            cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
            0,
            sizeof(PrimPush),
            &plan_push[i]);
        cmd.draw_indexed(mesh.index_count, 1, 0, 0, 0);
        s.counters.increment("draws_shadow");
    }
}

void draw_shadow_map_pass(StudioEngineState& s,
                          cd::rhi::ICommandBuffer& cmd,
                          const SunLight& sun,
                          cd::rhi::Extent2D shadow_map_size,
                          bool shadow_dirty)
{
    if (!shadow_dirty && s.shadow_initialised_on_gpu)
        return;

    auto& device = *s.device_owner;
    cd::math::Vec3f sd = sun.has_sun ? sun.dir : cd::math::Vec3f { -0.4F, -0.9F, -0.2F };
    {
        const float sd_len = std::sqrt(sd.x * sd.x + sd.y * sd.y + sd.z * sd.z);
        if (sd_len > 1e-4F) { sd.x /= sd_len; sd.y /= sd_len; sd.z /= sd_len; }
        else { sd = { 0.0F, -1.0F, 0.0F }; }
    }
    const cd::math::Vec3f eye { -sd.x * 25.0F, -sd.y * 25.0F, -sd.z * 25.0F };
    const cd::math::Vec3f tgt { 0.0F, 0.0F, 0.0F };
    const cd::math::Vec3f up =
        (std::fabs(sd.y) > 0.99F) ? cd::math::Vec3f { 0.0F, 0.0F, 1.0F }
                                  : cd::math::Vec3f { 0.0F, 1.0F, 0.0F };
    const auto            light_view = cd::math::look_at(eye, tgt, up);
    const auto            light_proj = cd::math::ortho(-12.5F, 12.5F, -12.5F, 12.5F, 0.1F, 60.0F);
    const cd::math::Mat4f light_vp   = light_proj * light_view;
    (void)device.upload_buffer(
        s.shadow_ubo,
        0,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(&light_vp), sizeof(light_vp)));

    if (!s.shadow_initialised_on_gpu)
    {
        const std::array<cd::rhi::TextureBarrier, 1> sb {
            cd::rhi::TextureBarrier {
                .texture = s.shadow_target.image,
                .from    = cd::rhi::ResourceState::kUndefined,
                .to      = cd::rhi::ResourceState::kDepthWrite,
                .range   = { 0, 1, 0, 1 },
            },
        };
        cmd.barrier({}, sb);
        s.shadow_initialised_on_gpu = true;
    }
    else
    {
        const std::array<cd::rhi::TextureBarrier, 1> sb {
            cd::rhi::TextureBarrier {
                .texture = s.shadow_target.image,
                .from    = cd::rhi::ResourceState::kShaderResource,
                .to      = cd::rhi::ResourceState::kDepthWrite,
                .range   = { 0, 1, 0, 1 },
            },
        };
        cmd.barrier({}, sb);
    }

    {
        cd::rhi::DepthStencilAttachmentInfo sda {};
        sda.view        = s.shadow_target.view;
        sda.depth_load  = cd::rhi::LoadOp::kClear;
        sda.depth_store = cd::rhi::StoreOp::kStore;
        sda.clear.depth = 1.0F;
        cd::rhi::RenderPassBeginInfo srp {};
        srp.render_area       = cd::rhi::Rect2D { { 0, 0 }, shadow_map_size };
        srp.color_attachments = {};
        srp.depth_stencil     = &sda;
        cmd.begin_render_pass(srp);
        cmd.set_viewport(cd::rhi::Viewport {
            0.0F, 0.0F,
            static_cast<float>(shadow_map_size.width),
            static_cast<float>(shadow_map_size.height),
            0.0F, 1.0F });
        cmd.set_scissor(cd::rhi::Rect2D { { 0, 0 }, shadow_map_size });

        if (sun.has_sun)
        {
            s.materials.shadow.apply(cmd);
            std::vector<cd::math::Mat4f> csm_light_mvp(s.entities.size());
            std::vector<std::uint8_t>    csm_valid(s.entities.size(), 0U);
            auto prep_csm_one = [&](std::size_t i)
            {
                const auto& ent = s.entities[i];
                if (!ent.visible) return;
                if (ent.name.find("Ground_Floor") != std::string::npos ||
                    ent.name.find("Volumetric_Clouds") != std::string::npos ||
                    ent.name.find("Volumetric_Fog") != std::string::npos ||
                    ent.name.find("Camera") != std::string::npos)
                    return;
                const auto& mesh = s.mesh_for(ent.kind);
                if (!mesh.vb.is_valid()) return;
                const auto* lt = s.scene.local(ent.handle);
                if (lt == nullptr) return;
                csm_light_mvp[i] = light_vp * cd::math::to_mat4(lt->value);
                csm_valid[i]     = 1U;
            };
            if (s.entities.size() >= 256U)
                cd::concurrency::parallel_for(std::size_t { 0 }, s.entities.size(), prep_csm_one);
            else
            {
                for (std::size_t i = 0; i < s.entities.size(); ++i)
                    prep_csm_one(i);
            }
            for (std::size_t i = 0; i < s.entities.size(); ++i)
            {
                if (csm_valid[i] == 0U) continue;
                const auto& ent  = s.entities[i];
                const auto& mesh = s.mesh_for(ent.kind);
                cmd.bind_vertex_buffer(0, mesh.vb, 0);
                cmd.bind_index_buffer(mesh.ib, 0, mesh.index_type);
                cmd.push_constants(
                    s.materials.shadow.pipeline_layout(),
                    cd::rhi::ShaderStage::kVertex,
                    0,
                    sizeof(cd::math::Mat4f),
                    &csm_light_mvp[i]);
                cmd.draw_indexed(mesh.index_count, 1, 0, 0, 0);
            }
        }
        cmd.end_render_pass();
    }

    {
        const std::array<cd::rhi::TextureBarrier, 1> sb {
            cd::rhi::TextureBarrier {
                .texture = s.shadow_target.image,
                .from    = cd::rhi::ResourceState::kDepthWrite,
                .to      = cd::rhi::ResourceState::kShaderResource,
                .range   = { 0, 1, 0, 1 },
            },
        };
        cmd.barrier({}, sb);
    }
}

void velocity_gbuffer_pass(StudioEngineState& s,
                           cd::rhi::ICommandBuffer& cmd,
                           cd::rhi::Extent2D extent,
                           const cd::math::Mat4f& vp_unjittered,
                           bool force_first_frame)
{
    const cd::rhi::ResourceState vel_prev =
        (s.frame_idx == 0U || force_first_frame)
            ? cd::rhi::ResourceState::kUndefined
            : cd::rhi::ResourceState::kShaderResource;
    const std::array<cd::rhi::TextureBarrier, 2> vb {
        cd::rhi::TextureBarrier { .texture = s.rts.gbuf_velocity.image,
                                  .from    = vel_prev,
                                  .to      = cd::rhi::ResourceState::kColorAttachment,
                                  .range   = { 0, 1, 0, 1 } },
        cd::rhi::TextureBarrier { .texture = s.rts.depth.image,
                                  .from    = cd::rhi::ResourceState::kDepthWrite,
                                  .to      = cd::rhi::ResourceState::kDepthRead,
                                  .range   = { 0, 1, 0, 1 } },
    };
    cmd.barrier({}, vb);

    std::array<cd::rhi::ColorAttachmentInfo, 1> vel_ca {
        cd::rhi::ColorAttachmentInfo { .view        = s.rts.gbuf_velocity.view,
                                       .load_op     = cd::rhi::LoadOp::kClear,
                                       .store_op    = cd::rhi::StoreOp::kStore,
                                       .clear_color = { .f32 = { 0.0F, 0.0F, 0.0F, 0.0F } } },
    };
    cd::rhi::DepthStencilAttachmentInfo vel_depth {};
    vel_depth.view        = s.rts.depth.view;
    vel_depth.depth_load  = cd::rhi::LoadOp::kLoad;
    vel_depth.depth_store = cd::rhi::StoreOp::kStore;

    cd::rhi::RenderPassBeginInfo vel_rp {};
    vel_rp.render_area       = cd::rhi::Rect2D { { 0, 0 }, extent };
    vel_rp.color_attachments = vel_ca;
    vel_rp.depth_stencil     = &vel_depth;
    cmd.begin_render_pass(vel_rp);
    cmd.set_viewport(cd::rhi::Viewport {
        0.0F, 0.0F, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0F, 1.0F });
    cmd.set_scissor(cd::rhi::Rect2D { { 0, 0 }, extent });
    s.materials.velocity.apply(cmd);

    for (auto& ent : s.entities)
    {
        if (!ent.visible)
            continue;
        const auto& mesh = s.mesh_for(ent.kind);
        if (!mesh.vb.is_valid())
            continue;
        const auto* lt = s.scene.local(ent.handle);
        if (lt == nullptr)
            continue;
        cmd.bind_vertex_buffer(0, mesh.vb, 0);
        cmd.bind_index_buffer(mesh.ib, 0, mesh.index_type);

        const auto  curr_model_mat = cd::math::to_mat4(lt->value);
        const auto& prev_model_mat = ent.prev_model_valid ? ent.prev_model : curr_model_mat;

        struct VelocityPush
        {
            cd::math::Mat4f prev_vp_model;
            cd::math::Mat4f curr_vp_model;
        } vpush {
            .prev_vp_model = s.prev_vp_unjittered * prev_model_mat,
            .curr_vp_model = vp_unjittered * curr_model_mat,
        };

        cmd.push_constants(
            s.materials.velocity.pipeline_layout(),
            cd::rhi::ShaderStage::kVertex,
            0,
            sizeof(vpush),
            &vpush);
        cmd.draw_indexed(mesh.index_count, 1, 0, 0, 0);

        ent.prev_model       = curr_model_mat;
        ent.prev_model_valid = true;
    }
    cmd.end_render_pass();

    const std::array<cd::rhi::TextureBarrier, 2> vb2 {
        cd::rhi::TextureBarrier { .texture = s.rts.gbuf_velocity.image,
                                  .from    = cd::rhi::ResourceState::kColorAttachment,
                                  .to      = cd::rhi::ResourceState::kShaderResource,
                                  .range   = { 0, 1, 0, 1 } },
        cd::rhi::TextureBarrier { .texture = s.rts.depth.image,
                                  .from    = cd::rhi::ResourceState::kDepthRead,
                                  .to      = cd::rhi::ResourceState::kShaderResource,
                                  .range   = { 0, 1, 0, 1 } },
    };
    cmd.barrier({}, vb2);
}

void run_bloom_chain(StudioEngineState& s,
                     cd::rhi::ICommandBuffer& cmd,
                     float ev_stops,
                     bool force_first_frame)
{
    auto run_bloom_pass = [&](cd::material::Material& mat,
                              cd::material::MaterialInstance& inst,
                              cd::framegraph::ColorTarget& dst,
                              cd::rhi::LoadOp load_op,
                              std::span<const std::byte> push_bytes,
                              bool first_frame)
    {
        const std::array<cd::rhi::TextureBarrier, 1> tb {
            cd::rhi::TextureBarrier {
                .texture = dst.image,
                .from    = first_frame ? cd::rhi::ResourceState::kUndefined
                                       : cd::rhi::ResourceState::kShaderResource,
                .to      = cd::rhi::ResourceState::kColorAttachment,
                .range   = { 0, 1, 0, 1 },
            },
        };
        cmd.barrier({}, tb);

        std::array<cd::rhi::ColorAttachmentInfo, 1> ca {
            cd::rhi::ColorAttachmentInfo { .view        = dst.view,
                                           .load_op     = load_op,
                                           .store_op    = cd::rhi::StoreOp::kStore,
                                           .clear_color = { .f32 = { 0.0F, 0.0F, 0.0F, 1.0F } } },
        };
        cd::rhi::RenderPassBeginInfo rp {};
        rp.render_area       = cd::rhi::Rect2D { { 0, 0 }, dst.extent };
        rp.color_attachments = ca;
        rp.depth_stencil     = nullptr;
        cmd.begin_render_pass(rp);
        cmd.set_viewport(cd::rhi::Viewport {
            0.0F, 0.0F,
            static_cast<float>(dst.extent.width),
            static_cast<float>(dst.extent.height),
            0.0F, 1.0F });
        cmd.set_scissor(cd::rhi::Rect2D { { 0, 0 }, dst.extent });
        mat.apply(cmd);
        inst.bind(cmd, 0);
        if (!push_bytes.empty())
        {
            cmd.push_constants(
                mat.pipeline_layout(),
                cd::rhi::ShaderStage::kFragment,
                0,
                static_cast<std::uint32_t>(push_bytes.size()),
                push_bytes.data());
        }
        cmd.draw(3, 1, 0, 0);
        cmd.end_render_pass();

        const std::array<cd::rhi::TextureBarrier, 1> tb2 {
            cd::rhi::TextureBarrier {
                .texture = dst.image,
                .from    = cd::rhi::ResourceState::kColorAttachment,
                .to      = cd::rhi::ResourceState::kShaderResource,
                .range   = { 0, 1, 0, 1 },
            },
        };
        cmd.barrier({}, tb2);
    };

    const bool bloom_first_frame = (s.frame_idx == 0U || force_first_frame);
    {
        cd::post::bloom::PrefilterPush bpp {};
        bpp.params[0] = 1.10F;
        bpp.params[1] = 0.50F;
        bpp.params[2] = ev_stops;
        bpp.params[3] = 0.0F;
        const std::span<const std::byte> bytes { reinterpret_cast<const std::byte*>(&bpp), sizeof(bpp) };
        run_bloom_pass(s.materials.bloom_prefilter, s.bloom_prefilter_inst,
                       s.bloom_chain.mips[0], cd::rhi::LoadOp::kClear, bytes, bloom_first_frame);
    }
    for (std::uint32_t i = 0; i < 3U; ++i)
    {
        run_bloom_pass(s.materials.bloom_downsample, s.bloom_down_insts[i],
                       s.bloom_chain.mips[i + 1U], cd::rhi::LoadOp::kClear, {}, bloom_first_frame);
    }
    for (std::uint32_t i = 0; i < 3U; ++i)
    {
        const std::uint32_t           dst_index = 2U - i;
        cd::post::bloom::UpsamplePush bup {};
        bup.params[0] = 1.0F;
        bup.params[1] = 1.0F;
        bup.params[2] = 0.0F;
        bup.params[3] = 0.0F;
        const std::span<const std::byte> bytes { reinterpret_cast<const std::byte*>(&bup), sizeof(bup) };
        run_bloom_pass(s.materials.bloom_upsample, s.bloom_up_insts[i],
                       s.bloom_chain.mips[dst_index], cd::rhi::LoadOp::kLoad, bytes, bloom_first_frame);
    }
}

void begin_composite_pass(StudioEngineState& s,
                          cd::rhi::ICommandBuffer& cmd,
                          cd::rhi::TextureViewHandle swapchain_view,
                          cd::rhi::Extent2D extent,
                          std::chrono::steady_clock::time_point frame_loop_start,
                          const cd::math::Mat4f& vp_unjittered)
{
    const std::uint32_t read_idx  = s.frame_idx & 1U;
    const std::uint32_t write_idx = 1U - read_idx;

    {
        const std::array<cd::rhi::TextureBarrier, 2> hb {
            cd::rhi::TextureBarrier { .texture = s.rts.history[read_idx].image,
                                      .from    = s.history_states[read_idx],
                                      .to      = cd::rhi::ResourceState::kShaderResource,
                                      .range   = { 0, 1, 0, 1 } },
            cd::rhi::TextureBarrier { .texture = s.rts.history[write_idx].image,
                                      .from    = s.history_states[write_idx],
                                      .to      = cd::rhi::ResourceState::kColorAttachment,
                                      .range   = { 0, 1, 0, 1 } },
        };
        cmd.barrier({}, hb);
        s.history_states[read_idx]  = cd::rhi::ResourceState::kShaderResource;
        s.history_states[write_idx] = cd::rhi::ResourceState::kColorAttachment;
    }

    std::array<cd::rhi::ColorAttachmentInfo, 2> swap_attach {
        cd::rhi::ColorAttachmentInfo { .view        = swapchain_view,
                                       .load_op     = cd::rhi::LoadOp::kClear,
                                       .store_op    = cd::rhi::StoreOp::kStore,
                                       .clear_color = { .f32 = { 0.0F, 0.0F, 0.0F, 1.0F } } },
        cd::rhi::ColorAttachmentInfo { .view        = s.rts.history[write_idx].view,
                                       .load_op     = cd::rhi::LoadOp::kClear,
                                       .store_op    = cd::rhi::StoreOp::kStore,
                                       .clear_color = { .f32 = { 0.0F, 0.0F, 0.0F, 1.0F } } },
    };
    cd::rhi::RenderPassBeginInfo swap_rp {};
    swap_rp.render_area       = cd::rhi::Rect2D { { 0, 0 }, extent };
    swap_rp.color_attachments = swap_attach;
    swap_rp.depth_stencil     = nullptr;
    cmd.begin_render_pass(swap_rp);
    cmd.set_viewport(cd::rhi::Viewport {
        0.0F, 0.0F, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0F, 1.0F });
    cmd.set_scissor(cd::rhi::Rect2D { { 0, 0 }, extent });
    s.materials.composite.apply(cmd);
    s.composite_insts[read_idx].bind(cmd, 0);

    const float aspect = static_cast<float>(extent.width) /
                         std::max(1.0F, static_cast<float>(extent.height));
    const auto  basis       = cd::app_common::compute_camera_basis(s.cam.eye, s.cam.target);
    const float half_h      = std::max(1e-4F, std::tan(s.cam.fov_y * 0.5F));
    const float half_w      = std::max(1e-4F, half_h * aspect);
    const bool  basis_valid = (cd::math::length(s.cam.target - s.cam.eye) > 1e-5F);

    cd::post::composite::Push cp {};
    cp.fx[0] = static_cast<float>(s.fx.tonemap_op);
    cp.fx[1] = s.fx.exposure;
    cp.fx[2] = s.fx.saturation_boost;
    cp.fx[3] = s.fx.bloom_post;
    cp.ao[0] = s.fx.ao_strength;
    cp.ao[1] = 20.0F;
    cp.ao[2] = s.cam.near_z;
    cp.ao[3] = s.cam.far_z;

    const float focus_dist = cd::math::length(s.cam.eye - s.cam.target);
    cp.dof[0] = s.fx.dof_strength;
    cp.dof[1] = focus_dist;
    cp.dof[2] = 4.0F;
    cp.dof[3] = 8.0F;

    cp.shafts[0]  = 0.5F;
    cp.shafts[1]  = 0.5F;
    cp.shafts[2]  = -1.0F;
    cp.shafts[3]  = 1.0F;
    cp.sun_col[0] = 0.0F;
    cp.sun_col[1] = 0.0F;
    cp.sun_col[2] = 0.0F;
    cp.sun_col[3] = s.fx.clouds_coverage;

    for (const auto& lrow : s.lights)
    {
        if (!lrow.enabled || lrow.light.type != cd::light::LightType::kDirectional)
            continue;
        cp.sun_col[0] = lrow.light.color.x;
        cp.sun_col[1] = lrow.light.color.y;
        cp.sun_col[2] = lrow.light.color.z;
        if (!basis_valid)
            break;
        const cd::math::Vec3f to_sun {
            -lrow.light.direction.x, -lrow.light.direction.y, -lrow.light.direction.z };
        const float fwd_dot = cd::math::dot(to_sun, basis.forward);
        if (fwd_dot <= 0.0F)
            break;
        const float r_dot     = cd::math::dot(to_sun, basis.right);
        const float u_dot     = cd::math::dot(to_sun, basis.up);
        const float sun_ndc_x = (r_dot / fwd_dot) / half_w;
        const float sun_ndc_y = (u_dot / fwd_dot) / half_h;
        cp.shafts[0] = 0.5F + 0.5F * sun_ndc_x;
        cp.shafts[1] = 0.5F - 0.5F * sun_ndc_y;
        const float ndc_max   = std::max(std::abs(sun_ndc_x), std::abs(sun_ndc_y));
        const float t         = std::clamp((1.5F - ndc_max) / 1.0F, 0.0F, 1.0F);
        const float edge_fade = t * t * (3.0F - 2.0F * t);
        cp.shafts[2] = s.fx.shafts_strength * edge_fade;
        cp.shafts[3] = 1.6F;
        break;
    }

    cp.atmo[0] = s.fx.volumetric_fog_on ? -s.fx.fog_density : s.fx.fog_density;
    cp.atmo[1] = s.fx.aerial_perspective;
    cp.atmo[2] = s.fx.vignette_strength;
    cp.atmo[3] = s.fx.film_grain;
    cp.lens[0] = s.fx.chromab_strength;

    cd::math::Vec3f sun_dir_world { 0.0F, -1.0F, 0.0F };
    for (const auto& lrow : s.lights)
    {
        if (lrow.enabled && lrow.light.type == cd::light::LightType::kDirectional)
        {
            sun_dir_world = lrow.light.direction;
            break;
        }
    }
    cp.lens[1] = sun_dir_world.x;
    cp.lens[2] = sun_dir_world.y;
    cp.lens[3] = sun_dir_world.z;

    cp.cam_right[0] = basis.right.x;
    cp.cam_right[1] = basis.right.y;
    cp.cam_right[2] = basis.right.z;
    cp.cam_right[3] = half_w;
    cp.cam_up[0]    = basis.up.x;
    cp.cam_up[1]    = basis.up.y;
    cp.cam_up[2]    = basis.up.z;
    cp.cam_up[3]    = half_h;
    cp.cam_fwd[0]   = basis.forward.x;
    cp.cam_fwd[1]   = basis.forward.y;
    cp.cam_fwd[2]   = basis.forward.z;
    cp.cam_fwd[3]   = (s.frame_idx > 0U) ? s.fx.taa_amount : 0.0F;
    cp.cam_pos[0]   = s.cam.eye.x;
    cp.cam_pos[1]   = s.cam.eye.y;
    cp.cam_pos[2]   = s.cam.eye.z;
    cp.cam_pos[3]   = std::chrono::duration<float>(
        std::chrono::steady_clock::now() - frame_loop_start).count();

    cp.ssr[0] = s.fx.ssr_strength;
    cp.ssr[1] = 25.0F;
    cp.ssr[2] = 24.0F;
    cp.ssr[3] = 1.5F;

    {
        const auto& pb    = s.prev_cam_basis;
        const bool  first = !pb.valid;
        const cd::math::Vec3f pr =
            first ? cd::math::Vec3f { cp.cam_right[0], cp.cam_right[1], cp.cam_right[2] } : pb.right;
        const cd::math::Vec3f pu =
            first ? cd::math::Vec3f { cp.cam_up[0], cp.cam_up[1], cp.cam_up[2] } : pb.up;
        const cd::math::Vec3f pf =
            first ? cd::math::Vec3f { cp.cam_fwd[0], cp.cam_fwd[1], cp.cam_fwd[2] } : pb.fwd;
        const cd::math::Vec3f pp =
            first ? cd::math::Vec3f { cp.cam_pos[0], cp.cam_pos[1], cp.cam_pos[2] } : pb.pos;
        const float phw = first ? cp.cam_right[3] : pb.half_w;
        const float phh = first ? cp.cam_up[3] : pb.half_h;

        cp.prev_cam_right[0] = pr.x;
        cp.prev_cam_right[1] = pr.y;
        cp.prev_cam_right[2] = pr.z;
        cp.prev_cam_right[3] = phw;
        cp.prev_cam_up[0]    = pu.x;
        cp.prev_cam_up[1]    = pu.y;
        cp.prev_cam_up[2]    = pu.z;
        cp.prev_cam_up[3]    = phh;
        cp.prev_cam_fwd[0]   = pf.x;
        cp.prev_cam_fwd[1]   = pf.y;
        cp.prev_cam_fwd[2]   = pf.z;
        cp.prev_cam_fwd[3]   = s.fx.motion_blur;
        cp.prev_cam_pos[0]   = pp.x;
        cp.prev_cam_pos[1]   = pp.y;
        cp.prev_cam_pos[2]   = pp.z;
        cp.prev_cam_pos[3]   = 8.0F;
    }

    cmd.push_constants(
        s.materials.composite.pipeline_layout(),
        cd::rhi::ShaderStage::kFragment,
        0,
        sizeof(cp),
        &cp);
    cmd.draw(3, 1, 0, 0);

    s.prev_cam_basis.right  = { cp.cam_right[0], cp.cam_right[1], cp.cam_right[2] };
    s.prev_cam_basis.up     = { cp.cam_up[0], cp.cam_up[1], cp.cam_up[2] };
    s.prev_cam_basis.fwd    = { cp.cam_fwd[0], cp.cam_fwd[1], cp.cam_fwd[2] };
    s.prev_cam_basis.pos    = { cp.cam_pos[0], cp.cam_pos[1], cp.cam_pos[2] };
    s.prev_cam_basis.half_w = cp.cam_right[3];
    s.prev_cam_basis.half_h = cp.cam_up[3];
    s.prev_cam_basis.valid  = true;
    s.prev_vp_unjittered    = vp_unjittered;
    s.prev_vp_valid         = true;
}

HdrSceneFrame make_hdr_frame(std::uint32_t frame_idx,
                             cd::rhi::Extent2D extent,
                             const cd::camera::Camera& cam,
                             float taa_amount) noexcept
{
    HdrSceneFrame out {};
    out.aspect        = static_cast<float>(extent.width) /
                        std::max(1.0F, static_cast<float>(extent.height));
    out.vp_unjittered = cd::camera::view_projection(cam, out.aspect);

    const cd::math::Vec2f jitter_px =
        (taa_amount > 0.001F)
            ? cd::post::taa::jitter_offset(frame_idx, 8U)
            : cd::math::Vec2f { 0.0F, 0.0F };
    const float jx_ndc = jitter_px.x * 2.0F / std::max(1.0F, static_cast<float>(extent.width));
    const float jy_ndc = jitter_px.y * 2.0F / std::max(1.0F, static_cast<float>(extent.height));
    out.vp             = out.vp_unjittered;
    for (std::size_t c = 0; c < 4U; ++c)
    {
        out.vp[c][0] += jx_ndc * out.vp_unjittered[c][3];
        out.vp[c][1] += jy_ndc * out.vp_unjittered[c][3];
    }
    return out;
}

HdrSceneFrame begin_hdr_scene_pass(StudioEngineState& s,
                                   cd::rhi::ICommandBuffer& cmd,
                                   cd::rhi::Extent2D extent)
{
    hdr_pass_barriers(cmd, s.frame_idx, s.rts.hdr, s.rts.gbuf_normal, s.rts.gbuf_albedo, s.rts.gbuf_mr);
    const HdrPassAttach a = hdr_pass_attach(
        s.rts.hdr, s.rts.gbuf_normal, s.rts.gbuf_albedo, s.rts.gbuf_mr, s.rts.depth);
    cd::rhi::RenderPassBeginInfo rp {};
    rp.render_area       = cd::rhi::Rect2D { { 0, 0 }, extent };
    rp.color_attachments = a.color;
    rp.depth_stencil     = &a.depth;
    cmd.begin_render_pass(rp);
    cmd.set_viewport(cd::rhi::Viewport {
        0.0F, 0.0F, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0F, 1.0F });
    cmd.set_scissor(cd::rhi::Rect2D { { 0, 0 }, extent });
    return make_hdr_frame(s.frame_idx, extent, s.cam, s.fx.taa_amount);
}

std::pair<HdrSceneFrame, std::unique_ptr<cd::rhi::IParallelPassRecorder>>
begin_hdr_scene_pass_lanes(StudioEngineState& s,
                           cd::rhi::ICommandBuffer& cmd,
                           cd::rhi::Extent2D extent,
                           std::uint32_t lane_count)
{
    hdr_pass_barriers(cmd, s.frame_idx, s.rts.hdr, s.rts.gbuf_normal, s.rts.gbuf_albedo, s.rts.gbuf_mr);
    const HdrPassAttach a = hdr_pass_attach(
        s.rts.hdr, s.rts.gbuf_normal, s.rts.gbuf_albedo, s.rts.gbuf_mr, s.rts.depth);
    cd::rhi::RenderPassBeginInfo rp {};
    rp.render_area       = cd::rhi::Rect2D { { 0, 0 }, extent };
    rp.color_attachments = a.color;
    rp.depth_stencil     = &a.depth;
    auto rec = cmd.begin_parallel_render_pass(rp, lane_count);
    if (rec == nullptr)
    {
        cmd.begin_render_pass(rp);
        cmd.set_viewport(cd::rhi::Viewport {
            0.0F, 0.0F, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0F, 1.0F });
        cmd.set_scissor(cd::rhi::Rect2D { { 0, 0 }, extent });
    }
    return { make_hdr_frame(s.frame_idx, extent, s.cam, s.fx.taa_amount), std::move(rec) };
}

void prep_hdr_lane(cd::rhi::IDrawRecorder& lane, cd::rhi::Extent2D extent)
{
    lane.set_viewport(cd::rhi::Viewport {
        0.0F, 0.0F, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0F, 1.0F });
    lane.set_scissor(cd::rhi::Rect2D { { 0, 0 }, extent });
}

void draw_floor_and_entities(StudioEngineState& s,
                             cd::rhi::IDrawRecorder& cmd,
                             float floor_y,
                             const cd::math::Mat4f& vp,
                             const SunLight& sun,
                             bool show_editor_floor)
{
    auto emit_floor_draw = [&](float sentinel_w)
    {
        cmd.bind_vertex_buffer(0, s.meshes.floor.vb, 0);
        cmd.bind_index_buffer(s.meshes.floor.ib, 0, s.meshes.floor.index_type);
        cd::math::Mat4f floor_model = cd::math::Mat4f::identity();
        floor_model[3][1]           = floor_y;
        PrimPush fp {};
        fp.mvp      = vp * floor_model;
        fp.model    = floor_model;
        fp.tint[0]  = 0.15F;
        fp.tint[1]  = 0.16F;
        fp.tint[2]  = 0.18F;
        fp.tint[3]  = sentinel_w;
        fill_prim_push_shared(fp, s.fx, sun, s.cam);
        fp.fx_params[1]  = 0.0F;
        fp.fx_params[2]  = 0.0F;
        fp.fx_params4[0] = fp.fx_params4[1] = fp.fx_params4[2] = 0.0F;
        fp.fx_params4[3] = static_cast<float>(s.fx.view_mode);
        cmd.push_constants(
            s.materials.prim.pipeline_layout(),
            cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
            0,
            sizeof(fp),
            &fp);
        cmd.draw_indexed(s.meshes.floor.index_count, 1, 0, 0, 0);
        s.counters.increment("draws_prim");
    };

    if (show_editor_floor && cd::studio_rt::golden::enabled())
    {
        emit_floor_draw(2.0F);
    }

    static thread_local std::vector<PrimPush>     ent_push_scratch;
    static thread_local std::vector<std::uint8_t> ent_push_valid;
    ent_push_scratch.resize(s.entities.size());
    ent_push_valid.assign(s.entities.size(), 0U);

    auto prep_entity_push = [&](std::size_t i)
    {
        const auto& ent = s.entities[i];
        if (!ent.visible)
            return;
        if (ent.name.find("Ground_Floor") != std::string::npos &&
            !ent.use_texture && ent.custom_texture_id == 0)
            return;
        if (ent.name.find("Volumetric_Clouds") != std::string::npos ||
            ent.name.find("Volumetric_Fog") != std::string::npos ||
            ent.name.find("Camera") != std::string::npos)
            return;
        const auto& mesh = s.mesh_for(ent.kind);
        if (!mesh.vb.is_valid())
            return;
        const auto* lt = s.scene.local(ent.handle);
        if (lt == nullptr)
            return;
        auto model = cd::math::to_mat4(lt->value);
        if (ent.name.find("Ground_Floor") != std::string::npos)
        {
            model[3][1] = std::min(model[3][1], -0.28F);
        }
        PrimPush& pp = ent_push_scratch[i];
        pp.mvp       = vp * model;
        pp.model     = model;
        pp.tint[0]   = ent.tint.x;
        pp.tint[1]   = ent.tint.y;
        pp.tint[2]   = ent.tint.z;

        const bool has_custom_override_tex =
            (ent.kind == PrimitiveKind::kGltf && ent.use_texture && ent.custom_texture_id >= 2);
        const bool is_gltf_prim =
            (ent.kind == PrimitiveKind::kSponza ||
             (ent.kind == PrimitiveKind::kGltf && !has_custom_override_tex));
        pp.tint[3] = 1.0F;
        if (is_gltf_prim)
            pp.tint[3] = 4.0F;
        else if (ent.is_pbr)
            pp.tint[3] = 3.0F;

        fill_prim_push_shared(pp, s.fx, sun, s.cam);
        const bool  gltf_tex_path = ((ent.kind == PrimitiveKind::kGltf) && !has_custom_override_tex && s.has_gltf_texture);
        const bool  pbr_tex_path  = (!is_gltf_prim && ent.is_pbr && ent.use_texture);
        const float tex_slot_val  = static_cast<float>(std::max(1, ent.custom_texture_id));
        pp.fx_params[1]           = pbr_tex_path ? tex_slot_val : (gltf_tex_path ? 1.0F : 0.0F);

        if (ent.is_pbr && !is_gltf_prim)
        {
            pp.fx_params4[0]     = ent.metallic;
            pp.fx_params4[1]     = ent.roughness;
            const float irid_int = std::floor(std::clamp(ent.iridescence_nm, 0.0F, 1000.0F));
            const float ms_frac  = std::clamp(ent.multiscatter_comp * 0.99F, 0.0F, 0.99F);
            pp.fx_params4[2]     = irid_int + ms_frac;
        }
        else if (is_gltf_prim)
        {
            float m  = 0.0F;
            float r  = 0.9F;
            float ns = 0.0F;
            const auto ranges = s.prim_ranges_for(ent.kind);
            if (!ranges.empty())
            {
                m  = ranges.front().metallic;
                r  = ranges.front().roughness;
                ns = ranges.front().normal_strength;
            }
            pp.fx_params4[0] = m;
            pp.fx_params4[1] = r;
            pp.fx_params4[2] = ns;
        }
        else
        {
            pp.fx_params4[0] = s.fx.clearcoat_strength;
            pp.fx_params4[1] = s.fx.sheen_strength;
            pp.fx_params4[2] = s.fx.sss_strength;
        }
        pp.fx_params4[3]  = static_cast<float>(s.fx.view_mode);
        ent_push_valid[i] = 1U;
    };

    if (s.entities.size() >= 256U)
        cd::concurrency::parallel_for(std::size_t { 0 }, s.entities.size(), prep_entity_push);
    else
    {
        for (std::size_t i = 0; i < s.entities.size(); ++i)
            prep_entity_push(i);
    }

    for (std::size_t i = 0; i < s.entities.size(); ++i)
    {
        if (ent_push_valid[i] == 0U)
            continue;
        const auto& ent  = s.entities[i];
        const auto& mesh = s.mesh_for(ent.kind);
        cmd.bind_vertex_buffer(0, mesh.vb, 0);
        cmd.bind_index_buffer(mesh.ib, 0, mesh.index_type);
        cmd.push_constants(
            s.materials.prim.pipeline_layout(),
            cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
            0,
            sizeof(PrimPush),
            &ent_push_scratch[i]);

        const bool skip_prim_ranges =
            (ent.kind == PrimitiveKind::kGltf && ent.use_texture && ent.custom_texture_id >= 2);
        const auto ranges = skip_prim_ranges
            ? std::span<const cd_studio::GltfPrimRange> {}
            : s.prim_ranges_for(ent.kind);
        if (!ranges.empty())
        {
            bool dirty_shared_prim_inst = false;
            for (const auto& pr : ranges)
            {
                if (pr.index_count == 0U)
                    continue;
                if (pr.has_texture && pr.albedo_view.is_valid())
                {
                    if (pr.prim_inst.is_valid())
                    {
                        pr.prim_inst.bind(cmd, 0);
                    }
                    else
                    {
                        const std::array<cd::rhi::DescriptorWrite, 1> tw {
                            cd::rhi::DescriptorWrite {
                                .binding       = 4,
                                .array_element = 0,
                                .type          = cd::rhi::DescriptorType::kCombinedImageSampler,
                                .view          = pr.albedo_view,
                                .sampler       = s.albedo_sampler,
                            },
                        };
                        (void)s.prim_inst.update(tw);
                        s.prim_inst.bind(cmd, 0);
                        dirty_shared_prim_inst = true;
                    }
                }
                PrimPush sub_pp      = ent_push_scratch[i];
                sub_pp.fx_params[1]  = pr.has_texture ? 1.0F : 0.0F;
                sub_pp.fx_params[3]  = pr.alpha_cutoff;
                sub_pp.fx_params4[0] = pr.metallic;
                sub_pp.fx_params4[1] = pr.roughness;
                sub_pp.fx_params4[2] = pr.normal_strength;
                cmd.push_constants(
                    s.materials.prim.pipeline_layout(),
                    cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
                    0,
                    sizeof(PrimPush),
                    &sub_pp);
                cmd.draw_indexed(pr.index_count, 1, pr.index_offset, 0, 0);
                s.counters.increment("draws_prim");
            }
            if (dirty_shared_prim_inst && s.albedo_tex.view.is_valid())
            {
                std::vector<cd::rhi::DescriptorWrite> rw;
                rw.reserve(3);
                rw.push_back(cd::rhi::DescriptorWrite {
                    .binding       = 4,
                    .array_element = 0,
                    .type          = cd::rhi::DescriptorType::kCombinedImageSampler,
                    .view          = s.albedo_tex.view,
                    .sampler       = s.albedo_sampler });
                if (s.normal_tex.view.is_valid())
                {
                    rw.push_back(cd::rhi::DescriptorWrite {
                        .binding       = 8,
                        .array_element = 0,
                        .type          = cd::rhi::DescriptorType::kCombinedImageSampler,
                        .view          = s.normal_tex.view,
                        .sampler       = s.albedo_sampler });
                }
                if (s.mr_tex.view.is_valid())
                {
                    rw.push_back(cd::rhi::DescriptorWrite {
                        .binding       = 9,
                        .array_element = 0,
                        .type          = cd::rhi::DescriptorType::kCombinedImageSampler,
                        .view          = s.mr_tex.view,
                        .sampler       = s.albedo_sampler });
                }
                (void)s.prim_inst.update(rw);
            }
            s.prim_inst.bind(cmd, 0);
        }
        else
        {
            cmd.draw_indexed(mesh.index_count, 1, 0, 0, 0);
            s.counters.increment("draws_prim");
        }
    }

    if (show_editor_floor && !cd::studio_rt::golden::enabled())
    {
        emit_floor_draw(2.25F);
    }
}

void spawn_golden_fixture_entities(StudioEngineState& s, bool chrome_probe_mode)
{
    struct Seed
    {
        const char*     name {};
        cd::math::Vec3f pos {};
        cd::math::Vec3f tint {};
        PrimitiveKind   k {};
    };
    const std::array<Seed, 5> seeds { {
        { "Cube",     { -2.4F, 0.0F, 0.0F }, { 1.00F, 0.10F, 0.10F }, PrimitiveKind::kCube     },
        { "Sphere",   { -1.2F, 0.0F, 0.0F }, { 0.20F, 0.95F, 0.30F }, PrimitiveKind::kSphere   },
        { "Cone",     {  0.0F, 0.0F, 0.0F }, { 0.15F, 0.40F, 1.00F }, PrimitiveKind::kCone     },
        { "Cylinder", {  1.2F, 0.0F, 0.0F }, { 1.00F, 0.75F, 0.15F }, PrimitiveKind::kCylinder },
        { "Torus",    {  2.4F, 0.0F, 0.0F }, { 0.90F, 0.15F, 0.90F }, PrimitiveKind::kTorus    },
    } };
    for (const auto& seed : seeds)
    {
        SceneEntity e;
        e.handle = s.scene.create_node();
        e.name   = seed.name;
        e.tint   = seed.tint;
        e.kind   = seed.k;
        s.scene.local(e.handle)->value.position = seed.pos;
        s.entities.push_back(std::move(e));
    }

    // Sponza & CesiumMan
    if (s.meshes.gltf.vb.is_valid())
    {
        SceneEntity es;
        es.handle = s.scene.create_node();
        es.name   = std::string { "Sponza (" } + s.meshes.gltf_loaded_name + ")";
        es.tint   = { 1.0F, 1.0F, 1.0F };
        es.kind   = PrimitiveKind::kSponza;
        s.scene.local(es.handle)->value.position = { 0.0F, 0.0F, 0.0F };
        s.scene.local(es.handle)->value.scale    = { 0.01F, 0.01F, 0.01F };
        s.scene.local(es.handle)->value.rotation = { 0.0F, 0.0F, 0.0F, 1.0F };
        s.entities.push_back(std::move(es));
    }

    const auto& cesium_mesh = chrome_probe_mode ? cd::render::GpuMesh {} : s.meshes.gltf_cesium;
    if (cesium_mesh.vb.is_valid() || !s.meshes.gltf.vb.is_valid())
    {
        SceneEntity ec;
        ec.handle = s.scene.create_node();
        ec.tint   = { 1.0F, 1.0F, 1.0F };
        ec.kind   = PrimitiveKind::kGltf;
        if (cesium_mesh.vb.is_valid())
        {
            ec.name = std::string { "CesiumMan (" } + s.meshes.gltf_cesium_loaded_name + ")";
            s.scene.local(ec.handle)->value.position = { -4.0F, 0.0F, -1.5F };
            s.scene.local(ec.handle)->value.scale    = { 2.2F, 2.2F, 2.2F };
            s.scene.local(ec.handle)->value.rotation = { -0.7071068F, 0.0F, 0.0F, 0.7071068F };
        }
        else
        {
            ec.name = "Earth (procedural showcase)";
            s.scene.local(ec.handle)->value.position = { -4.5F, 0.7F, 0.0F };
            s.scene.local(ec.handle)->value.scale    = { 1.5F, 1.5F, 1.5F };
            s.scene.local(ec.handle)->value.rotation = { 0.0F, 0.0F, 0.0F, 1.0F };
        }
        s.entities.push_back(std::move(ec));
    }

    // 4x4 PBR sphere sweep
    for (const auto& slot : cd::studio_rt::build_pbr_demo_grid())
    {
        SceneEntity e;
        e.handle      = s.scene.create_node();
        e.name        = slot.name;
        e.kind        = PrimitiveKind::kSphere;
        e.tint        = slot.tint;
        e.is_pbr      = true;
        e.metallic    = slot.metallic;
        e.roughness   = slot.roughness;
        e.use_texture = false;
        s.scene.local(e.handle)->value.position = slot.position;
        s.scene.local(e.handle)->value.scale    = { slot.scale, slot.scale, slot.scale };
        s.entities.push_back(std::move(e));
    }

    if (chrome_probe_mode)
    {
        SceneEntity e;
        e.handle    = s.scene.create_node();
        e.name      = "ChromeProbe (phase797)";
        e.kind      = PrimitiveKind::kSphere;
        e.tint      = cd::math::Vec3f { 0.95F, 0.93F, 0.88F };
        e.is_pbr    = true;
        e.metallic  = 1.0F;
        e.roughness = 0.04F;
        s.scene.local(e.handle)->value.position = {
            cd::studio_rt::sponza_fixtures::kChromeProbePosition[0],
            cd::studio_rt::sponza_fixtures::kChromeProbePosition[1],
            cd::studio_rt::sponza_fixtures::kChromeProbePosition[2],
        };
        const float k = cd::studio_rt::sponza_fixtures::kChromeProbeScale;
        s.scene.local(e.handle)->value.scale = { k, k, k };
        s.entities.push_back(std::move(e));
    }
}

}  // namespace cd::studio::render
