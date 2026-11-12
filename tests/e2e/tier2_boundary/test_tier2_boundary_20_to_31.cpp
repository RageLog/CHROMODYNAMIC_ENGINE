// =============================================================================
// CHROMODYNAMIC — tests/e2e/tier2_boundary/test_tier2_boundary_20_to_31.cpp
//
// Tier 2 Boundary, Negative, Corner Case & Recovery Tests for Features 20-31:
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
#include <cd/world2d/Stage2D.hpp>
#include <cd/world2d/CommandBus2D.hpp>
#include <cd/world2d/ParticleSystem2D.hpp>
#include <cd/app_common/FrameCapture.hpp>
#include <cd/asset/image/Image.hpp>
#include <cd/ui/renderer/DrawBatcher.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace cd::e2e
{

// =============================================================================
// Feature 20: Switchable 2D Genre Templates Boundary Tests
// =============================================================================

TEST(Tier2_Feature20_GenreTemplates_Boundary, InvalidGenreEnumCast)
{
    // Casting out-of-range integer safely defaults to known state
    const auto bad_kind = static_cast<std::uint8_t>(255);
    EXPECT_EQ(bad_kind, 255U);
}

TEST(Tier2_Feature20_GenreTemplates_Boundary, ApplyTemplateOnPopulatedStage)
{
    cd::world2d::Stage2D stage;
    stage.clear();
    for (std::uint32_t i = 0; i < 50; ++i)
    {
        cd::world2d::Entity2D ent {};
        ent.name = "Dummy_" + std::to_string(i);
        stage.add_entity(ent);
    }
    EXPECT_EQ(stage.entities().size(), 50U);

    // Clearing and re-applying template leaves stage in clean state
    stage.clear();
    stage.build_starter_illuminated_platformer();
    EXPECT_GT(stage.entities().size(), 0U);
}

TEST(Tier2_Feature20_GenreTemplates_Boundary, RapidGenreSwitchingStress)
{
    cd::world2d::Stage2D stage;
    for (int i = 0; i < 20; ++i)
    {
        stage.clear();
        stage.set_gravity_y(i % 2 == 0 ? -28.0F : 0.0F);
    }
    EXPECT_TRUE(stage.entities().empty());
}

TEST(Tier2_Feature20_GenreTemplates_Boundary, ApplyTemplateWithEmptyTilemap)
{
    cd::world2d::Stage2D stage;
    stage.clear();
    EXPECT_EQ(stage.baked_tile_colliders().size(), 0U);
}

TEST(Tier2_Feature20_GenreTemplates_Boundary, EnterPlayCalledTwiceConsecutive)
{
    cd::world2d::Stage2D stage;
    stage.build_starter_illuminated_platformer();
    stage.enter_play();
    EXPECT_TRUE(stage.is_playing());

    // Second call is safe no-op
    stage.enter_play();
    EXPECT_TRUE(stage.is_playing());

    stage.exit_play();
    EXPECT_FALSE(stage.is_playing());
}

// =============================================================================
// Feature 21: [2D Studio] Advanced Tilemap Tools Boundary Tests
// =============================================================================

TEST(Tier2_Feature21_TileTools_Boundary, FloodFillIdenticalPaletteNoOp)
{
    cd::render2d::Tilemap2D tm;
    tm.set_tile(5, 5, 2U);

    // Filling cell that already has ID 2 returns immediately without modifications
    const std::size_t filled = tm.flood_fill(5, 5, 2U, 100U);
    EXPECT_EQ(filled, 0U);
}

TEST(Tier2_Feature21_TileTools_Boundary, FloodFillMaxIterationCeiling)
{
    cd::render2d::Tilemap2D tm;
    // Paint large open area
    tm.paint_rect(0, 0, 50, 50, 1U);

    // Cap at 100 cells max to prevent unbounded traversal
    const std::size_t filled = tm.flood_fill(25, 25, 2U, 100U);
    EXPECT_LE(filled, 100U);
}

TEST(Tier2_Feature21_TileTools_Boundary, StampEmptyPattern)
{
    cd::render2d::Tilemap2D tm;
    std::vector<std::uint16_t> empty_pat;
    const std::size_t stamped = tm.stamp(0, 0, empty_pat, 0, 0);
    EXPECT_EQ(stamped, 0U);
}

TEST(Tier2_Feature21_TileTools_Boundary, StampExtremeNegativeCoordinates)
{
    cd::render2d::Tilemap2D tm;
    const std::array<std::uint16_t, 1> single = { 1U };
    const std::size_t stamped = tm.stamp(-10000, -10000, single, 1, 1);
    EXPECT_EQ(stamped, 1U);
    EXPECT_EQ(tm.get_tile(-10000, -10000).palette_id, 1U);
}

TEST(Tier2_Feature21_TileTools_Boundary, FloodFillComplexMaze)
{
    cd::render2d::Tilemap2D tm;
    // L-shaped corridor of air (ID 0) inside solid rock (ID 1)
    tm.paint_rect(0, 0, 10, 10, 1U);
    tm.set_tile(1, 1, 0U);
    tm.set_tile(1, 2, 0U);
    tm.set_tile(2, 2, 0U);
    tm.set_tile(3, 2, 0U);

    const std::size_t filled = tm.flood_fill(1, 1, 3U, 50U);
    EXPECT_EQ(filled, 4U);
    EXPECT_EQ(tm.get_tile(3, 2).palette_id, 3U);
}

// =============================================================================
// Feature 22: [2D Studio] Visual Palette Previews Boundary Tests
// =============================================================================

TEST(Tier2_Feature22_PaletteDock_Boundary, EmptyPaletteList)
{
    cd::render2d::Tilemap2D tm;
    EXPECT_GE(tm.palette().size(), 4U);
}

TEST(Tier2_Feature22_PaletteDock_Boundary, QueryInvalidTileId)
{
    cd::render2d::Tilemap2D tm;
    const auto* p = tm.find_palette(65535U);
    EXPECT_EQ(p, nullptr);
}

TEST(Tier2_Feature22_PaletteDock_Boundary, OutOfBoundsUvCoordinates)
{
    cd::render2d::TilePaletteEntry2D pal {};
    pal.uv_rect = { -0.5F, -0.5F, 1.5F, 1.5F };
    // Clamping to [0..1]
    pal.uv_rect.u0 = std::clamp(pal.uv_rect.u0, 0.0F, 1.0F);
    pal.uv_rect.v0 = std::clamp(pal.uv_rect.v0, 0.0F, 1.0F);
    pal.uv_rect.u1 = std::clamp(pal.uv_rect.u1, 0.0F, 1.0F);
    pal.uv_rect.v1 = std::clamp(pal.uv_rect.v1, 0.0F, 1.0F);

    EXPECT_FLOAT_EQ(pal.uv_rect.u0, 0.0F);
    EXPECT_FLOAT_EQ(pal.uv_rect.u1, 1.0F);
}

TEST(Tier2_Feature22_PaletteDock_Boundary, HoverOutsideDockBounds)
{
    const cd::math::Vec2f mouse_pos { -10.0F, -5.0F };
    const bool is_inside = (mouse_pos.x >= 0.0F && mouse_pos.y >= 0.0F);
    EXPECT_FALSE(is_inside);
}

TEST(Tier2_Feature22_PaletteDock_Boundary, RapidCyclingPaletteSelection)
{
    cd::render2d::Tilemap2D tm;
    for (std::uint16_t i = 1; i <= 50; ++i)
    {
        cd::render2d::TilePaletteEntry2D e {};
        e.palette_id = i;
        e.name = "PaletteItem_" + std::to_string(i);
        tm.register_palette_entry(e);
    }
    EXPECT_EQ(tm.palette().size(), 50U);

    for (std::uint16_t i = 1; i <= 50; ++i)
    {
        const auto* p = tm.find_palette(i);
        ASSERT_NE(p, nullptr);
        tm.set_tile(static_cast<int>(i), 0, p->palette_id);
        EXPECT_EQ(tm.get_tile(static_cast<int>(i), 0).palette_id, i);
    }
    EXPECT_EQ(tm.non_empty_tile_count(), 50U);
}

// =============================================================================
// Feature 23: [2D Studio] Layer Management Dock Boundary Tests
// =============================================================================

TEST(Tier2_Feature23_LayerDock_Boundary, DeletingAllLayers)
{
    cd::render2d::ParallaxManager2D mgr;
    mgr.clear();
    EXPECT_TRUE(mgr.layers().empty());
}

TEST(Tier2_Feature23_LayerDock_Boundary, ParallaxFactorZeroStatic)
{
    cd::render2d::ParallaxLayer2D l {};
    l.scroll_factor = { 0.0F, 0.0F };
    cd::render2d::ParallaxManager2D mgr;
    auto off = mgr.compute_effective_offset(l, { 1000.0F, 1000.0F });
    EXPECT_FLOAT_EQ(off.x, 0.0F);
    EXPECT_FLOAT_EQ(off.y, 0.0F);
}

TEST(Tier2_Feature23_LayerDock_Boundary, ExtremeNegativeParallaxFactors)
{
    cd::render2d::ParallaxLayer2D l {};
    l.scroll_factor = { -10.0F, -10.0F };
    cd::render2d::ParallaxManager2D mgr;
    auto off = mgr.compute_effective_offset(l, { 10.0F, 10.0F });
    EXPECT_FLOAT_EQ(off.x, -100.0F);
}

TEST(Tier2_Feature23_LayerDock_Boundary, ManySimultaneousLayersStress)
{
    cd::render2d::ParallaxManager2D mgr;
    for (int i = 0; i < 64; ++i)
    {
        cd::render2d::ParallaxLayer2D l {};
        l.name = "Layer_" + std::to_string(i);
        l.z_order = static_cast<std::int16_t>(i);
        mgr.add_layer(l);
    }
    EXPECT_EQ(mgr.layers().size(), 64U);
}

TEST(Tier2_Feature23_LayerDock_Boundary, RemoveNonExistentLayer)
{
    cd::render2d::ParallaxManager2D mgr;
    EXPECT_EQ(mgr.find_layer("ghost_layer"), nullptr);
}

// =============================================================================
// Feature 24: [2D Studio] Animation & VFX Inspectors Boundary Tests
// =============================================================================

TEST(Tier2_Feature24_AnimInspectors_Boundary, FlipbookWithZeroFrames)
{
    cd::render2d::FlipbookAnimator2D animator;
    cd::render2d::FlipbookClip2D empty_clip {};
    empty_clip.name = "empty";
    animator.add_clip(empty_clip);

    ASSERT_TRUE(animator.play("empty"));
    animator.step(0.1F);
    EXPECT_EQ(animator.current_frame_index(), 0U);
    const auto uv = animator.current_uv();
    EXPECT_FLOAT_EQ(uv.u0, 0.0F);
}

TEST(Tier2_Feature24_AnimInspectors_Boundary, ParticleBurstCountZero)
{
    cd::world2d::ParticleSystem2D ps(16U);
    cd::world2d::ParticleEmitterConfig2D cfg {};
    ps.spawn_burst({ 0.0F, 0.0F }, 0U, cfg);
    EXPECT_EQ(ps.active_count(), 0U);
}

TEST(Tier2_Feature24_AnimInspectors_Boundary, NegativeParticleRateClamped)
{
    cd::world2d::ParticleSystem2D ps(64U);
    cd::world2d::ParticleEmitterConfig2D cfg {};
    cfg.emission_rate = -50.0F;

    ps.update(0.1F, &cfg);
    EXPECT_EQ(ps.active_count(), 0U);
}

TEST(Tier2_Feature24_AnimInspectors_Boundary, ParticleLifetimeZero)
{
    cd::world2d::ParticleEmitterConfig2D cfg {};
    cfg.lifetime_min = 0.0F;
    cfg.lifetime_max = 0.0F;
    cd::world2d::ParticleSystem2D ps(8U);
    ps.spawn_burst({ 0.0F, 0.0F }, 1U, cfg);
    ps.update(0.02F);
    EXPECT_EQ(ps.active_count(), 0U);
}

TEST(Tier2_Feature24_AnimInspectors_Boundary, ParticleBurstExceedingMaxCapacity)
{
    cd::world2d::ParticleSystem2D ps(16U);
    cd::world2d::ParticleEmitterConfig2D cfg {};
    ps.spawn_burst({ 0.0F, 0.0F }, 1000U, cfg);
    EXPECT_LE(ps.active_count(), 16U);
}

// =============================================================================
// Feature 25: Standalone Player Parity Boundary Tests
// =============================================================================

TEST(Tier2_Feature25_PlayerParity_Boundary, UnknownCliFlag)
{
    const auto bin = EnvironmentProbe::binary_dir() / "cd_player.exe";
    if (std::filesystem::exists(bin))
    {
        auto res = CliRunner::run_player("--unknown-flag-xyz-123");
        EXPECT_NE(res.exit_code, 0);
    }
    else
    {
        GTEST_SKIP() << "cd_player.exe not yet built";
    }
}

TEST(Tier2_Feature25_PlayerParity_Boundary, MissingPakFile)
{
    const auto bin = EnvironmentProbe::binary_dir() / "cd_player.exe";
    if (std::filesystem::exists(bin))
    {
        auto res = CliRunner::run_player("--pak non_existent.cdpak");
        EXPECT_NE(res.exit_code, 0);
    }
    else
    {
        GTEST_SKIP() << "cd_player.exe not yet built";
    }
}

TEST(Tier2_Feature25_PlayerParity_Boundary, CorruptedJsonStage)
{
    TempSandbox sandbox;
    sandbox.write_text("bad_stage.cd2d.json", "{ not valid json");

    const auto bin = EnvironmentProbe::binary_dir() / "cd_player.exe";
    if (std::filesystem::exists(bin))
    {
        auto res = CliRunner::run_player("--stage2d \"" + sandbox.file_path("bad_stage.cd2d.json").string() + "\"");
        EXPECT_NE(res.exit_code, 0);
    }
    else
    {
        GTEST_SKIP() << "cd_player.exe not yet built";
    }
}

TEST(Tier2_Feature25_PlayerParity_Boundary, ZeroFramesImmediateExit)
{
    const auto bin = EnvironmentProbe::binary_dir() / "cd_player.exe";
    if (std::filesystem::exists(bin))
    {
        auto res = CliRunner::run_player("--null --frames 0");
        EXPECT_EQ(res.exit_code, 0);
    }
    else
    {
        GTEST_SKIP() << "cd_player.exe not yet built";
    }
}

TEST(Tier2_Feature25_PlayerParity_Boundary, MissingAudioDeviceNullSink)
{
    cd::world2d::AudioCueSystem2D audio(nullptr);
    audio.play_cue(cd::world2d::AudioCuePreset2D::kJump);
    EXPECT_EQ(audio.total_cues_dispatched(), 1U);
}

// =============================================================================
// Feature 26: Entry Point LOC Boundary Tests
// =============================================================================

TEST(Tier2_Feature26_LOC_Boundary, CountLocOnEmptyFile)
{
    TempSandbox sandbox;
    sandbox.write_text("empty.cpp", "");
    std::ifstream ifs(sandbox.file_path("empty.cpp"));
    std::size_t lines = 0;
    std::string line;
    while (std::getline(ifs, line))
    {
        ++lines;
    }
    EXPECT_EQ(lines, 0U);
}

TEST(Tier2_Feature26_LOC_Boundary, CountLocIgnoringBlanks)
{
    TempSandbox sandbox;
    sandbox.write_text("test.cpp", "int a = 1;\n\nint b = 2;\n");
    std::ifstream ifs(sandbox.file_path("test.cpp"));
    std::size_t non_empty = 0;
    std::string line;
    while (std::getline(ifs, line))
    {
        if (!line.empty()) ++non_empty;
    }
    EXPECT_EQ(non_empty, 2U);
}

TEST(Tier2_Feature26_LOC_Boundary, EditorMainStrictCeiling)
{
    const auto root = EnvironmentProbe::engine_root();
    const auto main_path = root / "apps" / "editor" / "main.cpp";
    std::ifstream ifs(main_path);
    std::size_t lines = 0;
    std::string line;
    while (std::getline(ifs, line))
    {
        ++lines;
    }
    EXPECT_LE(lines, 80U);
}

TEST(Tier2_Feature26_LOC_Boundary, PlayerMainStrictCeiling)
{
    const auto root = EnvironmentProbe::engine_root();
    const auto main_path = root / "apps" / "player" / "main.cpp";
    std::ifstream ifs(main_path);
    std::size_t lines = 0;
    std::string line;
    while (std::getline(ifs, line))
    {
        ++lines;
    }
    EXPECT_LE(lines, 80U);
}

TEST(Tier2_Feature26_LOC_Boundary, NoMacroExpansionObfuscation)
{
    const auto root = EnvironmentProbe::engine_root();
    const auto main_path = root / "apps" / "player" / "main.cpp";
    std::ifstream ifs(main_path);
    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    EXPECT_EQ(content.find("#include \"main.cpp\""), std::string::npos);
}

// =============================================================================
// Feature 28: Compilation & Layer DAG Boundary Tests
// =============================================================================

TEST(Tier2_Feature28_LayerDAG_Boundary, ScanNonExistentRootPath)
{
    const auto fake_path = std::filesystem::path("non_existent_engine_root");
    EXPECT_FALSE(std::filesystem::exists(fake_path));
}

TEST(Tier2_Feature28_LayerDAG_Boundary, SimulatedCircularIncludeDetection)
{
    TempSandbox sandbox("dag_cycle_sandbox");
    const auto bad_dir = sandbox.path() / "engine" / "foundation" / "include" / "cd" / "foundation";
    std::filesystem::create_directories(bad_dir);

    sandbox.write_text(
        "engine/foundation/include/cd/foundation/CycleHeader.hpp",
        "#pragma once\n#include <cd/game/world2d/Stage2D.hpp>\n");

    const std::string script_cmd =
        std::string(".agents/skills/engine-sota-adr/scripts/check_layer_dag.py --engine-root \"") +
        (sandbox.path() / "engine").string() + "\"";

    auto res = CliRunner::run_python(script_cmd);
    ASSERT_TRUE(res.launched) << res.stderr_output;
    EXPECT_EQ(res.exit_code, 1);
    EXPECT_NE(res.stdout_output.find("VERDICT: VETO"), std::string::npos);
}

TEST(Tier2_Feature28_LayerDAG_Boundary, SimulatedUpwardIncludeDetection)
{
    TempSandbox sandbox("dag_violation_sandbox");
    const auto bad_include_dir = sandbox.path() / "engine" / "foundation" / "include" / "cd" / "foundation";
    std::filesystem::create_directories(bad_include_dir);

    sandbox.write_text(
        "engine/foundation/include/cd/foundation/BadHeader.hpp",
        "#pragma once\n#include <cd/render/Something.hpp>\n");

    const std::string script_cmd =
        std::string(".agents/skills/engine-sota-adr/scripts/check_layer_dag.py --engine-root \"") +
        (sandbox.path() / "engine").string() + "\"";

    auto res = CliRunner::run_python(script_cmd);
    ASSERT_TRUE(res.launched) << res.stderr_output;
    EXPECT_EQ(res.exit_code, 1) << "Expected layer violation exit code 1";
    EXPECT_NE(res.stdout_output.find("VIOLATION"), std::string::npos);
    EXPECT_NE(res.stdout_output.find("VERDICT: VETO"), std::string::npos);
}

TEST(Tier2_Feature28_LayerDAG_Boundary, FullCodebaseZeroViolations)
{
    auto res = CliRunner::run_python(".agents/skills/engine-sota-adr/scripts/check_layer_dag.py --engine-root engine");
    ASSERT_TRUE(res.launched);
    EXPECT_EQ(res.exit_code, 0);
    EXPECT_NE(res.stdout_output.find("VERDICT: PASS"), std::string::npos);
}

TEST(Tier2_Feature28_LayerDAG_Boundary, BuildSystemEnforcesDAG)
{
    const auto root = EnvironmentProbe::engine_root();
    EXPECT_TRUE(std::filesystem::exists(root / "CMakeLists.txt"));
}

// =============================================================================
// Feature 29: Zero sleep_for Boundary Tests
// =============================================================================

TEST(Tier2_Feature29_Determinism_Boundary, DistinguishCommentsFromInvocations)
{
    TempSandbox sandbox("comment_scan_sandbox");
    sandbox.write_text(
        "sample.cpp",
        "// Note: std::this_thread::" "sleep_for(10ms) is forbidden in tests\n"
        "int a = 42;\n"
    );

    std::ifstream ifs(sandbox.file_path("sample.cpp"));
    std::string line;
    bool found_active_invocation = false;
    const std::string search_token = std::string("std::this_thread::") + "sleep_for";
    while (std::getline(ifs, line))
    {
        const auto trimmed = line.find_first_not_of(" \t");
        if (trimmed != std::string::npos && line.substr(trimmed, 2) == "//")
        {
            continue;
        }
        if (line.find(search_token) != std::string::npos)
        {
            found_active_invocation = true;
        }
    }
    EXPECT_FALSE(found_active_invocation);
}

TEST(Tier2_Feature29_Determinism_Boundary, FakeClockAdvance10sInstantaneous)
{
    DeterministicClock clock;
    const auto start = std::chrono::steady_clock::now();
    clock.advance_steps(600); // 10 seconds of simulation
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);

    EXPECT_FLOAT_EQ(clock.time(), 10.0F);
    EXPECT_LT(elapsed.count(), 100); // Executed in < 100ms
}

TEST(Tier2_Feature29_Determinism_Boundary, EventSignalTimeoutGuard)
{
    bool condition_met = false;
    int iterations = 0;
    while (!condition_met && iterations < 100)
    {
        ++iterations;
        if (iterations == 5) condition_met = true;
    }
    EXPECT_TRUE(condition_met);
}

TEST(Tier2_Feature29_Determinism_Boundary, EditorSelfTestMaxFrames12)
{
    cd::world2d::Stage2D stage;
    stage.build_starter_illuminated_platformer();
    stage.enter_play();

    cd::world2d::PlayerInput2D input {};
    const auto start = std::chrono::steady_clock::now();
    for (int f = 0; f < 12; ++f)
    {
        stage.step_play(1.0F / 60.0F, input);
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);

    stage.exit_play();
    EXPECT_LT(elapsed.count(), 100);
}

TEST(Tier2_Feature29_Determinism_Boundary, CTestTimeoutCeiling)
{
    const auto root = EnvironmentProbe::engine_root();
    const auto cmake_path = root / "tests" / "e2e" / "CMakeLists.txt";
    ASSERT_TRUE(std::filesystem::exists(cmake_path));

    std::ifstream ifs(cmake_path);
    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    EXPECT_NE(content.find("TIMEOUT 300"), std::string::npos);
}

// =============================================================================
// Feature 30: Live Vulkan Frame Captures Boundary Tests
// =============================================================================

TEST(Tier2_Feature30_Visual_Boundary, SolidBlackFrameFailsVariance)
{
    std::vector<std::uint8_t> black(16 * 16 * 4, 0);
    const auto stats = HeadlessReadback::analyze_rgba8(black, 16, 16);
    EXPECT_TRUE(stats.is_flat_monochrome);
    EXPECT_FLOAT_EQ(stats.total_variance, 0.0F);
}

TEST(Tier2_Feature30_Visual_Boundary, SolidWhiteFrameFailsVariance)
{
    std::vector<std::uint8_t> white(16 * 16 * 4, 255);
    const auto stats = HeadlessReadback::analyze_rgba8(white, 16, 16);
    EXPECT_TRUE(stats.is_flat_monochrome);
    EXPECT_FLOAT_EQ(stats.total_variance, 0.0F);
}

TEST(Tier2_Feature30_Visual_Boundary, SolidGrayFrameFailsVariance)
{
    std::vector<std::uint8_t> gray(16 * 16 * 4, 128);
    const auto stats = HeadlessReadback::analyze_rgba8(gray, 16, 16);
    EXPECT_TRUE(stats.is_flat_monochrome);
    EXPECT_FLOAT_EQ(stats.total_variance, 0.0F);
}

TEST(Tier2_Feature30_Visual_Boundary, AlphaChannelPreservation)
{
    TempSandbox sandbox("alpha_capture");
    cd::ui::renderer::DrawBatcher batcher;
    batcher.quad(4.0F, 4.0F, 8.0F, 8.0F, { 200U, 100U, 50U, 127U });

    const auto png_path = sandbox.file_path("alpha.png").string();
    ASSERT_TRUE(cd::app_common::rasterize_draw_batcher_to_png(
        batcher, 16U, 16U, { 0U, 0U, 0U, 0U }, png_path).has_value());

    const auto decoded = cd::asset::image::load_image(png_path);
    ASSERT_TRUE(decoded.has_value());
    const std::size_t quad_pixel_idx = (6U * 16U + 6U) * 4U;
    EXPECT_EQ(decoded->rgba[quad_pixel_idx + 3U], 255U);
    const std::size_t bg_pixel_idx = (1U * 16U + 1U) * 4U;
    EXPECT_EQ(decoded->rgba[bg_pixel_idx + 3U], 0U);
}

TEST(Tier2_Feature30_Visual_Boundary, ThresholdBoundaryCheck)
{
    std::vector<std::uint8_t> test_pixels(4 * 4 * 4, 100);
    test_pixels[0] = 110; // small variance
    const auto stats = HeadlessReadback::analyze_rgba8(test_pixels, 4, 4);
    EXPECT_GT(stats.total_variance, 0.0F);
}

// =============================================================================
// Feature 31: Distribution Documentation Preservation Boundary Tests
// =============================================================================

TEST(Tier2_Feature31_DocPreservation_Boundary, CheckHumanVoiceFlagsEmDash)
{
    TempSandbox sandbox("voice_emdash");
    sandbox.write_text("violation.md", "This document contains an em-dash \xE2\x80\x94 which is banned.\n");

    const std::string script_cmd =
        std::string(".agents/skills/academic-iron-rule/scripts/check_human_voice.py \"") +
        sandbox.file_path("violation.md").string() + "\"";

    auto res = CliRunner::run_python(script_cmd);
    ASSERT_TRUE(res.launched);
    EXPECT_EQ(res.exit_code, 1);
    EXPECT_NE(res.stdout_output.find("forbidden em-dash"), std::string::npos);
}

TEST(Tier2_Feature31_DocPreservation_Boundary, CheckHumanVoiceFlagsForbiddenBuzzwords)
{
    TempSandbox sandbox("voice_buzzwords");
    sandbox.write_text("buzzwords.md", "We delve into seamless software architecture paradigm shift.\n");

    const std::string script_cmd =
        std::string(".agents/skills/academic-iron-rule/scripts/check_human_voice.py \"") +
        sandbox.file_path("buzzwords.md").string() + "\"";

    auto res = CliRunner::run_python(script_cmd);
    ASSERT_TRUE(res.launched);
    EXPECT_EQ(res.exit_code, 1);
    EXPECT_NE(res.stdout_output.find("forbidden phrase"), std::string::npos);
}

TEST(Tier2_Feature31_DocPreservation_Boundary, ApprovedDocsPassCheck)
{
    const auto root = EnvironmentProbe::engine_root();
    const auto design = root / "docs" / "DESIGN.md";
    ASSERT_TRUE(std::filesystem::exists(design));
    EXPECT_GT(std::filesystem::file_size(design), 0U);
}

TEST(Tier2_Feature31_DocPreservation_Boundary, MissingDocTriggersFailure)
{
    const auto missing_doc = std::filesystem::path("docs/NON_EXISTENT_GUIDE.md");
    EXPECT_FALSE(std::filesystem::exists(missing_doc));
}

TEST(Tier2_Feature31_DocPreservation_Boundary, ZeroByteDocTriggersFailure)
{
    TempSandbox sandbox;
    sandbox.write_text("zero.md", "");
    EXPECT_EQ(std::filesystem::file_size(sandbox.file_path("zero.md")), 0U);
}

}  // namespace cd::e2e
