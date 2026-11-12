// =============================================================================
// CHROMODYNAMIC — cd::scene_renderer unit tests (FAZ 4 Waves 4.1–4.4)
// =============================================================================
#include <cd/framegraph/FrameGraph.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/NullDevice.hpp>
#include <cd/scene_renderer/GlobalIlluminationPass.hpp>
#include <cd/scene_renderer/GpuDrivenPass.hpp>
#include <cd/scene_renderer/PhotometricAndAtmospherePass.hpp>
#include <cd/scene_renderer/SceneRenderer.hpp>
#include <cd/scene_renderer/SurfaceAndPostFxPipeline.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
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

// ---------------------------------------------------------------------------
// FAZ 36 Waves 36.1 & 36.2: StageMode & HD2D Hybrid Pipeline
// ---------------------------------------------------------------------------
TEST(SceneRendererWave36, StageModeParsingAndStringConversion)
{
    EXPECT_EQ(cd::scene_renderer::parse_stage_mode("Pure2D"), cd::scene_renderer::StageMode::Pure2D);
    EXPECT_EQ(cd::scene_renderer::parse_stage_mode("2D"), cd::scene_renderer::StageMode::Pure2D);
    EXPECT_EQ(cd::scene_renderer::parse_stage_mode("HD2D"), cd::scene_renderer::StageMode::HD2D);
    EXPECT_EQ(cd::scene_renderer::parse_stage_mode("hd-2d"), cd::scene_renderer::StageMode::HD2D);
    EXPECT_EQ(cd::scene_renderer::parse_stage_mode("Pure3D"), cd::scene_renderer::StageMode::Pure3D);
    EXPECT_EQ(cd::scene_renderer::parse_stage_mode("anything_else"), cd::scene_renderer::StageMode::Pure3D);

    EXPECT_EQ(cd::scene_renderer::stage_mode_to_string(cd::scene_renderer::StageMode::Pure2D), "Pure2D");
    EXPECT_EQ(cd::scene_renderer::stage_mode_to_string(cd::scene_renderer::StageMode::HD2D), "HD2D");
    EXPECT_EQ(cd::scene_renderer::stage_mode_to_string(cd::scene_renderer::StageMode::Pure3D), "Pure3D");
}

TEST(SceneRendererWave36, HD2DSceneDataCompilesAndExecutesAll19Passes)
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

    cd::scene_renderer::SceneViewData scene_data {};
    scene_data.stage_mode = cd::scene_renderer::StageMode::HD2D;
    scene_data.camera_pos = { 0.0F, 5.0F, -10.0F };
    scene_data.camera_target = { 0.0F, 0.0F, 0.0F };
    scene_data.focus_distance_dof = 11.18F;

    // Add a 3D mesh instance
    cd::scene_renderer::SceneMeshInstance mesh {};
    mesh.tint = { 0.8F, 0.2F, 0.2F, 1.0F };
    mesh.metallic = 0.5F;
    mesh.roughness = 0.2F;
    mesh.index_count = 36U;
    scene_data.meshes.push_back(mesh);

    // Add an HD2D sprite card (billboard with alpha cutoff)
    cd::scene_renderer::SceneSpriteCardHD2D hero_card {};
    hero_card.position = { 0.0F, 0.5F, 0.0F };
    hero_card.size = { 1.5F, 1.5F };
    hero_card.tint = { 1.0F, 1.0F, 1.0F, 1.0F };
    hero_card.alpha_cutoff = 0.5F;
    hero_card.y_locked = true;
    hero_card.cast_shadows = true;
    scene_data.sprite_cards.push_back(hero_card);

    const auto handles = cd::scene_renderer::build_scene_renderer_framegraph(fg, *back_tex, cfg, &scene_data);
    EXPECT_TRUE(handles.depth.is_valid());
    EXPECT_TRUE(handles.hdr_scene.is_valid());
    EXPECT_TRUE(handles.final_backbuffer.is_valid());
    EXPECT_EQ(fg.pass_count(), 19U);

    ASSERT_TRUE(fg.compile().has_value());
    auto cmd = dev.create_command_buffer();
    cmd->begin();
    ASSERT_TRUE(fg.execute(*cmd).has_value());
    cmd->end();
}

// ---------------------------------------------------------------------------
// FAZ 36 Waves 36.1 & 36.2: Empirical Challenger Stress & Boundary Tests
// ---------------------------------------------------------------------------
TEST(SceneRendererChallenger, StageModeAdversarialBoundaryCases)
{
    // Empty & whitespace strings
    EXPECT_EQ(cd::scene_renderer::parse_stage_mode(""), cd::scene_renderer::StageMode::Pure3D);
    EXPECT_EQ(cd::scene_renderer::parse_stage_mode(" "), cd::scene_renderer::StageMode::Pure3D);
    EXPECT_EQ(cd::scene_renderer::parse_stage_mode("   "), cd::scene_renderer::StageMode::Pure3D);

    // Case variations & aliases
    EXPECT_EQ(cd::scene_renderer::parse_stage_mode("Pure2D"), cd::scene_renderer::StageMode::Pure2D);
    EXPECT_EQ(cd::scene_renderer::parse_stage_mode("pure2d"), cd::scene_renderer::StageMode::Pure2D);
    EXPECT_EQ(cd::scene_renderer::parse_stage_mode("2d"), cd::scene_renderer::StageMode::Pure2D);
    EXPECT_EQ(cd::scene_renderer::parse_stage_mode("2D"), cd::scene_renderer::StageMode::Pure2D);

    EXPECT_EQ(cd::scene_renderer::parse_stage_mode("HD2D"), cd::scene_renderer::StageMode::HD2D);
    EXPECT_EQ(cd::scene_renderer::parse_stage_mode("hd2d"), cd::scene_renderer::StageMode::HD2D);
    EXPECT_EQ(cd::scene_renderer::parse_stage_mode("hd-2d"), cd::scene_renderer::StageMode::HD2D);
    EXPECT_EQ(cd::scene_renderer::parse_stage_mode("HD-2D"), cd::scene_renderer::StageMode::HD2D);

    EXPECT_EQ(cd::scene_renderer::parse_stage_mode("Pure3D"), cd::scene_renderer::StageMode::Pure3D);
    EXPECT_EQ(cd::scene_renderer::parse_stage_mode("pure3d"), cd::scene_renderer::StageMode::Pure3D);
    EXPECT_EQ(cd::scene_renderer::parse_stage_mode("3d"), cd::scene_renderer::StageMode::Pure3D);
    EXPECT_EQ(cd::scene_renderer::parse_stage_mode("3D"), cd::scene_renderer::StageMode::Pure3D);

    // Arbitrary invalid strings fallback safely to Pure3D
    EXPECT_EQ(cd::scene_renderer::parse_stage_mode("unknown_stage"), cd::scene_renderer::StageMode::Pure3D);
    EXPECT_EQ(cd::scene_renderer::parse_stage_mode("null"), cd::scene_renderer::StageMode::Pure3D);
    EXPECT_EQ(cd::scene_renderer::parse_stage_mode("9999"), cd::scene_renderer::StageMode::Pure3D);

    // String conversion round-trips
    EXPECT_EQ(cd::scene_renderer::stage_mode_to_string(cd::scene_renderer::StageMode::Pure2D), "Pure2D");
    EXPECT_EQ(cd::scene_renderer::stage_mode_to_string(cd::scene_renderer::StageMode::HD2D), "HD2D");
    EXPECT_EQ(cd::scene_renderer::stage_mode_to_string(cd::scene_renderer::StageMode::Pure3D), "Pure3D");

    // Invalid enum integer cast returns safe default "Pure3D"
    EXPECT_EQ(cd::scene_renderer::stage_mode_to_string(static_cast<cd::scene_renderer::StageMode>(99)), "Pure3D");
    EXPECT_EQ(cd::scene_renderer::stage_mode_to_string(static_cast<cd::scene_renderer::StageMode>(255)), "Pure3D");
}

TEST(SceneRendererChallenger, FocusDistanceDoFAutofocusBoundaries)
{
    cd::rhi::NullDevice dev;
    cd::rhi::TextureDesc back_desc {};
    back_desc.extent = { 640U, 360U, 1U };
    back_desc.format = cd::rhi::Format::kRGBA8Unorm;
    back_desc.usage = cd::rhi::TextureUsage::kColorAttachment;
    auto back_tex = dev.create_texture(back_desc);
    ASSERT_TRUE(back_tex.has_value());

    // Case 1: HD2D mode with camera directly on top of 2D plane (eye == target -> distance = 0)
    {
        cd::framegraph::FrameGraph fg(dev);
        cd::scene_renderer::SceneRendererConfig cfg {};
        cfg.viewport = { 640U, 360U };
        cfg.postfx.enable_dof = true;

        cd::scene_renderer::SceneViewData scene_data {};
        scene_data.stage_mode = cd::scene_renderer::StageMode::HD2D;
        scene_data.camera_pos = { 0.0F, 0.0F, 0.0F };
        scene_data.camera_target = { 0.0F, 0.0F, 0.0F };

        const auto handles = cd::scene_renderer::build_scene_renderer_framegraph(fg, *back_tex, cfg, &scene_data);
        EXPECT_TRUE(handles.dof_coc.is_valid());
        ASSERT_TRUE(fg.compile().has_value());
        auto cmd = dev.create_command_buffer();
        cmd->begin();
        ASSERT_TRUE(fg.execute(*cmd).has_value());
        cmd->end();
    }

    // Case 2: Non-HD2D mode with negative focus distance (focus_distance_dof = -50.0F)
    {
        cd::framegraph::FrameGraph fg(dev);
        cd::scene_renderer::SceneRendererConfig cfg {};
        cfg.viewport = { 640U, 360U };
        cfg.postfx.enable_dof = true;

        cd::scene_renderer::SceneViewData scene_data {};
        scene_data.stage_mode = cd::scene_renderer::StageMode::Pure3D;
        scene_data.focus_distance_dof = -50.0F;

        const auto handles = cd::scene_renderer::build_scene_renderer_framegraph(fg, *back_tex, cfg, &scene_data);
        EXPECT_TRUE(handles.dof_coc.is_valid());
        ASSERT_TRUE(fg.compile().has_value());
        auto cmd = dev.create_command_buffer();
        cmd->begin();
        ASSERT_TRUE(fg.execute(*cmd).has_value());
        cmd->end();
    }

    // Case 3: Extreme distance (eye to target distance = 50,000)
    {
        cd::framegraph::FrameGraph fg(dev);
        cd::scene_renderer::SceneRendererConfig cfg {};
        cfg.viewport = { 640U, 360U };
        cfg.postfx.enable_dof = true;

        cd::scene_renderer::SceneViewData scene_data {};
        scene_data.stage_mode = cd::scene_renderer::StageMode::HD2D;
        scene_data.camera_pos = { 0.0F, 0.0F, 0.0F };
        scene_data.camera_target = { 30000.0F, 40000.0F, 0.0F }; // distance = 50000.0F

        const auto handles = cd::scene_renderer::build_scene_renderer_framegraph(fg, *back_tex, cfg, &scene_data);
        EXPECT_TRUE(handles.dof_coc.is_valid());
        ASSERT_TRUE(fg.compile().has_value());
        auto cmd = dev.create_command_buffer();
        cmd->begin();
        ASSERT_TRUE(fg.execute(*cmd).has_value());
        cmd->end();
    }
}

TEST(SceneRendererChallenger, AlphaCutoutAndHD2DSpriteCardBoundaries)
{
    cd::rhi::NullDevice dev;
    cd::rhi::TextureDesc back_desc {};
    back_desc.extent = { 640U, 360U, 1U };
    back_desc.format = cd::rhi::Format::kRGBA8Unorm;
    back_desc.usage = cd::rhi::TextureUsage::kColorAttachment;
    auto back_tex = dev.create_texture(back_desc);
    ASSERT_TRUE(back_tex.has_value());

    cd::framegraph::FrameGraph fg(dev);
    cd::scene_renderer::SceneRendererConfig cfg {};
    cfg.viewport = { 640U, 360U };

    cd::scene_renderer::SceneViewData scene_data {};
    scene_data.stage_mode = cd::scene_renderer::StageMode::HD2D;
    scene_data.camera_pos = { 0.0F, 1.0F, 5.0F };
    scene_data.camera_target = { 0.0F, 1.0F, 0.0F };

    // Card 1: Standard cutout threshold at 0.5f
    cd::scene_renderer::SceneSpriteCardHD2D c1 {};
    c1.position = { 0.0F, 1.0F, 0.0F };
    c1.size = { 1.0F, 1.0F };
    c1.alpha_cutoff = 0.5F;
    c1.y_locked = true;
    scene_data.sprite_cards.push_back(c1);

    // Card 2: Boundary cutout at 0.0f (opaque/all pass)
    cd::scene_renderer::SceneSpriteCardHD2D c2 {};
    c2.position = { 2.0F, 1.0F, 0.0F };
    c2.size = { 1.0F, 1.0F };
    c2.alpha_cutoff = 0.0F;
    c2.y_locked = false; // non-y-locked
    scene_data.sprite_cards.push_back(c2);

    // Card 3: Boundary cutout at 1.0f (strict mask)
    cd::scene_renderer::SceneSpriteCardHD2D c3 {};
    c3.position = { -2.0F, 1.0F, 0.0F };
    c3.size = { 1.0F, 1.0F };
    c3.alpha_cutoff = 1.0F;
    c3.y_locked = true;
    scene_data.sprite_cards.push_back(c3);

    // Card 4: Exactly on camera X/Z coords (dx=0, dz=0) triggering atan2(0,0)
    cd::scene_renderer::SceneSpriteCardHD2D c4 {};
    c4.position = { 0.0F, -5.0F, 5.0F }; // same x=0, z=5 as camera
    c4.size = { 1.0F, 1.0F };
    c4.alpha_cutoff = 0.5F;
    c4.y_locked = true;
    scene_data.sprite_cards.push_back(c4);

    // Card 5: Extreme negative coordinates & inverted size
    cd::scene_renderer::SceneSpriteCardHD2D c5 {};
    c5.position = { -10000.0F, -5000.0F, -20000.0F };
    c5.size = { -2.0F, -2.0F };
    c5.alpha_cutoff = 0.5F;
    c5.y_locked = false;
    scene_data.sprite_cards.push_back(c5);

    const auto handles = cd::scene_renderer::build_scene_renderer_framegraph(fg, *back_tex, cfg, &scene_data);
    EXPECT_TRUE(handles.gbuffer_albedo.is_valid());
    ASSERT_TRUE(fg.compile().has_value());
    auto cmd = dev.create_command_buffer();
    cmd->begin();
    ASSERT_TRUE(fg.execute(*cmd).has_value());
    cmd->end();
}

TEST(SceneRendererChallenger, NullSceneDataAndZeroViewportStress)
{
    cd::rhi::NullDevice dev;
    cd::rhi::TextureDesc back_desc {};
    back_desc.extent = { 1U, 1U, 1U };
    back_desc.format = cd::rhi::Format::kRGBA8Unorm;
    back_desc.usage = cd::rhi::TextureUsage::kColorAttachment;
    auto back_tex = dev.create_texture(back_desc);
    ASSERT_TRUE(back_tex.has_value());

    // Subtest 1: scene_data == nullptr with all 19 passes enabled
    {
        cd::framegraph::FrameGraph fg(dev);
        cd::scene_renderer::SceneRendererConfig cfg {};
        cfg.viewport = { 1280U, 720U };
        cfg.gi.enable_nrc = true;

        const auto handles = cd::scene_renderer::build_scene_renderer_framegraph(fg, *back_tex, cfg, nullptr);
        EXPECT_TRUE(handles.hdr_scene.is_valid());
        EXPECT_EQ(fg.pass_count(), 19U);
        ASSERT_TRUE(fg.compile().has_value());
        auto cmd = dev.create_command_buffer();
        cmd->begin();
        ASSERT_TRUE(fg.execute(*cmd).has_value());
        cmd->end();
    }

    // Subtest 2: Zero-sized viewport (0 x 0)
    {
        cd::framegraph::FrameGraph fg(dev);
        cd::scene_renderer::SceneRendererConfig cfg {};
        cfg.viewport = { 0U, 0U };
        cfg.gi.enable_nrc = true;

        cd::scene_renderer::SceneViewData scene_data {};
        scene_data.stage_mode = cd::scene_renderer::StageMode::HD2D;

        const auto handles = cd::scene_renderer::build_scene_renderer_framegraph(fg, *back_tex, cfg, &scene_data);
        EXPECT_TRUE(handles.hdr_scene.is_valid());
        ASSERT_TRUE(fg.compile().has_value());
        auto cmd = dev.create_command_buffer();
        cmd->begin();
        ASSERT_TRUE(fg.execute(*cmd).has_value());
        cmd->end();
    }

    // Subtest 3: Extreme 8K Viewport (7680 x 4320)
    {
        cd::framegraph::FrameGraph fg(dev);
        cd::scene_renderer::SceneRendererConfig cfg {};
        cfg.viewport = { 7680U, 4320U };

        cd::scene_renderer::SceneViewData scene_data {};
        scene_data.stage_mode = cd::scene_renderer::StageMode::HD2D;

        const auto handles = cd::scene_renderer::build_scene_renderer_framegraph(fg, *back_tex, cfg, &scene_data);
        EXPECT_TRUE(handles.hdr_scene.is_valid());
        ASSERT_TRUE(fg.compile().has_value());
        auto cmd = dev.create_command_buffer();
        cmd->begin();
        ASSERT_TRUE(fg.execute(*cmd).has_value());
        cmd->end();
    }

    // Subtest 4: High entity density stress test (128 meshes + 128 HD2D sprite cards)
    {
        cd::framegraph::FrameGraph fg(dev);
        cd::scene_renderer::SceneRendererConfig cfg {};
        cfg.viewport = { 1920U, 1080U };
        cfg.gi.enable_nrc = true;

        cd::scene_renderer::SceneViewData scene_data {};
        scene_data.stage_mode = cd::scene_renderer::StageMode::HD2D;
        scene_data.meshes.reserve(128);
        scene_data.sprite_cards.reserve(128);

        for (std::uint32_t i = 0; i < 128; ++i)
        {
            cd::scene_renderer::SceneMeshInstance m {};
            m.index_count = 36U;
            m.metallic = static_cast<float>(i % 10) * 0.1F;
            m.roughness = static_cast<float>((i + 1) % 10) * 0.1F;
            scene_data.meshes.push_back(m);

            cd::scene_renderer::SceneSpriteCardHD2D c {};
            c.position = { static_cast<float>(i) - 64.0F, 0.0F, static_cast<float>(i % 8) };
            c.size = { 1.0F, 1.0F };
            c.alpha_cutoff = 0.5F;
            c.y_locked = (i % 2 == 0);
            scene_data.sprite_cards.push_back(c);
        }

        const auto handles = cd::scene_renderer::build_scene_renderer_framegraph(fg, *back_tex, cfg, &scene_data);
        EXPECT_TRUE(handles.hdr_scene.is_valid());
        ASSERT_TRUE(fg.compile().has_value());
        auto cmd = dev.create_command_buffer();
        cmd->begin();
        ASSERT_TRUE(fg.execute(*cmd).has_value());
        cmd->end();
    }
}

}  // namespace
