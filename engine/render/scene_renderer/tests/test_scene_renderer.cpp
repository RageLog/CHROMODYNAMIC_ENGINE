// =============================================================================
// CHROMODYNAMIC — cd::scene_renderer unit tests (FAZ 4 Waves 4.1–4.4)
// =============================================================================
#include <cd/framegraph/FrameGraph.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/NullDevice.hpp>
#include <cd/scene_renderer/GlobalIlluminationPass.hpp>
#include <cd/scene_renderer/GpuDrivenPass.hpp>
#include <cd/scene_renderer/PhotometricAndAtmospherePass.hpp>
#include <cd/scene_renderer/SurfaceAndPostFxPipeline.hpp>

#include <gtest/gtest.h>

#include <array>
#include <vector>

namespace
{

// ---------------------------------------------------------------------------
// Wave 4.1: GPU-Driven Culling, DrawIndexedIndirectCount & Meshlet LOD Cut
// ---------------------------------------------------------------------------
TEST(SceneRendererWave41, FrustumAndHiZCullingBatchIndirectCount)
{
    cd::scene_renderer::GpuCullingConfig cfg {};
    cfg.hiz_furthest_depth_ndc = 0.75F;

    const std::array<cd::scene_renderer::CullableInstance, 3> instances {{
        // 0: visible inside frustum and in front of Hi-Z occluder
        { { 0.0F, 0.0F, 0.0F }, 2.0F, 360U, 0U, 0, 10U, 0.35F },
        // 1: outside frustum (x = -2000)
        { { -2000.0F, 0.0F, 0.0F }, 1.0F, 120U, 360U, 0, 11U, 0.20F },
        // 2: occluded by Hi-Z pyramid (min_depth_ndc = 0.90 > 0.75)
        { { 1.0F, 2.0F, 3.0F }, 1.0F, 240U, 480U, 0, 12U, 0.90F },
    }};

    const auto res = cd::scene_renderer::cull_and_batch_indirect(instances, cfg);
    EXPECT_EQ(res.draw_count, 1U);
    EXPECT_EQ(res.frustum_culled_count, 1U);
    EXPECT_EQ(res.hiz_culled_count, 1U);
    ASSERT_EQ(res.commands.size(), 1U);
    EXPECT_EQ(res.commands[0].index_count, 360U);
    EXPECT_EQ(res.commands[0].first_instance, 10U);
}

TEST(SceneRendererWave41, MeshletDagLodCutSelectsThresholdBoundary)
{
    const std::array<cd::scene_renderer::MeshletDagNode, 3> nodes {{
        // proj_self = 0.005*1000/10 = 0.5px <= 1.0px, proj_parent = 0.02*1000/10 = 2.0px > 1.0px -> SELECTED
        { 101U, 64U, 0.005F, 0.020F, 10.0F },
        // proj_self = 0.02*1000/10 = 2.0px > 1.0px -> too coarse
        { 102U, 64U, 0.020F, 0.080F, 10.0F },
        // proj_parent = 0.004*1000/10 = 0.4px <= 1.0px -> parent already fine enough
        { 103U, 64U, 0.001F, 0.004F, 10.0F },
    }};

    const auto stats = cd::scene_renderer::evaluate_meshlet_lod_cut(nodes, 1000.0F, 1.0F);
    ASSERT_EQ(stats.selected_meshlets.size(), 1U);
    EXPECT_EQ(stats.selected_meshlets[0], 101U);
    EXPECT_EQ(stats.total_triangles, 64U);
    EXPECT_EQ(stats.task_shader_groups, 1U);
}

// ---------------------------------------------------------------------------
// Wave 4.2: Photometric Units, IES, PCSS, Contact Shadows & Atmosphere
// ---------------------------------------------------------------------------
TEST(SceneRendererWave42, PhotometricUnitsAndShadows)
{
    const float cd_point = cd::scene_renderer::lumens_to_point_candela(1256.637F);
    EXPECT_NEAR(cd_point, 100.0F, 0.5F);

    const float cd_spot = cd::scene_renderer::lumens_to_spot_candela(1000.0F, 0.5F);
    EXPECT_GT(cd_spot, cd_point);

    const auto warm = cd::scene_renderer::kelvin_to_linear_rgb(2700.0F);
    const auto cool = cd::scene_renderer::kelvin_to_linear_rgb(10000.0F);
    EXPECT_GT(warm[0], warm[2]);
    EXPECT_GT(cool[2], cool[0]);

    const std::array<float, 3> ies { 100.0F, 50.0F, 0.0F };
    EXPECT_NEAR(cd::scene_renderer::eval_ies_profile(ies, 0.0F), 100.0F, 1e-3F);
    EXPECT_NEAR(cd::scene_renderer::eval_ies_profile(ies, 3.14159265F * 0.5F), 50.0F, 1e-2F);

    EXPECT_NEAR(cd::scene_renderer::eval_pcss_penumbra_radius(10.0F, 5.0F, 0.4F), 0.4F, 1e-4F);

    std::vector<float> depth(16 * 16, 0.5F);
    // Place an occluder at (8, 8) with depth 0.50; ray steps into 0.505 -> occluded.
    const float shadow = cd::scene_renderer::trace_contact_shadow(
        depth, 16U, 16U, 0.4F, 0.5F, 0.495F, 0.05F, 0.0F, 0.005F, 8U, 0.02F);
    EXPECT_FLOAT_EQ(shadow, 0.0F);
}

// ---------------------------------------------------------------------------
// Waves 4.2, 4.3, 4.4: Full SceneRenderer FrameGraph Compilation & Execution
// ---------------------------------------------------------------------------
TEST(SceneRendererWave43And44, FullFrameGraphCompilesAndExecutesAllFeaturePasses)
{
    cd::rhi::NullDevice dev;
    cd::rhi::TextureDesc back_desc {};
    back_desc.extent = { 1280U, 720U, 1U };
    back_desc.format = cd::rhi::Format::kRGBA8Unorm;
    back_desc.usage = cd::rhi::TextureUsage::kColorAttachment;
    auto back_tex = dev.create_texture(back_desc);
    ASSERT_TRUE(back_tex.has_value());

    cd::framegraph::FrameGraph fg(dev);
    cd::scene_renderer::SceneRendererConfig cfg {};
    cfg.viewport = { 1280U, 720U };
    cfg.gi.enable_nrc = true;

    const auto handles = cd::scene_renderer::build_scene_renderer_framegraph(fg, *back_tex, cfg);
    EXPECT_TRUE(handles.depth.is_valid());
    EXPECT_TRUE(handles.atmosphere_handles.transmittance_lut.is_valid());
    EXPECT_TRUE(handles.atmosphere_handles.froxel_fog_volume.is_valid());
    EXPECT_TRUE(handles.gi_handles.gtao_ao.is_valid());
    EXPECT_TRUE(handles.gi_handles.ddgi_irradiance.is_valid());
    EXPECT_TRUE(handles.gi_handles.restir_di_reservoir.is_valid());
    EXPECT_TRUE(handles.gi_handles.restir_gi_reservoir.is_valid());
    EXPECT_TRUE(handles.gi_handles.nrc_radiance.is_valid());
    EXPECT_TRUE(handles.gi_handles.denoised_gi.is_valid());
    EXPECT_TRUE(handles.final_backbuffer.is_valid());

    // 1(gbuffer) + 1(decals/terrain/water/particles) + 4(atmo/fog/clouds/shafts)
    // + 8(GTAO/SSR/DDGI/ReSTIR_DI/ReSTIR_GI/NRC/SVGF/IBL) + 5(velocity/TAA/bloom/DoF/composite) = 19 passes
    EXPECT_EQ(fg.pass_count(), 19U);

    ASSERT_TRUE(fg.compile().has_value());
    auto cmd = dev.create_command_buffer();
    cmd->begin();
    ASSERT_TRUE(fg.execute(*cmd).has_value());
    cmd->end();

    const auto hair = cd::scene_renderer::eval_kajiya_kay_hair(
        { 0.0F, 1.0F, 0.0F },
        { 0.0F, 0.0F, 1.0F },
        { 0.0F, 0.0F, 1.0F },
        { 0.4F, 0.2F, 0.1F }
    );
    EXPECT_GT(hair[0], 0.0F);

    const std::array<float, 4> heights { 0.4F, 0.5F, 0.7F, 0.9F };
    const auto pom_uv = cd::scene_renderer::eval_pom_uv_offset(
        { 0.5F, 0.5F }, { 0.2F, 0.1F, 1.0F }, 0.05F, heights);
    EXPECT_LT(pom_uv[0], 0.5F);
}

}  // namespace
