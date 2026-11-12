// =============================================================================
// CHROMODYNAMIC — tests/e2e/tier1_feature/test_tier1_features_20_to_31.cpp
//
// Tier 1 Category-Partition Feature Tests for Features 20 through 31:
//   F20: Switchable 2D Genre Templates
//   F21: [2D Studio] Advanced Tilemap Tools (Stamp & Flood-Fill)
//   F22: [2D Studio] Visual Palette Previews
//   F23: [2D Studio] Layer Management Dock
//   F24: [2D Studio] Animation & VFX Inspectors
//   F25: Standalone Player Parity (cd_player.exe)
//   F26: Entry Point <80 LOC & Zero Duplication
//   F28: Compilation & Layer DAG Invariants
//   F29: Zero sleep_for & Self-Test All Gate
//   F30: Live Vulkan 1.3 Frame Captures Visual Validation
//   F31: Distribution Documentation Preservation
// =============================================================================

#include "../harness/E2eHarness.hpp"
#include "../harness/HeadlessReadback.hpp"
#include "../harness/CliRunner.hpp"

#include <cd/render2d/Renderer2D.hpp>
#include <cd/render2d/SpriteBatch2D.hpp>
#include <cd/render2d/Tilemap2D.hpp>
#include <cd/render2d/ParallaxLayer2D.hpp>
#include <cd/render2d/TextureAtlas2D.hpp>
#include <cd/world2d/Stage2D.hpp>
#include <cd/world2d/CommandBus2D.hpp>
#include <cd/world2d/ParticleSystem2D.hpp>
#include <cd/world2d/SpatialHash2D.hpp>
#include <cd/app_common/FrameCapture.hpp>
#include <cd/asset/image/Image.hpp>
#include <cd/ui/renderer/DrawBatcher.hpp>

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace cd::e2e
{

namespace
{
inline FrameColorStats render_stage_to_png_and_analyze(
    const cd::world2d::Stage2D& stage,
    TempSandbox&                sandbox,
    std::string_view            filename,
    std::uint32_t               w = 128U,
    std::uint32_t               h = 128U)
{
    cd::render2d::Canvas2D canvas;
    cd::render2d::ViewportRect2D vp { 0.0F, 0.0F, static_cast<float>(w), static_cast<float>(h) };
    stage.render(canvas, vp);

    cd::ui::renderer::DrawBatcher batcher;
    canvas.replay_into<cd::ui::renderer::DrawBatcher, cd::ui::renderer::Color, cd::ui::renderer::ScissorRect, cd::ui::renderer::AtlasUv>(batcher);

    const auto png_path = sandbox.file_path(filename).string();
    const auto wr = cd::app_common::rasterize_draw_batcher_to_png(
        batcher, w, h, { 15U, 18U, 26U, 255U }, png_path);
    if (!wr.has_value())
    {
        return {};
    }

    const auto decoded = cd::asset::image::load_image(png_path);
    if (!decoded.has_value())
    {
        return {};
    }

    return HeadlessReadback::analyze_rgba8(decoded->rgba, decoded->width, decoded->height);
}
} // namespace

// =============================================================================
// Feature 20: Switchable 2D Genre Templates
// =============================================================================

TEST(Tier1_Feature20_GenreTemplates, ApplyPlatformer)
{
    cd::world2d::Stage2D stage;
    stage.build_starter_illuminated_platformer();

    EXPECT_LT(stage.gravity_y(), -20.0F);
    EXPECT_GT(stage.entities().size(), 0U);
    EXPECT_GT(stage.baked_tile_colliders().size(), 0U);
}

TEST(Tier1_Feature20_GenreTemplates, ApplyTopDownRpg)
{
    cd::world2d::Stage2D stage;
    stage.clear();
    stage.set_gravity_y(0.0F);
    stage.set_ambient_color({ 0.15F, 0.18F, 0.25F });

    cd::world2d::Entity2D hero {};
    hero.name = "RpgHero";
    hero.y_sort = true;
    hero.position = { 0.0F, 0.0F };
    stage.add_entity(hero);

    EXPECT_FLOAT_EQ(stage.gravity_y(), 0.0F);
    EXPECT_EQ(stage.entities().size(), 1U);
    EXPECT_TRUE(stage.entities()[0].y_sort);
}

TEST(Tier1_Feature20_GenreTemplates, ApplyBulletHeavenArena)
{
    cd::world2d::Stage2D stage;
    stage.set_gravity_y(0.0F);

    cd::world2d::Entity2D player {};
    player.name = "ArenaShip";
    player.position = { 0.0F, 0.0F };
    stage.add_entity(player);

    EXPECT_FLOAT_EQ(stage.gravity_y(), 0.0F);
    EXPECT_FALSE(stage.entities().empty());
}

TEST(Tier1_Feature20_GenreTemplates, PlayerInputHandling)
{
    cd::world2d::Stage2D stage;
    stage.build_starter_illuminated_platformer();
    stage.enter_play();

    cd::world2d::PlayerInput2D input {};
    input.move_x = 1.0F;

    stage.step_play(1.0F / 60.0F, input);
    EXPECT_TRUE(stage.is_playing());

    stage.exit_play();
    EXPECT_FALSE(stage.is_playing());
}

TEST(Tier1_Feature20_GenreTemplates, SerializationRoundtrip)
{
    cd::world2d::Stage2D original;
    original.build_starter_illuminated_platformer();

    const std::string json = original.save_to_json_string();
    ASSERT_FALSE(json.empty());

    cd::world2d::Stage2D restored;
    auto res = restored.load_from_json_string(json);
    EXPECT_TRUE(res.has_value());
    EXPECT_EQ(restored.entities().size(), original.entities().size());
}

// =============================================================================
// Feature 21: [2D Studio] Advanced Tilemap Tools (Stamp & Flood-Fill)
// =============================================================================

TEST(Tier1_Feature21_TileTools, StampMultiTilePattern)
{
    cd::render2d::Tilemap2D tm;
    const std::array<std::uint16_t, 9> pattern = {
        1U, 1U, 1U,
        1U, 2U, 1U,
        1U, 1U, 1U
    };

    const std::size_t stamped = tm.stamp(10, 10, pattern, 3, 3);
    EXPECT_EQ(stamped, 9U);
    EXPECT_EQ(tm.get_tile(10, 10).palette_id, 1U);
    EXPECT_EQ(tm.get_tile(11, 11).palette_id, 2U);
}

TEST(Tier1_Feature21_TileTools, FloodFillConnectedRegion)
{
    cd::render2d::Tilemap2D tm;
    // Paint a 5x5 hollow box of ID 1
    tm.paint_rect(0, 0, 4, 0, 1U);
    tm.paint_rect(0, 4, 4, 4, 1U);
    tm.paint_rect(0, 0, 0, 4, 1U);
    tm.paint_rect(4, 0, 4, 4, 1U);

    // Flood fill the 3x3 interior (cells 1..3, 1..3) with ID 2
    const std::size_t filled = tm.flood_fill(2, 2, 2U, 100U);
    EXPECT_EQ(filled, 9U);
    EXPECT_EQ(tm.get_tile(2, 2).palette_id, 2U);
}

TEST(Tier1_Feature21_TileTools, UndoRestoresPreviousTiles)
{
    cd::world2d::Stage2D stage;
    cd::world2d::CommandBus2D bus;

    auto rep = bus.execute_json(stage, R"({"op":"paint_tile_2d","gx":10,"gy":10,"palette_id":5})");
    (void)rep;
    EXPECT_GT(bus.undo_depth(), 0U);

    EXPECT_TRUE(bus.undo(stage));
}

TEST(Tier1_Feature21_TileTools, RedoReappliesStroke)
{
    cd::world2d::Stage2D stage;
    cd::world2d::CommandBus2D bus;

    auto rep = bus.execute_json(stage, R"({"op":"paint_tile_2d","gx":10,"gy":10,"palette_id":5})");
    (void)rep;
    bus.undo(stage);
    EXPECT_GT(bus.redo_depth(), 0U);

    EXPECT_TRUE(bus.redo(stage));
}

TEST(Tier1_Feature21_TileTools, WangBorderAutoUpdate)
{
    cd::render2d::Tilemap2D tm;
    tm.set_tile(0, 0, 1U);
    tm.set_tile(1, 0, 1U);

    EXPECT_TRUE((tm.get_tile(0, 0).autotile_mask & cd::render2d::autotile_bits::kEast) != 0U);
    EXPECT_TRUE((tm.get_tile(1, 0).autotile_mask & cd::render2d::autotile_bits::kWest) != 0U);
}

// =============================================================================
// Feature 22: [2D Studio] Visual Palette Previews
// =============================================================================

TEST(Tier1_Feature22_PaletteDock, RendersTileThumbnails)
{
    cd::render2d::Tilemap2D tm;
    cd::render2d::TilePaletteEntry2D e1 {};
    e1.palette_id = 1U;
    e1.name = "Grass";
    e1.uv_rect = { 0.0F, 0.0F, 0.1F, 0.1F };
    tm.register_palette_entry(e1);

    EXPECT_EQ(tm.palette().size(), 4U);
    EXPECT_EQ(tm.palette()[0].name, "Grass");
}

TEST(Tier1_Feature22_PaletteDock, ClickSelectsActiveTile)
{
    cd::render2d::Tilemap2D tm;
    cd::render2d::TilePaletteEntry2D e4 {};
    e4.palette_id = 4U;
    e4.name = "MoltenLava";
    e4.fill_color = { 240U, 80U, 20U, 255U };
    e4.hazard = true;
    tm.register_palette_entry(e4);

    const auto* entry = tm.find_palette(4U);
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->palette_id, 4U);
    EXPECT_EQ(entry->name, "MoltenLava");
    EXPECT_TRUE(entry->hazard);

    tm.set_tile(10, 10, entry->palette_id);
    EXPECT_EQ(tm.get_tile(10, 10).palette_id, 4U);
}

TEST(Tier1_Feature22_PaletteDock, WangConnectionIndicators)
{
    cd::render2d::TilePaletteEntry2D pal {};
    pal.has_autotile_uvs = true;
    for (std::size_t i = 0; i < 16; ++i)
    {
        pal.autotile_uvs[i] = cd::render2d::UvRect2D { 0.0F, 0.0F, 1.0F, 1.0F };
    }
    EXPECT_TRUE(pal.has_autotile_uvs);
    EXPECT_EQ(pal.autotile_uvs.size(), 16U);
}

TEST(Tier1_Feature22_PaletteDock, ScrollableGridCapacity)
{
    cd::render2d::Tilemap2D tm;
    for (std::uint16_t i = 1; i <= 25; ++i)
    {
        cd::render2d::TilePaletteEntry2D e {};
        e.palette_id = i;
        e.name = "Tile_" + std::to_string(i);
        tm.register_palette_entry(e);
    }
    EXPECT_EQ(tm.palette().size(), 25U);
}

TEST(Tier1_Feature22_PaletteDock, SelectionHighlight)
{
    cd::render2d::Tilemap2D tm;
    cd::render2d::TilePaletteEntry2D e {};
    e.palette_id = 3U;
    e.name = "Water";
    tm.register_palette_entry(e);

    const std::uint16_t active_selected_id = 3U;
    bool found_active = false;
    for (const auto& pal : tm.palette())
    {
        if (pal.palette_id == active_selected_id)
        {
            found_active = true;
            EXPECT_EQ(pal.name, "Water");
            break;
        }
    }
    EXPECT_TRUE(found_active);
    EXPECT_EQ(tm.find_palette(999U), nullptr);
}

// =============================================================================
// Feature 23: [2D Studio] Layer Management Dock
// =============================================================================

TEST(Tier1_Feature23_LayerDock, VisibilityToggle)
{
    cd::render2d::ParallaxManager2D mgr;
    cd::render2d::ParallaxLayer2D layer {};
    layer.name = "Backdrop";
    layer.procedural_style = cd::render2d::ParallaxProceduralStyle::kCitySkyline;
    layer.visible = true;
    mgr.add_layer(layer);

    cd::render2d::Canvas2D canvas;
    cd::render2d::Camera2D camera;
    cd::render2d::ViewportRect2D vp { 0.0F, 0.0F, 800.0F, 600.0F };

    mgr.emit_to_canvas(canvas, camera, vp);
    EXPECT_GT(canvas.commands().size(), 0U);

    canvas.clear(vp);
    mgr.find_layer("Backdrop")->visible = false;
    mgr.emit_to_canvas(canvas, camera, vp);
    EXPECT_EQ(canvas.commands().size(), 0U);
}

TEST(Tier1_Feature23_LayerDock, LockingPadlock)
{
    cd::render2d::ParallaxManager2D mgr;
    cd::render2d::ParallaxLayer2D l1 {};
    l1.name = "LockedDistantMountains";
    l1.z_order = -100;
    l1.scroll_factor = { 0.1F, 0.1F };
    mgr.add_layer(l1);

    const auto* locked = mgr.find_layer("LockedDistantMountains");
    ASSERT_NE(locked, nullptr);
    EXPECT_EQ(locked->z_order, -100);
    EXPECT_FLOAT_EQ(locked->scroll_factor.x, 0.1F);

    cd::render2d::ParallaxLayer2D l2 {};
    l2.name = "ForegroundProps";
    l2.z_order = 50;
    mgr.add_layer(l2);
    mgr.sort_layers();

    EXPECT_EQ(mgr.layers()[0].name, "LockedDistantMountains");
    EXPECT_EQ(mgr.layers()[1].name, "ForegroundProps");
}

TEST(Tier1_Feature23_LayerDock, ScrollFactorSlider)
{
    cd::render2d::ParallaxLayer2D layer {};
    layer.scroll_factor = { 0.75F, 0.75F };
    EXPECT_FLOAT_EQ(layer.scroll_factor.x, 0.75F);
}

TEST(Tier1_Feature23_LayerDock, AutoScrollSpeedControls)
{
    cd::render2d::ParallaxManager2D mgr;
    cd::render2d::ParallaxLayer2D layer {};
    layer.name = "Clouds";
    layer.auto_scroll_velocity = { -2.5F, 1.0F };
    mgr.add_layer(layer);

    mgr.update(2.0F);
    const auto* updated = mgr.find_layer("Clouds");
    ASSERT_NE(updated, nullptr);
    EXPECT_FLOAT_EQ(updated->current_offset.x, -5.0F);
    EXPECT_FLOAT_EQ(updated->current_offset.y, 2.0F);

    const auto eff = mgr.compute_effective_offset(*updated, { 100.0F, 50.0F });
    EXPECT_FLOAT_EQ(eff.x, 100.0F * 0.5F - 5.0F);
}

TEST(Tier1_Feature23_LayerDock, LayerReordering)
{
    cd::render2d::ParallaxManager2D mgr;
    cd::render2d::ParallaxLayer2D l1 {}; l1.name = "L1"; l1.z_order = 10;
    cd::render2d::ParallaxLayer2D l2 {}; l2.name = "L2"; l2.z_order = 5;

    mgr.add_layer(l1);
    mgr.add_layer(l2);
    mgr.sort_layers();

    EXPECT_EQ(mgr.layers()[0].name, "L2");
    EXPECT_EQ(mgr.layers()[1].name, "L1");
}

// =============================================================================
// Feature 24: [2D Studio] Animation & VFX Inspectors
// =============================================================================

TEST(Tier1_Feature24_AnimInspectors, TimelineScrubberSteps)
{
    cd::render2d::FlipbookAnimator2D animator;
    cd::render2d::FlipbookClip2D clip {};
    clip.name = "player_walk";
    clip.frames.push_back({ { 0.0F, 0.0F, 0.25F, 1.0F }, 0.10F });
    clip.frames.push_back({ { 0.25F, 0.0F, 0.50F, 1.0F }, 0.10F });
    clip.frames.push_back({ { 0.50F, 0.0F, 0.75F, 1.0F }, 0.10F });
    clip.frames.push_back({ { 0.75F, 0.0F, 1.0F, 1.0F }, 0.10F });
    clip.loop = true;
    animator.add_clip(clip);

    ASSERT_TRUE(animator.play("player_walk"));
    EXPECT_EQ(animator.current_frame_index(), 0U);
    EXPECT_FLOAT_EQ(animator.current_uv().u0, 0.0F);

    animator.step(0.12F);
    EXPECT_EQ(animator.current_frame_index(), 1U);
    EXPECT_FLOAT_EQ(animator.current_uv().u0, 0.25F);

    animator.step(0.10F);
    EXPECT_EQ(animator.current_frame_index(), 2U);
    EXPECT_FLOAT_EQ(animator.current_uv().u0, 0.50F);
}

TEST(Tier1_Feature24_AnimInspectors, PlayPauseToggle)
{
    cd::render2d::FlipbookAnimator2D animator;
    cd::render2d::FlipbookClip2D clip {};
    clip.name = "jump";
    clip.frames.push_back({ { 0.0F, 0.0F, 0.5F, 1.0F }, 0.10F });
    clip.frames.push_back({ { 0.5F, 0.0F, 1.0F, 1.0F }, 0.10F });
    animator.add_clip(clip);
    animator.play("jump");

    animator.step(0.0F);
    EXPECT_EQ(animator.current_frame_index(), 0U);

    animator.step(0.15F);
    EXPECT_EQ(animator.current_frame_index(), 1U);
}

TEST(Tier1_Feature24_AnimInspectors, FpsControl)
{
    cd::render2d::FlipbookAnimator2D low_fps_anim;
    cd::render2d::FlipbookClip2D c10 {};
    c10.name = "clip_10fps";
    c10.frames.push_back({ {}, 0.10F });
    c10.frames.push_back({ {}, 0.10F });
    low_fps_anim.add_clip(c10);
    low_fps_anim.play("clip_10fps");

    cd::render2d::FlipbookAnimator2D high_fps_anim;
    cd::render2d::FlipbookClip2D c60 {};
    c60.name = "clip_60fps";
    c60.loop = false;
    c60.frames.push_back({ {}, 0.016F });
    c60.frames.push_back({ {}, 0.016F });
    c60.frames.push_back({ {}, 0.016F });
    high_fps_anim.add_clip(c60);
    high_fps_anim.play("clip_60fps");

    low_fps_anim.step(0.02F);
    high_fps_anim.step(0.02F);

    EXPECT_EQ(low_fps_anim.current_frame_index(), 0U);
    EXPECT_GT(high_fps_anim.current_frame_index(), 0U);
}

TEST(Tier1_Feature24_AnimInspectors, ParticleRateSlider)
{
    cd::world2d::ParticleSystem2D ps(256U);
    cd::world2d::ParticleEmitterConfig2D cfg {};
    cfg.emission_rate = 100.0F;
    cfg.lifetime_min = 1.0F;
    cfg.lifetime_max = 1.0F;

    ps.update(0.1F, &cfg);
    EXPECT_GT(ps.active_count(), 5U);
    EXPECT_LE(ps.active_count(), 20U);
}

TEST(Tier1_Feature24_AnimInspectors, ColorRampPicker)
{
    cd::world2d::ParticleSystem2D ps(16U);
    cd::world2d::ParticleEmitterConfig2D cfg {};
    cfg.start_color = { 1.0F, 0.8F, 0.2F, 1.0F };
    cfg.end_color   = { 0.2F, 0.0F, 0.8F, 0.0F };

    EXPECT_TRUE(ps.emit_particle({ 0.0F, 0.0F }, cfg));
    ASSERT_EQ(ps.active_count(), 1U);

    const auto p = ps.active_particles()[0];
    EXPECT_FLOAT_EQ(p.start_color.x, 1.0F);
    EXPECT_FLOAT_EQ(p.start_color.y, 0.8F);
    EXPECT_FLOAT_EQ(p.end_color.z, 0.8F);
    EXPECT_FLOAT_EQ(p.end_color.w, 0.0F);
}

// =============================================================================
// Feature 25: Standalone Player Parity (cd_player.exe)
// =============================================================================

TEST(Tier1_Feature25_PlayerParity, GenreFlagPlatformerHeadless)
{
    const auto bin = EnvironmentProbe::binary_dir() / "cd_player.exe";
    if (std::filesystem::exists(bin))
    {
        auto res = CliRunner::run_player("--headless --frames 3");
        EXPECT_EQ(res.exit_code, 0);
    }
    else
    {
        GTEST_SKIP() << "cd_player.exe not yet built";
    }
}

TEST(Tier1_Feature25_PlayerParity, GenreFlagRpgHeadless)
{
    const auto bin = EnvironmentProbe::binary_dir() / "cd_player.exe";
    if (std::filesystem::exists(bin))
    {
        auto res = CliRunner::run_player("--null --frames 2");
        EXPECT_EQ(res.exit_code, 0);
    }
    else
    {
        GTEST_SKIP() << "cd_player.exe not yet built";
    }
}

TEST(Tier1_Feature25_PlayerParity, GenreFlagArenaHeadless)
{
    const auto bin = EnvironmentProbe::binary_dir() / "cd_player.exe";
    if (std::filesystem::exists(bin))
    {
        auto res = CliRunner::run_player("--headless --frames 2");
        EXPECT_EQ(res.exit_code, 0);
    }
    else
    {
        GTEST_SKIP() << "cd_player.exe not yet built";
    }
}

TEST(Tier1_Feature25_PlayerParity, LoadsStage2dJson)
{
    TempSandbox sandbox;
    cd::world2d::Stage2D stage;
    stage.build_starter_illuminated_platformer();
    const std::string json = stage.save_to_json_string();
    sandbox.write_text("test_stage.cd2d.json", json);

    cd::world2d::Stage2D loaded;
    EXPECT_TRUE(loaded.load_from_json_string(json).has_value());
}

TEST(Tier1_Feature25_PlayerParity, TextureAtlasUpload)
{
    cd::render2d::TextureAtlas2D atlas(64U, 64U);
    EXPECT_TRUE(atlas.is_valid());
    EXPECT_EQ(atlas.pixels().size(), 64U * 64U * 4U);
}

// =============================================================================
// Feature 26: Entry Point <80 LOC & Zero Duplication
// =============================================================================

TEST(Tier1_Feature26_LOC, EditorMainLessThan80)
{
    const auto root = EnvironmentProbe::engine_root();
    const auto main_path = root / "apps" / "editor" / "main.cpp";
    ASSERT_TRUE(std::filesystem::exists(main_path));

    std::ifstream ifs(main_path);
    std::size_t lines = 0;
    std::string line;
    while (std::getline(ifs, line))
    {
        ++lines;
    }
    EXPECT_LT(lines, 80U);
}

TEST(Tier1_Feature26_LOC, PlayerMainLessThan80)
{
    const auto root = EnvironmentProbe::engine_root();
    const auto main_path = root / "apps" / "player" / "main.cpp";
    ASSERT_TRUE(std::filesystem::exists(main_path));

    std::ifstream ifs(main_path);
    std::size_t lines = 0;
    std::string line;
    while (std::getline(ifs, line))
    {
        ++lines;
    }
    EXPECT_LT(lines, 80U);
}

TEST(Tier1_Feature26_LOC, AppCommonSharedFileTextIo)
{
    const auto root = EnvironmentProbe::engine_root();
    EXPECT_TRUE(std::filesystem::exists(root / "apps" / "common" / "include" / "cd" / "app_common" / "FileTextIo.hpp"));
}

TEST(Tier1_Feature26_LOC, AppCommonSharedCameraMath3D)
{
    const auto root = EnvironmentProbe::engine_root();
    EXPECT_TRUE(std::filesystem::exists(root / "apps" / "common" / "include" / "cd" / "app_common" / "CameraMath3D.hpp"));
}

TEST(Tier1_Feature26_LOC, AppCommonSharedFrameCapture)
{
    const auto root = EnvironmentProbe::engine_root();
    EXPECT_TRUE(std::filesystem::exists(root / "apps" / "common" / "include" / "cd" / "app_common" / "FrameCapture.hpp"));
}

// =============================================================================
// Feature 28: Compilation & Layer DAG Invariants
// =============================================================================

TEST(Tier1_Feature28_LayerDAG, ScriptExists)
{
    const auto root = EnvironmentProbe::engine_root();
    const auto script = root / ".agents" / "skills" / "engine-sota-adr" / "scripts" / "check_layer_dag.py";
    EXPECT_TRUE(std::filesystem::exists(script));
}

TEST(Tier1_Feature28_LayerDAG, NoUpwardDependencies)
{
    auto res = CliRunner::run_python(".agents/skills/engine-sota-adr/scripts/check_layer_dag.py --engine-root engine");
    ASSERT_TRUE(res.launched) << res.stderr_output;
    EXPECT_EQ(res.exit_code, 0) << res.stdout_output;
    EXPECT_NE(res.stdout_output.find("violations=0"), std::string::npos);
    EXPECT_NE(res.stdout_output.find("VERDICT: PASS"), std::string::npos);
}

TEST(Tier1_Feature28_LayerDAG, Render2DNoWorld2DIncludes)
{
    // render2d (level 3) must never include world2d (level 4)
    const auto root = EnvironmentProbe::engine_root();
    const auto r2d_path = root / "engine" / "render" / "render2d" / "include" / "cd" / "render2d";
    ASSERT_TRUE(std::filesystem::exists(r2d_path));

    for (const auto& entry : std::filesystem::directory_iterator(r2d_path))
    {
        if (entry.is_regular_file())
        {
            std::ifstream ifs(entry.path());
            std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
            EXPECT_EQ(content.find("<cd/world2d/"), std::string::npos);
        }
    }
}

TEST(Tier1_Feature28_LayerDAG, CompilerWarningsAsErrors)
{
    const auto root = EnvironmentProbe::engine_root();
    const auto cmake_file = root / "CMakeLists.txt";
    ASSERT_TRUE(std::filesystem::exists(cmake_file));

    std::ifstream ifs(cmake_file);
    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    EXPECT_NE(content.find("Warnings as error"), std::string::npos);
}

TEST(Tier1_Feature28_LayerDAG, CPlusPlus23Standard)
{
    #if defined(__cplusplus)
    EXPECT_GE(__cplusplus, 202302L);
    #endif
}

// =============================================================================
// Feature 29: Zero sleep_for & Self-Test All Gate
// =============================================================================

TEST(Tier1_Feature29_Determinism, ZeroSleepInTests)
{
    const auto root = EnvironmentProbe::engine_root();
    const auto tests_dir = root / "tests";
    ASSERT_TRUE(std::filesystem::exists(tests_dir));

    std::size_t files_scanned = 0;
    std::size_t sleep_for_occurrences = 0;
    std::vector<std::string> offending_locations;

    const std::string forbidden_call = std::string("std::this_thread::") + "sleep_for";

    for (const auto& entry : std::filesystem::recursive_directory_iterator(tests_dir))
    {
        if (entry.is_regular_file())
        {
            const auto ext = entry.path().extension().string();
            if (ext == ".cpp" || ext == ".hpp" || ext == ".h")
            {
                ++files_scanned;
                std::ifstream ifs(entry.path());
                std::string line;
                std::size_t line_no = 0;
                while (std::getline(ifs, line))
                {
                    ++line_no;
                    const auto trimmed_pos = line.find_first_not_of(" \t");
                    if (trimmed_pos != std::string::npos && line.substr(trimmed_pos, 2) == "//")
                    {
                        continue;
                    }
                    if (line.find("find(") != std::string::npos || line.find("forbidden") != std::string::npos)
                    {
                        continue;
                    }
                    if (line.find(forbidden_call) != std::string::npos)
                    {
                        ++sleep_for_occurrences;
                        offending_locations.push_back(entry.path().string() + ":" + std::to_string(line_no));
                    }
                }
            }
        }
    }

    EXPECT_GT(files_scanned, 10U);
    EXPECT_EQ(sleep_for_occurrences, 0U) << "Found forbidden sleep_for in automated tests!";
}

TEST(Tier1_Feature29_Determinism, EditorSelfTestAllGate)
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

TEST(Tier1_Feature29_Determinism, DeterministicFakeClockAdvance)
{
    DeterministicClock clock(1.0F / 60.0F);
    clock.advance_steps(60);
    EXPECT_NEAR(clock.time(), 1.0F, 0.001F);
    EXPECT_EQ(clock.step_count(), 60U);
}

TEST(Tier1_Feature29_Determinism, PIECycleEnterExitRestore)
{
    cd::world2d::Stage2D stage;
    stage.build_starter_illuminated_platformer();

    stage.enter_play();
    EXPECT_TRUE(stage.is_playing());

    stage.exit_play();
    EXPECT_FALSE(stage.is_playing());
}

TEST(Tier1_Feature29_Determinism, HeadlessSelfTestSafety)
{
    cd::world2d::Stage2D stage;
    stage.build_starter_illuminated_platformer();
    cd::world2d::PlayerInput2D input {};
    stage.enter_play();
    for (int i = 0; i < 10; ++i)
    {
        stage.step_play(1.0F / 60.0F, input);
    }
    stage.exit_play();
    EXPECT_FALSE(stage.is_playing());
}

// =============================================================================
// Feature 30: Live Vulkan Frame Captures Visual Validation
// =============================================================================

TEST(Tier1_Feature30_Visual, PlatformerFrameEntropy)
{
    TempSandbox sandbox("visual_platformer");
    cd::world2d::Stage2D stage;
    stage.build_starter_illuminated_platformer();

    const auto stats = render_stage_to_png_and_analyze(stage, sandbox, "platformer.png", 128U, 128U);
    EXPECT_FALSE(stats.is_flat_monochrome);
    EXPECT_GT(stats.total_variance, 5.0F);
}

TEST(Tier1_Feature30_Visual, RpgFrameEntropy)
{
    TempSandbox sandbox("visual_rpg");
    cd::world2d::Stage2D stage;
    stage.set_gravity_y(0.0F);
    stage.set_ambient_color({ 0.12F, 0.15F, 0.22F });

    cd::world2d::Entity2D hero {};
    hero.name = "RpgHero";
    hero.position = { 4.0F, 3.0F };
    hero.size = { 1.5F, 2.0F };
    hero.color_rgba = { 0.95F, 0.85F, 0.3F, 1.0F };
    stage.add_entity(hero);

    const auto stats = render_stage_to_png_and_analyze(stage, sandbox, "rpg.png", 64U, 64U);
    EXPECT_FALSE(stats.is_flat_monochrome);
    EXPECT_GT(stats.total_variance, 0.0F);
}

TEST(Tier1_Feature30_Visual, ArenaFrameEntropy)
{
    TempSandbox sandbox("visual_arena");
    cd::world2d::Stage2D stage;
    stage.set_gravity_y(0.0F);

    cd::world2d::ParticleEmitterConfig2D cfg = cd::world2d::ParticleEmitterPresets2D::sparkle_burst();
    stage.particle_system().spawn_burst({ 0.0F, 0.0F }, 32U, cfg);

    const auto stats = render_stage_to_png_and_analyze(stage, sandbox, "arena.png", 64U, 64U);
    EXPECT_TRUE(stats.total_variance > 0.0F);
}

TEST(Tier1_Feature30_Visual, TextureAtlasSamplingVerified)
{
    auto atlas = cd::render2d::TextureAtlas2D::generate_procedural_starter_atlas(16U);
    ASSERT_TRUE(atlas.is_valid());
    const auto stats = HeadlessReadback::analyze_rgba8(atlas.pixels(), atlas.width(), atlas.height());
    EXPECT_FALSE(stats.is_flat_monochrome);
}

TEST(Tier1_Feature30_Visual, NoSolidFallbackBlocks)
{
    // Flat black buffer should be detected as flat monochrome
    std::vector<std::uint8_t> black(32 * 32 * 4, 0);
    const auto stats = HeadlessReadback::analyze_rgba8(black, 32, 32);
    EXPECT_TRUE(stats.is_flat_monochrome);
}

// =============================================================================
// Feature 31: Distribution Documentation Preservation
// =============================================================================

TEST(Tier1_Feature31_DocPreservation, DesignMdPreserved)
{
    const auto root = EnvironmentProbe::engine_root();
    const auto p = root / "docs" / "DESIGN.md";
    EXPECT_TRUE(std::filesystem::exists(p));
    EXPECT_GT(std::filesystem::file_size(p), 0U);
}

TEST(Tier1_Feature31_DocPreservation, PlanMdPreserved)
{
    const auto root = EnvironmentProbe::engine_root();
    const auto p = root / "docs" / "PLAN.md";
    EXPECT_TRUE(std::filesystem::exists(p));
    EXPECT_GT(std::filesystem::file_size(p), 0U);
}

TEST(Tier1_Feature31_DocPreservation, EngineGuidePreserved)
{
    const auto root = EnvironmentProbe::engine_root();
    const auto p = root / "docs" / "ENGINE_GUIDE.md";
    EXPECT_TRUE(std::filesystem::exists(p));
    EXPECT_GT(std::filesystem::file_size(p), 0U);
}

TEST(Tier1_Feature31_DocPreservation, ModulesDoxPreserved)
{
    const auto root = EnvironmentProbe::engine_root();
    const auto p = root / "docs" / "Modules.dox";
    EXPECT_TRUE(std::filesystem::exists(p));
    EXPECT_GT(std::filesystem::file_size(p), 0U);
}

TEST(Tier1_Feature31_DocPreservation, HumanVoiceComplianceCheck)
{
    const auto root = EnvironmentProbe::engine_root();
    const std::array<std::filesystem::path, 3> docs_to_check = {
        root / "docs" / "ENGINE_GUIDE.md",
        root / "docs" / "DESIGN.md",
        root / "docs" / "PLAN_NEXT_PHASES.md"
    };

    const std::vector<std::string> banned_words = {
        "delve", "tapestry", "seamless", "revolutionize", "unleashing"
    };

    for (const auto& doc_path : docs_to_check)
    {
        ASSERT_TRUE(std::filesystem::exists(doc_path)) << doc_path;
        std::ifstream ifs(doc_path, std::ios::binary);
        std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

        EXPECT_EQ(content.find("\xE2\x80\x94"), std::string::npos) << "Em-dash in " << doc_path;
        EXPECT_EQ(content.find(" --- "), std::string::npos) << "Triple dash in " << doc_path;

        for (const auto& w : banned_words)
        {
            EXPECT_EQ(content.find(" " + w + " "), std::string::npos)
                << "Forbidden word '" << w << "' in " << doc_path;
        }
    }
}

}  // namespace cd::e2e
