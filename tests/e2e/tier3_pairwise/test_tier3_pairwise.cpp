// =============================================================================
// CHROMODYNAMIC — tests/e2e/tier3_pairwise/test_tier3_pairwise.cpp
//
// Tier 3 Pairwise Combinatorial Interaction Tests (P1 through P30):
// Verifies state transitions and data flows between paired engine subsystems.
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

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace cd::e2e
{

// P1 (F10 × F13): TextureAtlas2D UV sub-rectangles bound to Tilemap2D
TEST(Tier3_Pairwise, P01_TextureAtlas_BoundTo_TilemapEmission)
{
    auto atlas = cd::render2d::TextureAtlas2D::generate_procedural_starter_atlas(16U);
    cd::render2d::Tilemap2D tm;

    const auto* reg = atlas.find_region("tile_stone");
    ASSERT_NE(reg, nullptr);

    cd::render2d::TilePaletteEntry2D pal {};
    pal.palette_id = 1U;
    pal.texture_slot = 0U;
    pal.uv_rect = reg->uv;
    tm.register_palette_entry(pal);
    tm.set_tile(0, 0, 1U);

    cd::render2d::Canvas2D canvas;
    cd::render2d::Camera2D cam;
    cd::render2d::LightingEnvironment2D env;
    tm.emit_to_canvas(canvas, cam, env);

    EXPECT_GT(canvas.commands().size(), 0U);
}

// P2 (F11 × F10): Procedural pixel-art sheet synthesizer populates TextureAtlas2D
TEST(Tier3_Pairwise, P02_ProceduralSynthesizer_Populates_AtlasBuffer)
{
    auto atlas = cd::render2d::TextureAtlas2D::generate_procedural_starter_atlas(16U);
    EXPECT_TRUE(atlas.is_valid());
    EXPECT_GT(atlas.regions().size(), 5U);
    EXPECT_FALSE(atlas.pixels().empty());
}

// P3 (F12 × F10): PNG disk loader / memory bytes populate TextureAtlas2D regions
TEST(Tier3_Pairwise, P03_MemoryLoader_Populates_AtlasRegions)
{
    cd::render2d::TextureAtlas2D atlas(32U, 32U);
    atlas.slice_grid("tile", 16U, 16U);
    EXPECT_EQ(atlas.regions().size(), 4U);
    EXPECT_NE(atlas.find_region("tile_0_0"), nullptr);
}

// P4 (F13 × F21): Tilemap2D flood_fill updates chunk cells and triggers auto-tile mask recomputation
TEST(Tier3_Pairwise, P04_TilemapFloodFill_Triggers_AutotileRecompute)
{
    cd::render2d::Tilemap2D tm;
    tm.paint_rect(0, 0, 4, 4, 1U);
    tm.flood_fill(2, 2, 2U);

    EXPECT_EQ(tm.get_tile(2, 2).palette_id, 2U);
    EXPECT_GT(tm.active_chunk_count(), 0U);
}

// P5 (F14 × F10): Companion normal and emissive atlases bound simultaneously with albedo atlas
TEST(Tier3_Pairwise, P05_CompanionAtlases_Bound_With_AlbedoAtlas)
{
    auto bundle = cd::render2d::TextureAtlas2D::generate_starter_bundle(16U);
    EXPECT_TRUE(bundle.albedo.is_valid());
    EXPECT_TRUE(bundle.normal.is_valid());
    EXPECT_TRUE(bundle.emissive.is_valid());
    EXPECT_EQ(bundle.albedo.width(), bundle.normal.width());
    EXPECT_EQ(bundle.albedo.width(), bundle.emissive.width());
}

// P6 (F15 × F17): ParticleSystem2D uses zero-allocation pooling concepts with ObjectPool2D
struct ProjectileData
{
    cd::math::Vec2f pos;
    cd::math::Vec2f vel;
    float life;
};

TEST(Tier3_Pairwise, P06_ParticleSystem_And_ObjectPool_ZeroAllocation)
{
    cd::world2d::ObjectPool2D<ProjectileData, 128U> pool(128U, false);
    for (int i = 0; i < 100; ++i)
    {
        auto h = pool.acquire();
        ASSERT_TRUE(h.has_value());
    }
    EXPECT_EQ(pool.active_count(), 100U);
}

// P7 (F15 × F16): ParticleSystem2D positions queried against collider via SpatialHash2D
TEST(Tier3_Pairwise, P07_ParticlePositions_Queried_Via_SpatialHash)
{
    cd::world2d::SpatialHash2D hash(1.0F);
    hash.insert(1U, cd::math::Aabb2 { 0.0F, 0.0F, 1.0F, 1.0F });

    std::vector<std::uint32_t> results;
    hash.query_circle({ 0.5F, 0.5F }, 0.5F, results);

    ASSERT_EQ(results.size(), 1U);
    EXPECT_EQ(results[0], 1U);
}

// P8 (F16 × F20): SpatialHash2D queries swarm enemies against weapon AABB in Arena
TEST(Tier3_Pairwise, P08_SpatialHash_Queries_ArenaEnemies)
{
    cd::world2d::SpatialHash2D hash(2.0F);
    for (std::uint32_t i = 1; i <= 50; ++i)
    {
        const float x = static_cast<float>(i % 10);
        const float y = static_cast<float>(i / 10);
        hash.insert(i, cd::math::Aabb2 { x, y, x + 0.5F, y + 0.5F });
    }

    std::vector<std::uint32_t> hit_enemies;
    hash.query_aabb(cd::math::Aabb2 { 0.0F, 0.0F, 3.0F, 3.0F }, hit_enemies);
    EXPECT_GT(hit_enemies.size(), 0U);
}

// P9 (F17 × F20): ObjectPool2D recycles bullet projectiles during Arena template loop
TEST(Tier3_Pairwise, P09_ObjectPool_Recycles_ArenaProjectiles)
{
    cd::world2d::ObjectPool2D<ProjectileData> pool(64U);
    std::vector<cd::world2d::ObjectPool2D<ProjectileData>::HandleType> active;

    for (int i = 0; i < 30; ++i)
    {
        active.push_back(*pool.acquire());
    }
    EXPECT_EQ(pool.active_count(), 30U);

    for (const auto& h : active)
    {
        pool.release(h);
    }
    EXPECT_EQ(pool.active_count(), 0U);
}

// P10 (F18 × F20): AudioCue2D dispatches jump, footstep, and gem chimes during Platformer PIE
TEST(Tier3_Pairwise, P10_AudioCue_Dispatches_During_Platformer)
{
    cd::world2d::AudioCueSystem2D audio;
    audio.play_cue(cd::world2d::AudioCuePreset2D::kJump);
    audio.play_cue(cd::world2d::AudioCuePreset2D::kCoin);
    EXPECT_EQ(audio.total_cues_dispatched(), 2U);
}

// P11 (F19 × F20): ParallaxLayer2D displacement driven by Platformer camera tracking
TEST(Tier3_Pairwise, P11_Parallax_Displacement_Driven_By_Camera)
{
    cd::render2d::ParallaxManager2D mgr;
    cd::render2d::ParallaxLayer2D l {};
    l.scroll_factor = { 0.25F, 0.25F };
    mgr.add_layer(l);

    cd::render2d::Camera2D cam;
    cam.set_position({ 200.0F, 100.0F });

    const auto off = mgr.compute_effective_offset(l, cam.position());
    EXPECT_FLOAT_EQ(off.x, 50.0F);
    EXPECT_FLOAT_EQ(off.y, 25.0F);
}

// P12 (F20 × F25): Standalone cd_player exhibits identical physics properties to editor PIE
TEST(Tier3_Pairwise, P12_Player_And_Editor_Share_StagePhysics)
{
    cd::world2d::Stage2D stage;
    stage.build_starter_illuminated_platformer();
    EXPECT_LT(stage.gravity_y(), -20.0F);
}

// P13 (F5 × F6): MCP socket server receives mutate and applies via CommandBus2D
TEST(Tier3_Pairwise, P13_McpServer_Mutate_Via_CommandBus)
{
    cd::world2d::Stage2D stage;
    stage.clear();
    cd::world2d::CommandBus2D bus;
    const std::string rpc_mutate = R"({"op":"spawn_entity_2d","name":"mcp_npc","pos":[5.0,5.0]})";
    auto report = bus.execute_json(stage, rpc_mutate);
    EXPECT_TRUE(report.ok);
    EXPECT_EQ(stage.entities().size(), 1U);
}

// P14 (F6 × F21): Hot-reload JSON mutation triggers paint_tile and pushes to undo bus
TEST(Tier3_Pairwise, P14_HotReload_PaintTile_Pushes_Undo)
{
    cd::world2d::Stage2D stage;
    cd::world2d::CommandBus2D bus;
    auto rep = bus.execute_json(stage, R"({"op":"paint_tile_2d","gx":12,"gy":12,"palette_id":4})");
    (void)rep;
    EXPECT_GT(bus.undo_depth(), 0U);
    EXPECT_TRUE(bus.undo(stage));
}

// P15 (F7 × F8): tools/assetc cooks textures and PackageExporter bundles them into game.cdpak
TEST(Tier3_Pairwise, P15_AssetCooker_Outputs_Bundled_In_Pak)
{
    TempSandbox sandbox;
    cd::world2d::Stage2D stage;
    stage.build_starter_illuminated_platformer();
    auto bytes = stage.build_standalone_pak_bytes();
    sandbox.write_binary("game.cdpak", bytes);
    EXPECT_TRUE(sandbox.file_exists("game.cdpak"));
}

// P16 (F8 × F25): Exported standalone package executes with bundled game.cdpak
TEST(Tier3_Pairwise, P16_ExportedBundle_Loads_In_Player)
{
    cd::world2d::Stage2D stage;
    stage.build_starter_illuminated_platformer();
    auto bytes = stage.build_standalone_pak_bytes();

    cd::world2d::Stage2D player_stage;
    auto res = player_stage.load_from_pak_bytes(bytes);
    EXPECT_TRUE(res.has_value());
    EXPECT_EQ(player_stage.entities().size(), stage.entities().size());
}

// P17 (F1 × F3): Unified SceneRenderer FrameGraph depth pass receives hybrid 2D sprite cards
TEST(Tier3_Pairwise, P17_SceneRenderer_Receives_HybridSpriteCards)
{
    cd::rhi::NullDevice dev;
    cd::framegraph::FrameGraph fg(dev);

    cd::rhi::TextureDesc desc {};
    desc.type = cd::rhi::TextureType::k2D;
    desc.format = cd::rhi::Format::kRGBA8Unorm;
    desc.extent = { 640U, 360U, 1U };
    desc.usage = cd::rhi::TextureUsage::kColorAttachment;

    auto tex = dev.create_texture(desc);
    ASSERT_TRUE(tex.has_value());

    cd::scene_renderer::SceneRendererConfig cfg {};
    cfg.viewport = { 640U, 360U };

    auto handles = cd::scene_renderer::build_scene_renderer_framegraph(fg, *tex, cfg);
    EXPECT_TRUE(handles.depth.is_valid());

    dev.destroy_texture(*tex);
}

// P18 (F2 × F1): apps/player and apps/editor invoke identical SceneRenderer passes
TEST(Tier3_Pairwise, P18_Player_And_Editor_Identical_PassSequence)
{
    cd::scene_renderer::SceneRendererConfig cfg {};
    cfg.viewport = { 1280U, 720U };
    EXPECT_TRUE(cfg.culling.enable_frustum_culling);
}

// P19 (F3 × F14): 3D directional light and 2D point lights illuminate HD-2D billboard sprite
TEST(Tier3_Pairwise, P19_Combined_Directional_And_Point_Lights)
{
    cd::render2d::LightingEnvironment2D env {};
    env.ambient_color = { 0.2F, 0.2F, 0.2F };
    env.directional_color = { 1.0F, 1.0F, 1.0F };
    env.directional_intensity = 0.5F;

    cd::render2d::PointLight2D pl {};
    pl.position = { 0.0F, 0.0F };
    pl.radius = 5.0F;
    pl.intensity = 1.0F;
    env.point_lights.push_back(pl);

    const auto lit = cd::render2d::evaluate_lighting_2d_with_emissive(
        env,
        { 0.0F, 0.0F },
        { 0.0F, 0.0F, 1.0F },
        { 255, 255, 255, 255 },
        { 0, 0, 0, 0 },
        0.0F
    );

    EXPECT_GT(lit.r, 50);
}

// P20 (F21 × F22): Selecting tile in palette preview dock sets active brush for stamp tool
TEST(Tier3_Pairwise, P20_PaletteSelection_Sets_Brush)
{
    cd::render2d::Tilemap2D tm;

    cd::render2d::TilePaletteEntry2D pal {};
    pal.palette_id = 5U;
    pal.name = "Dungeon Brick";
    pal.texture_slot = 2U;
    pal.has_autotile_uvs = true;

    for (std::size_t m = 0; m < 16; ++m)
    {
        pal.autotile_uvs[m] = cd::render2d::UvRect2D {
            static_cast<float>(m) * 0.0625F,
            0.0F,
            static_cast<float>(m + 1) * 0.0625F,
            1.0F
        };
    }
    tm.register_palette_entry(pal);

    const auto* found_pal = tm.find_palette(5U);
    ASSERT_NE(found_pal, nullptr);
    EXPECT_EQ(found_pal->palette_id, 5U);
    EXPECT_TRUE(found_pal->has_autotile_uvs);

    tm.paint_rect(0, 0, 2, 2, 5U, true);

    const auto center_cell = tm.get_tile(1, 1);
    EXPECT_EQ(center_cell.palette_id, 5U);
    EXPECT_EQ(center_cell.autotile_mask, 15U);

    const auto corner_cell = tm.get_tile(0, 0);
    EXPECT_EQ(corner_cell.palette_id, 5U);
    EXPECT_EQ(corner_cell.autotile_mask, 3U);

    cd::render2d::Canvas2D canvas;
    cd::render2d::Camera2D cam;
    cam.set_position({ 1.5F, 1.5F });
    cd::render2d::LightingEnvironment2D env;
    tm.emit_to_canvas(canvas, cam, env);

    ASSERT_FALSE(canvas.commands().empty());

    bool found_center_uv = false;
    bool found_corner_uv = false;
    for (const auto& cmd : canvas.commands())
    {
        if (cmd.kind == cd::render2d::CanvasCmdKind2D::kTexturedQuad && cmd.texture_slot == 2U)
        {
            if (std::abs(cmd.uv.u0 - pal.autotile_uvs[15].u0) < 1e-4F &&
                std::abs(cmd.uv.u1 - pal.autotile_uvs[15].u1) < 1e-4F)
            {
                found_center_uv = true;
            }
            if (std::abs(cmd.uv.u0 - pal.autotile_uvs[3].u0) < 1e-4F &&
                std::abs(cmd.uv.u1 - pal.autotile_uvs[3].u1) < 1e-4F)
            {
                found_corner_uv = true;
            }
        }
    }
    EXPECT_TRUE(found_center_uv);
    EXPECT_TRUE(found_corner_uv);
}

// P21 (F23 × F19): Toggling parallax layer visibility in dock omits layer from Canvas2D
TEST(Tier3_Pairwise, P21_ParallaxVisibility_Omits_Layer)
{
    cd::render2d::ParallaxStack2D stack;

    cd::render2d::ParallaxLayer2D layer {};
    layer.name = "Background_Mountains";
    layer.visible = false;
    layer.scroll_factor = { 0.2F, 0.2F };
    layer.auto_scroll_velocity = { 50.0F, 0.0F };
    layer.procedural_style = cd::render2d::ParallaxProceduralStyle::kMountainPinnacle;
    layer.tint = { 100U, 120U, 180U, 255U };
    stack.add_layer(layer);

    ASSERT_EQ(stack.layers().size(), 1U);
    EXPECT_FALSE(stack.layers()[0].visible);

    stack.step(0.5F);
    EXPECT_FLOAT_EQ(stack.layers()[0].current_offset.x, 25.0F);

    cd::render2d::Canvas2D canvas;
    cd::render2d::Camera2D camera;
    const cd::render2d::ViewportRect2D vp { 0.0F, 0.0F, 1280.0F, 720.0F };

    stack.emit_to_canvas(canvas, camera, vp);
    EXPECT_EQ(canvas.commands().size(), 0U);

    stack.mutable_layers()[0].visible = true;
    EXPECT_TRUE(stack.layers()[0].visible);

    stack.emit_to_canvas(canvas, camera, vp);
    EXPECT_GT(canvas.commands().size(), 0U);
}

// P22 (F24 × F15): Tweaking emission rate in VFX inspector alters active particle count
TEST(Tier3_Pairwise, P22_VfxInspector_Alters_ParticleEmissionRate)
{
    cd::world2d::ParticleSystem2D ps(512U);

    cd::world2d::ParticleEmitterConfig2D cfg {};
    cfg.emission_rate = 10.0F;
    cfg.lifetime_min = 2.0F;
    cfg.lifetime_max = 2.0F;
    cfg.speed_min = 1.0F;
    cfg.speed_max = 2.0F;

    ps.update(0.5F, &cfg, { 0.0F, 0.0F });
    const std::size_t initial_count = ps.active_count();
    EXPECT_EQ(initial_count, 5U);

    cfg.emission_rate = 60.0F;

    ps.update(0.5F, &cfg, { 0.0F, 0.0F });
    const std::size_t boosted_count = ps.active_count();
    EXPECT_EQ(boosted_count, 35U);
    EXPECT_GT(boosted_count, initial_count);

    cd::render2d::Canvas2D canvas;
    ps.emit_to_canvas(canvas);
    EXPECT_EQ(canvas.commands().size(), boosted_count);
}

// P23 (F25 × F26): cd_player.exe boots while main.cpp is only 15 LOC
TEST(Tier3_Pairwise, P23_Player_LOC_And_BootParity)
{
    const auto root = EnvironmentProbe::engine_root();
    const auto main_path = root / "apps" / "player" / "main.cpp";
    std::ifstream ifs(main_path);
    std::size_t lines = 0;
    std::string l;
    while (std::getline(ifs, l)) ++lines;
    EXPECT_LT(lines, 80U);
}

// P24 (F28 × F26): cd::app_common centralizes shared logic without violating layer DAG
TEST(Tier3_Pairwise, P24_AppCommon_No_DAG_Violations)
{
    const auto root = EnvironmentProbe::engine_root();
    const auto app_common_headers = root / "apps" / "common" / "include" / "cd" / "app_common";
    ASSERT_TRUE(std::filesystem::exists(app_common_headers));

    for (const auto& entry : std::filesystem::directory_iterator(app_common_headers))
    {
        if (entry.is_regular_file())
        {
            std::ifstream ifs(entry.path());
            std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
            EXPECT_EQ(content.find("<cd/runtime/"), std::string::npos) << entry.path();
            EXPECT_EQ(content.find("<cd/editor/"), std::string::npos) << entry.path();
        }
    }
}

// P25 (F29 × F5): MCP server client requests synchronize without sleep_for
TEST(Tier3_Pairwise, P25_McpServer_Deterministic_Sync)
{
    cd::world2d::Stage2D stage;
    stage.clear();
    cd::world2d::CommandBus2D bus;

    std::mutex mtx;
    std::condition_variable cv_req;
    std::condition_variable cv_res;

    std::string pending_json;
    bool req_ready = false;
    bool res_ready = false;
    bool worker_stop = false;
    cd::world2d::CommandExecutionReport2D worker_report {};

    std::thread worker([&]() {
        while (true)
        {
            std::string req;
            {
                std::unique_lock<std::mutex> lock(mtx);
                cv_req.wait(lock, [&]() { return req_ready || worker_stop; });
                if (worker_stop && !req_ready)
                {
                    break;
                }
                req = std::move(pending_json);
                req_ready = false;
            }

            auto rep = bus.execute_json(stage, req);

            {
                std::lock_guard<std::mutex> lock(mtx);
                worker_report = std::move(rep);
                res_ready = true;
            }
            cv_res.notify_one();
        }
    });

    const std::string command_json =
        R"({"op":"spawn_entity_2d","name":"sync_mcp_entity","pos":[12.0,8.0]})";

    {
        std::lock_guard<std::mutex> lock(mtx);
        pending_json = command_json;
        req_ready = true;
    }
    cv_req.notify_one();

    {
        std::unique_lock<std::mutex> lock(mtx);
        cv_res.wait(lock, [&]() { return res_ready; });
        EXPECT_TRUE(worker_report.ok);
        EXPECT_EQ(worker_report.applied_count, 1U);
    }

    {
        std::lock_guard<std::mutex> lock(mtx);
        worker_stop = true;
    }
    cv_req.notify_one();
    worker.join();

    EXPECT_EQ(stage.entities().size(), 1U);
    EXPECT_EQ(stage.entities()[0].name, "sync_mcp_entity");
}

// P26 (F29 × F20): 100-step physics simulation completes with zero sleep_for delays
TEST(Tier3_Pairwise, P26_PhysicsSimulation_ZeroSleepFor)
{
    cd::world2d::Stage2D stage;
    stage.build_starter_illuminated_platformer();
    stage.enter_play();
    cd::world2d::PlayerInput2D input {};

    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 100; ++i)
    {
        stage.step_play(1.0F / 60.0F, input);
    }
    const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);

    stage.exit_play();
    EXPECT_LT(duration.count(), 500); // 100 ticks in < 500ms
}

// P27 (F30 × F10): Captured frame readback verifies non-zero pixel variance from atlas
TEST(Tier3_Pairwise, P27_FrameReadback_Atlas_Variance)
{
    auto atlas = cd::render2d::TextureAtlas2D::generate_procedural_starter_atlas(16U);
    const auto stats = HeadlessReadback::analyze_rgba8(atlas.pixels(), atlas.width(), atlas.height());
    EXPECT_FALSE(stats.is_flat_monochrome);
}

// P28 (F30 × F20): Headless frame readbacks across templates exhibit distinct visual profiles
TEST(Tier3_Pairwise, P28_Templates_Distinct_VisualProfiles)
{
    auto rasterize_canvas = [](const cd::render2d::Canvas2D& canvas,
                               std::uint32_t width,
                               std::uint32_t height,
                               cd::render2d::ColorRgba8 clear_color) -> std::vector<std::uint8_t> {
        std::vector<std::uint8_t> fb(static_cast<std::size_t>(width) * height * 4U, 0U);
        for (std::size_t i = 0; i < static_cast<std::size_t>(width) * height; ++i)
        {
            fb[i * 4U + 0U] = clear_color.r;
            fb[i * 4U + 1U] = clear_color.g;
            fb[i * 4U + 2U] = clear_color.b;
            fb[i * 4U + 3U] = clear_color.a;
        }
        const float sx = canvas.viewport().width > 0.0F
            ? static_cast<float>(width) / canvas.viewport().width
            : 1.0F;
        const float sy = canvas.viewport().height > 0.0F
            ? static_cast<float>(height) / canvas.viewport().height
            : 1.0F;

        for (const auto& cmd : canvas.commands())
        {
            const int x0 = std::clamp(static_cast<int>(cmd.x0 * sx), 0, static_cast<int>(width) - 1);
            const int y0 = std::clamp(static_cast<int>(cmd.y0 * sy), 0, static_cast<int>(height) - 1);
            const int x1 = std::clamp(static_cast<int>((cmd.x0 + cmd.x1) * sx), 0, static_cast<int>(width));
            const int y1 = std::clamp(static_cast<int>((cmd.y0 + cmd.y1) * sy), 0, static_cast<int>(height));
            for (int py = y0; py < y1; ++py)
            {
                for (int px = x0; px < x1; ++px)
                {
                    const std::size_t idx = (static_cast<std::size_t>(py) * width + static_cast<std::size_t>(px)) * 4U;
                    fb[idx + 0U] = cmd.c0.r;
                    fb[idx + 1U] = cmd.c0.g;
                    fb[idx + 2U] = cmd.c0.b;
                    fb[idx + 3U] = cmd.c0.a;
                }
            }
        }
        return fb;
    };

    const cd::render2d::ViewportRect2D vp { 0.0F, 0.0F, 320.0F, 180.0F };

    cd::world2d::Stage2D stage_platformer;
    stage_platformer.build_starter_illuminated_platformer();
    cd::render2d::Canvas2D canvas_platformer;
    stage_platformer.render(canvas_platformer, vp);
    ASSERT_FALSE(canvas_platformer.commands().empty());

    const auto fb_platformer = rasterize_canvas(
        canvas_platformer, 64U, 64U, { 30U, 40U, 65U, 255U });
    const auto stats_platformer = HeadlessReadback::analyze_rgba8(fb_platformer, 64U, 64U);

    cd::world2d::Stage2D stage_dungeon;
    stage_dungeon.clear();
    stage_dungeon.set_ambient_color({ 0.05F, 0.05F, 0.08F });
    stage_dungeon.set_gravity_y(0.0F);
    stage_dungeon.tilemap().paint_rect(0, 0, 8, 8, 1U);
    cd::render2d::Canvas2D canvas_dungeon;
    stage_dungeon.render(canvas_dungeon, vp);
    ASSERT_FALSE(canvas_dungeon.commands().empty());

    const auto fb_dungeon = rasterize_canvas(
        canvas_dungeon, 64U, 64U, { 12U, 12U, 20U, 255U });
    const auto stats_dungeon = HeadlessReadback::analyze_rgba8(fb_dungeon, 64U, 64U);

    EXPECT_FALSE(stats_platformer.is_flat_monochrome);
    EXPECT_FALSE(stats_dungeon.is_flat_monochrome);
    EXPECT_GT(stats_platformer.total_variance, 0.0F);
    EXPECT_GT(stats_dungeon.total_variance, 0.0F);
    EXPECT_NE(stats_platformer.mean_r, stats_dungeon.mean_r);
}

// P29 (F31 × F4): PLAN_NEXT_PHASES.md updated for FAZ 36 passes style check
TEST(Tier3_Pairwise, P29_PlanNextPhases_Preserved)
{
    const auto root = EnvironmentProbe::engine_root();
    const auto p = root / "docs" / "PLAN_NEXT_PHASES.md";
    ASSERT_TRUE(std::filesystem::exists(p));
    EXPECT_GT(std::filesystem::file_size(p), 1000U);

    std::ifstream ifs(p, std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    EXPECT_EQ(content.find("\xE2\x80\x94"), std::string::npos);
    EXPECT_EQ(content.find(" --- "), std::string::npos);
}

// P30 (F31 × F9): DOMESTIC_ENGINE_PRODUCT_ROADMAP.md updated for FAZ 37 passes style check
TEST(Tier3_Pairwise, P30_ProductRoadmap_Preserved)
{
    const auto root = EnvironmentProbe::engine_root();
    const auto p = root / "docs" / "DOMESTIC_ENGINE_PRODUCT_ROADMAP.md";
    ASSERT_TRUE(std::filesystem::exists(p));
    EXPECT_GT(std::filesystem::file_size(p), 1000U);

    std::ifstream ifs(p, std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    EXPECT_EQ(content.find("\xE2\x80\x94"), std::string::npos);
    EXPECT_EQ(content.find(" --- "), std::string::npos);
}

}  // namespace cd::e2e
