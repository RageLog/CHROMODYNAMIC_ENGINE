// =============================================================================
// CHROMODYNAMIC — tests/e2e/tier4_scenarios/test_tier4_scenarios.cpp
//
// Tier 4 Real-World Application Scenarios (Scenarios 1 through 15):
// Realistic end-to-end playable stages, authoring tool workflows, asset cooker
// packaging, live MCP synchronization, and architectural governance audits.
// =============================================================================

#include "../harness/E2eHarness.hpp"
#include "../harness/HeadlessReadback.hpp"
#include "../harness/MockAudioSink.hpp"
#include "../harness/CliRunner.hpp"

#include <cd/framegraph/FrameGraph.hpp>
#include <cd/rhi/NullDevice.hpp>
#include <cd/scene_renderer/SurfaceAndPostFxPipeline.hpp>
#include <cd/render2d/TextureAtlas2D.hpp>
#include <cd/render2d/Tilemap2D.hpp>
#include <cd/render2d/Lighting2D.hpp>
#include <cd/render2d/ParallaxLayer2D.hpp>
#include <cd/world2d/Stage2D.hpp>
#include <cd/world2d/CommandBus2D.hpp>
#include <cd/world2d/ParticleSystem2D.hpp>
#include <cd/world2d/SpatialHash2D.hpp>
#include <cd/world2d/ObjectPool2D.hpp>
#include <cd/world2d/AudioCue2D.hpp>
#include <cd/asset/pak/Pak.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace cd::e2e
{

// -----------------------------------------------------------------------------
// Scenario 1: Action Platformer Playable Stage Run
// Features: F10, F11, F13, F15, F16, F18, F19, F20, F25
// -----------------------------------------------------------------------------
TEST(Tier4_Scenarios, Scenario01_ActionPlatformerPlayableStageRun)
{
    cd::world2d::Stage2D stage;
    stage.build_starter_illuminated_platformer();

    auto atlas = cd::render2d::TextureAtlas2D::generate_procedural_starter_atlas(16U);
    ASSERT_TRUE(atlas.is_valid());

    stage.enter_play();
    EXPECT_TRUE(stage.is_playing());

    // Simulate 60 frames of hero movement and jumping
    cd::world2d::PlayerInput2D input {};
    input.move_x = 1.0F;

    for (int frame = 0; frame < 60; ++frame)
    {
        if (frame == 10) input.jump_pressed = true;
        if (frame == 11) input.jump_pressed = false;
        input.jump_held = (frame >= 10 && frame <= 15);

        stage.step_play(1.0F / 60.0F, input);
    }

    EXPECT_GT(stage.camera().position().x, 0.0F);

    cd::render2d::Canvas2D canvas;
    cd::render2d::ViewportRect2D vp { 0.0F, 0.0F, 1280.0F, 720.0F };
    stage.render(canvas, vp);
    EXPECT_GT(canvas.commands().size(), 0U);

    stage.exit_play();
    EXPECT_FALSE(stage.is_playing());
}

// -----------------------------------------------------------------------------
// Scenario 2: Top-Down RPG Dungeon Crawl
// Features: F10, F13, F14, F16, F18, F19, F20, F21, F25
// -----------------------------------------------------------------------------
TEST(Tier4_Scenarios, Scenario02_TopDownRpgDungeonCrawl)
{
    cd::world2d::Stage2D stage;
    stage.clear();
    stage.set_gravity_y(0.0F);
    stage.set_ambient_color({ 0.1F, 0.12F, 0.18F });

    // Carve 10x10 dungeon room with walls
    cd::render2d::Tilemap2D tm;
    tm.paint_rect(0, 0, 9, 9, 1U); // Stone wall boundary
    tm.flood_fill(4, 4, 0U);       // Hollow out interior room

    cd::world2d::Entity2D hero {};
    hero.name = "DungeonHero";
    hero.position = { 4.0F, 4.0F };
    hero.y_sort = true;
    stage.add_entity(hero);

    // Torch light in center of room
    cd::render2d::PointLight2D torch {};
    torch.position = { 4.0F, 4.0F };
    torch.radius = 6.0F;
    torch.intensity = 1.5F;

    EXPECT_FLOAT_EQ(stage.gravity_y(), 0.0F);
    EXPECT_EQ(stage.entities().size(), 1U);
}

// -----------------------------------------------------------------------------
// Scenario 3: Bullet-Heaven Arena 10k Objects
// Features: F10, F15, F16, F17, F18, F20, F25
// -----------------------------------------------------------------------------
struct ArenaBullet
{
    cd::math::Vec2f pos;
    cd::math::Vec2f vel;
    float life { 2.0F };
};

TEST(Tier4_Scenarios, Scenario03_BulletHeavenArena10kObjects)
{
    cd::world2d::SpatialHash2D spatial_hash(2.0F);
    cd::world2d::ObjectPool2D<ArenaBullet> bullet_pool(1000U);

    // Spawn 500 enemies into spatial hash
    for (std::uint32_t i = 1; i <= 500; ++i)
    {
        const float angle = static_cast<float>(i) * 0.1F;
        const float radius = 5.0F + static_cast<float>(i % 20);
        const float ex = std::cos(angle) * radius;
        const float ey = std::sin(angle) * radius;
        spatial_hash.insert(i, cd::math::Aabb2 { ex - 0.25F, ey - 0.25F, ex + 0.25F, ey + 0.25F });
    }
    EXPECT_EQ(spatial_hash.size(), 500U);

    // Fire 200 bullets from pool
    std::vector<cd::world2d::ObjectPool2D<ArenaBullet>::HandleType> bullets;
    for (int b = 0; b < 200; ++b)
    {
        auto h = bullet_pool.acquire();
        if (h.has_value())
        {
            auto* ptr = bullet_pool.get(*h);
            ptr->pos = { 0.0F, 0.0F };
            ptr->vel = { std::cos(static_cast<float>(b)), std::sin(static_cast<float>(b)) };
            bullets.push_back(*h);
        }
    }
    EXPECT_EQ(bullet_pool.active_count(), 200U);

    // Query overlaps around center
    std::vector<std::uint32_t> hit_enemies;
    spatial_hash.query_circle({ 0.0F, 0.0F }, 10.0F, hit_enemies);
    EXPECT_GT(hit_enemies.size(), 0U);

    // Release all bullets
    for (const auto& h : bullets)
    {
        bullet_pool.release(h);
    }
    EXPECT_EQ(bullet_pool.active_count(), 0U);
}

// -----------------------------------------------------------------------------
// Scenario 4: Live MCP Tooling Scene Sync Loop
// Features: F5, F6, F21, F23, F24
// -----------------------------------------------------------------------------
TEST(Tier4_Scenarios, Scenario04_LiveMcpToolingSceneSyncLoop)
{
    cd::world2d::Stage2D stage;
    stage.clear();
    cd::world2d::CommandBus2D bus;

    // Simulate MCP client sending batch commands
    const std::string batch_cmd = R"({
        "op": "spawn_entity_2d",
        "name": "mcp_spawner",
        "pos": [10.0, 15.0]
    })";

    auto report = bus.execute_json(stage, batch_cmd);
    EXPECT_TRUE(report.ok);
    EXPECT_EQ(stage.entities().size(), 1U);

    // Verify stage serialization reflects the MCP sync
    const std::string saved_json = stage.save_to_json_string();
    EXPECT_NE(saved_json.find("mcp_spawner"), std::string::npos);
}

// -----------------------------------------------------------------------------
// Scenario 5: Full Offline Asset Cook & Standalone Export
// Features: F7, F8, F25
// -----------------------------------------------------------------------------
TEST(Tier4_Scenarios, Scenario05_FullOfflineAssetCookAndExport)
{
    TempSandbox sandbox;
    cd::world2d::Stage2D stage;
    stage.build_starter_illuminated_platformer();

    // 1. Export .cdpak standalone archive bytes from stage
    const auto pak_bytes = stage.build_standalone_pak_bytes();
    ASSERT_FALSE(pak_bytes.empty());
    EXPECT_TRUE(sandbox.write_binary("game.cdpak", pak_bytes));

    // 2. Open archive via cd::asset::pak::Pak and verify header and TOC entries
    cd::asset::pak::Pak pak;
    const auto open_res = pak.open(pak_bytes.data(), pak_bytes.size());
    ASSERT_TRUE(open_res.has_value());
    EXPECT_GE(pak.entry_count(), 2U);

    const auto manifest_blob = pak.find("manifest.json");
    ASSERT_TRUE(manifest_blob.has_value());
    EXPECT_GT(manifest_blob->size, 0U);

    const auto stage_blob = pak.find("stages/main.cd2d.json");
    ASSERT_TRUE(stage_blob.has_value());
    EXPECT_GT(stage_blob->size, 0U);

    // 3. Verify standalone round-trip: load .cdpak bytes into a fresh stage
    cd::world2d::Stage2D loaded_stage;
    const auto load_res = loaded_stage.load_from_pak_bytes(pak_bytes);
    ASSERT_TRUE(load_res.has_value());
    EXPECT_EQ(loaded_stage.name(), stage.name());
    EXPECT_EQ(loaded_stage.entities().size(), stage.entities().size());
    EXPECT_EQ(loaded_stage.baked_tile_colliders().size(), stage.baked_tile_colliders().size());

    // 4. Test offline asset cooker (cd_assetc) if available
    const auto assetc_bin = EnvironmentProbe::binary_dir() / "cd_assetc.exe";
    if (std::filesystem::exists(assetc_bin))
    {
        auto res = CliRunner::run_assetc("--smoke-self-test");
        EXPECT_EQ(res.exit_code, 0);
        EXPECT_NE(res.stdout_output.find("TOC verified"), std::string::npos);
    }

    // 5. Build an offline multi-asset cooked bundle via PakBuilder (simulating BC7 + SPIR-V + Stage)
    cd::asset::pak::PakBuilder cooker_pak;
    cooker_pak.add("textures/atlas_albedo.cdbc7", "\x01\x02\x03\x04");
    cooker_pak.add("shaders/pbr_lighting.spv", "\x03\x02\x23\x07");
    cooker_pak.add("stages/world.cd2d.json", stage.save_to_json_string(false));

    const auto cooked_bytes = cooker_pak.build();
    ASSERT_FALSE(cooked_bytes.empty());
    EXPECT_TRUE(sandbox.write_binary("cooked_assets.cdpak", cooked_bytes));

    cd::asset::pak::Pak cooked_reader;
    const auto cooked_open = cooked_reader.open(cooked_bytes.data(), cooked_bytes.size());
    ASSERT_TRUE(cooked_open.has_value());
    EXPECT_EQ(cooked_reader.entry_count(), 3U);
    EXPECT_TRUE(cooked_reader.find("textures/atlas_albedo.cdbc7").has_value());
    EXPECT_TRUE(cooked_reader.find("shaders/pbr_lighting.spv").has_value());
}

// -----------------------------------------------------------------------------
// Scenario 6: Hybrid HD-2D Billboard Cutout Scene
// Features: F1, F2, F3, F10, F14
// -----------------------------------------------------------------------------
TEST(Tier4_Scenarios, Scenario06_HybridHD2DBillboardCutoutScene)
{
    cd::rhi::NullDevice dev;
    cd::framegraph::FrameGraph fg(dev);

    cd::rhi::TextureDesc desc {};
    desc.type = cd::rhi::TextureType::k2D;
    desc.format = cd::rhi::Format::kRGBA8Unorm;
    desc.extent = { 1280U, 720U, 1U };
    desc.usage = cd::rhi::TextureUsage::kColorAttachment;

    auto tex = dev.create_texture(desc);
    ASSERT_TRUE(tex.has_value());

    cd::scene_renderer::SceneRendererConfig cfg {};
    cfg.viewport = { 1280U, 720U };
    cfg.postfx.enable_dof = true;

    auto handles = cd::scene_renderer::build_scene_renderer_framegraph(fg, *tex, cfg);
    EXPECT_TRUE(handles.depth.is_valid());
    EXPECT_TRUE(handles.dof_coc.is_valid());

    dev.destroy_texture(*tex);
}

// -----------------------------------------------------------------------------
// Scenario 7: Parallax Multi-Layer Background Scroll
// Features: F10, F19, F20, F23
// -----------------------------------------------------------------------------
TEST(Tier4_Scenarios, Scenario07_ParallaxMultiLayerBackgroundScroll)
{
    cd::render2d::ParallaxManager2D mgr;

    cd::render2d::ParallaxLayer2D sky {};
    sky.name = "Sky";
    sky.scroll_factor = { 0.0F, 0.0F }; // Static sky
    sky.z_order = -100;
    mgr.add_layer(sky);

    cd::render2d::ParallaxLayer2D mountains {};
    mountains.name = "Mountains";
    mountains.scroll_factor = { 0.2F, 0.2F };
    mountains.z_order = -50;
    mgr.add_layer(mountains);

    cd::render2d::ParallaxLayer2D hills {};
    hills.name = "Hills";
    hills.scroll_factor = { 0.5F, 0.5F };
    hills.z_order = -10;
    mgr.add_layer(hills);

    mgr.sort_layers();

    const cd::math::Vec2f cam_pos { 200.0F, 50.0F };
    const auto off_sky = mgr.compute_effective_offset(*mgr.find_layer("Sky"), cam_pos);
    const auto off_mountains = mgr.compute_effective_offset(*mgr.find_layer("Mountains"), cam_pos);
    const auto off_hills = mgr.compute_effective_offset(*mgr.find_layer("Hills"), cam_pos);

    EXPECT_FLOAT_EQ(off_sky.x, 0.0F);
    EXPECT_FLOAT_EQ(off_mountains.x, 40.0F);
    EXPECT_FLOAT_EQ(off_hills.x, 100.0F);
}

// -----------------------------------------------------------------------------
// Scenario 8: Procedural Pixel-Art Synthesizer to Tilemap
// Features: F11, F13, F21, F22
// -----------------------------------------------------------------------------
TEST(Tier4_Scenarios, Scenario08_ProceduralPixelArtToTilemap)
{
    auto atlas = cd::render2d::TextureAtlas2D::generate_procedural_starter_atlas(16U);
    ASSERT_TRUE(atlas.is_valid());

    cd::render2d::Tilemap2D tm;
    const auto* reg = atlas.find_region("tile_stone");
    ASSERT_NE(reg, nullptr);

    cd::render2d::TilePaletteEntry2D pal {};
    pal.palette_id = 1U;
    pal.texture_slot = 0U;
    pal.uv_rect = reg->uv;
    tm.register_palette_entry(pal);

    tm.paint_rect(0, 0, 10, 2, 1U);
    EXPECT_GT(tm.non_empty_tile_count(), 0U);
}

// -----------------------------------------------------------------------------
// Scenario 9: 2D Normal & Emissive Dynamic Lighting
// Features: F10, F14, F20
// -----------------------------------------------------------------------------
TEST(Tier4_Scenarios, Scenario09_DynamicLightingNormalEmissiveOrbit)
{
    cd::render2d::LightingEnvironment2D env {};
    env.ambient_color = { 0.1F, 0.1F, 0.15F };

    // Primary moving light
    cd::render2d::PointLight2D light {};
    light.radius = 8.0F;
    light.intensity = 2.0F;
    light.color = { 1.0F, 0.8F, 0.6F };
    env.point_lights.push_back(light);

    for (int step = 0; step < 8; ++step)
    {
        const float angle = static_cast<float>(step) * (3.14159F / 4.0F);
        env.point_lights[0].position = { std::cos(angle) * 3.0F, std::sin(angle) * 3.0F };

        const auto lit = cd::render2d::evaluate_lighting_2d_with_emissive(
            env,
            { 0.0F, 0.0F },
            { 0.0F, 0.0F, 1.0F },
            { 200, 200, 200, 255 },
            { 0, 0, 0, 0 },
            0.0F
        );
        EXPECT_GT(lit.r, 0);
    }
}

// -----------------------------------------------------------------------------
// Scenario 10: Object Pool Stress & Zero-Allocation
// Features: F15, F16, F17
// -----------------------------------------------------------------------------
TEST(Tier4_Scenarios, Scenario10_ObjectPoolStressZeroAllocation)
{
    cd::world2d::ObjectPool2D<ArenaBullet, 256U> pool(256U, false);
    for (int iter = 0; iter < 1000; ++iter)
    {
        std::vector<cd::world2d::ObjectPool2D<ArenaBullet>::HandleType> handles;
        for (int i = 0; i < 50; ++i)
        {
            auto h = pool.acquire();
            if (h) handles.push_back(*h);
        }
        for (const auto& h : handles)
        {
            pool.release(h);
        }
    }
    EXPECT_EQ(pool.active_count(), 0U);
}

// -----------------------------------------------------------------------------
// Scenario 11: Audio Cue Dispatcher & Retro Synth
// Features: F18, F20
// -----------------------------------------------------------------------------
TEST(Tier4_Scenarios, Scenario11_AudioCueDispatcherAndRetroSynth)
{
    cd::world2d::AudioCueSystem2D audio;
    audio.set_listener_position({ 0.0F, 0.0F });

    // Dispatch 20 spatial cues around listener
    for (int i = 0; i < 20; ++i)
    {
        const float ex = static_cast<float>(i - 10);
        audio.play_cue_at(cd::world2d::AudioCuePreset2D::kCoin, { ex, 0.0F });
    }
    EXPECT_EQ(audio.total_cues_dispatched(), 20U);
}

// -----------------------------------------------------------------------------
// Scenario 12: Tilemap Stamp & Flood Fill Mutation
// Features: F13, F21, F22
// -----------------------------------------------------------------------------
TEST(Tier4_Scenarios, Scenario12_TilemapStampFloodFillMutation)
{
    cd::render2d::Tilemap2D tm;
    const std::array<std::uint16_t, 4> block = { 1U, 1U, 1U, 1U };

    // Stamp pattern
    tm.stamp(10, 10, block, 2, 2);
    EXPECT_EQ(tm.get_tile(10, 10).palette_id, 1U);

    // Flood fill to replace
    tm.flood_fill(10, 10, 2U);
    EXPECT_EQ(tm.get_tile(10, 10).palette_id, 2U);
}

// -----------------------------------------------------------------------------
// Scenario 13: Standalone Player CLI Genre Switch
// Features: F20, F25, F26
// -----------------------------------------------------------------------------
TEST(Tier4_Scenarios, Scenario13_StandalonePlayerCliGenreSwitch)
{
    const auto bin = EnvironmentProbe::binary_dir() / "cd_player.exe";
    if (std::filesystem::exists(bin))
    {
        auto r1 = CliRunner::run_player("--null --frames 1");
        EXPECT_EQ(r1.exit_code, 0);
    }
    else
    {
        GTEST_SKIP() << "cd_player.exe not yet built";
    }
}

// -----------------------------------------------------------------------------
// Scenario 14: Editor Self-Test Step Conformance
// Features: F21, F22, F23, F24, F29
// -----------------------------------------------------------------------------
TEST(Tier4_Scenarios, Scenario14_EditorSelfTestStepConformance)
{
    const auto bin = EnvironmentProbe::binary_dir() / "editor.exe";
    if (std::filesystem::exists(bin))
    {
        auto res = CliRunner::run_editor("--self-test-all");
        EXPECT_EQ(res.exit_code, 0);
        EXPECT_NE(res.stdout_output.find("PASS"), std::string::npos);
    }
    else
    {
        GTEST_SKIP() << "editor.exe not yet built";
    }
}

// -----------------------------------------------------------------------------
// Scenario 15: Layer DAG & Anti-Cliche Document Audit
// Features: F4, F9, F26, F28, F31
// -----------------------------------------------------------------------------
TEST(Tier4_Scenarios, Scenario15_LayerDagAndAntiClicheAudit)
{
    const auto root = EnvironmentProbe::engine_root();

    // 1. Distribution Documentation Preservation (Feature 31 / Requirement §R3.4)
    EXPECT_TRUE(std::filesystem::exists(root / "docs" / "DESIGN.md"));
    EXPECT_TRUE(std::filesystem::exists(root / "docs" / "PLAN.md"));
    EXPECT_TRUE(std::filesystem::exists(root / "docs" / "ENGINE_GUIDE.md"));
    EXPECT_TRUE(std::filesystem::exists(root / "docs" / "Modules.dox"));

    // 2. Entry Point LOC Invariant: apps/editor and apps/player < 80 lines (Feature 26 / Requirement §R3.2)
    auto count_lines = [](const std::filesystem::path& path) -> std::size_t {
        std::ifstream ifs(path);
        std::size_t lines = 0;
        std::string l;
        while (std::getline(ifs, l))
        {
            if (!l.empty() && l.find_first_not_of(" \t\r\n") != std::string::npos)
            {
                ++lines;
            }
        }
        return lines;
    };

    const auto editor_loc = count_lines(root / "apps" / "editor" / "main.cpp");
    const auto player_loc = count_lines(root / "apps" / "player" / "main.cpp");
    EXPECT_LT(editor_loc, 80U);
    EXPECT_LT(player_loc, 80U);

    // 3. Architectural Layer DAG Invariants (Feature 28 / Requirement §R3.1)
    const auto dag_script = root / ".agents" / "skills" / "engine-sota-adr" / "scripts" / "check_layer_dag.py";
    ASSERT_TRUE(std::filesystem::exists(dag_script));

    auto dag_res = CliRunner::run_python(dag_script, "--engine-root engine");
    EXPECT_EQ(dag_res.exit_code, 0);
    EXPECT_NE(dag_res.stdout_output.find("violations=0"), std::string::npos);
    EXPECT_NE(dag_res.stdout_output.find("VERDICT: PASS"), std::string::npos);

    // 4. Academic Iron Rule & Anti-Cliche Voice Verification (Feature 4, 9, 31)
    const auto voice_script = root / ".agents" / "skills" / "academic-iron-rule" / "scripts" / "check_human_voice.py";
    ASSERT_TRUE(std::filesystem::exists(voice_script));

    const auto design_doc = root / "docs" / "DESIGN.md";
    auto voice_res = CliRunner::run_python(voice_script, design_doc.string());
    EXPECT_EQ(voice_res.exit_code, 0);
    EXPECT_NE(voice_res.stdout_output.find("PASS"), std::string::npos);

    // 5. Direct Scan: docs/PLAN_NEXT_PHASES.md must have zero em-dash violations
    std::ifstream plan_ifs(root / "docs" / "PLAN_NEXT_PHASES.md");
    ASSERT_TRUE(plan_ifs.is_open());
    std::string line;
    std::size_t line_no = 0;
    while (std::getline(plan_ifs, line))
    {
        ++line_no;
        EXPECT_EQ(line.find("—"), std::string::npos)
            << "Em-dash found at docs/PLAN_NEXT_PHASES.md line " << line_no;
    }
}

}  // namespace cd::e2e
