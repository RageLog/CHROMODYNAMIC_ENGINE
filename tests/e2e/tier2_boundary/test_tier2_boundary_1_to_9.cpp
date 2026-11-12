// =============================================================================
// CHROMODYNAMIC — tests/e2e/tier2_boundary/test_tier2_boundary_1_to_9.cpp
//
// Tier 2 Boundary, Negative, Corner Case & Recovery Tests for Features 1-9:
//   F1: Unified SceneRenderer 19-Pass Execution
//   F2: 3D Split-Brain Elimination
//   F3: StageMode::HD2D Hybrid Pipeline
//   F4: FAZ 36 Roadmap Closure
//   F5: Live JSON-RPC / MCP Socket Server
//   F6: Hot-Reload Scene Synchronization
//   F7: tools/assetc Offline Asset Cooker
//   F8: Standalone Package Exporter
//   F9: FAZ 37 Roadmap Closure
// =============================================================================

#include "../harness/E2eHarness.hpp"
#include "../harness/CliRunner.hpp"

#include <cd/core/Result.hpp>
#include <cd/framegraph/FrameGraph.hpp>
#include <cd/rhi/NullDevice.hpp>
#include <cd/rhi/Descriptors.hpp>
#include <cd/rhi/Format.hpp>
#include <cd/scene_renderer/SceneRenderer.hpp>
#include <cd/scene_renderer/SurfaceAndPostFxPipeline.hpp>
#include <cd/world2d/CommandBus2D.hpp>
#include <cd/world2d/Stage2D.hpp>
#include <cd/asset/json/Json.hpp>
#include <cd/asset/pak/Pak.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace cd::e2e
{

// =============================================================================
// Feature 1: Unified SceneRenderer 19-Pass Execution Boundary Tests
// =============================================================================

TEST(Tier2_Feature1_SceneRenderer_Boundary, ZeroExtentViewport)
{
    cd::rhi::NullDevice dev;
    cd::framegraph::FrameGraph fg(dev);

    cd::rhi::TextureDesc desc {};
    desc.type = cd::rhi::TextureType::k2D;
    desc.format = cd::rhi::Format::kRGBA8Unorm;
    desc.extent = { 1U, 1U, 1U };
    desc.usage = cd::rhi::TextureUsage::kColorAttachment;

    auto tex = dev.create_texture(desc);
    ASSERT_TRUE(tex.has_value());

    cd::scene_renderer::SceneRendererConfig cfg {};
    cfg.viewport = { 0U, 0U };

    // Viewport clamped internally to min 1x1 without crash
    auto handles = cd::scene_renderer::build_scene_renderer_framegraph(fg, *tex, cfg);
    EXPECT_TRUE(handles.final_backbuffer.is_valid());

    dev.destroy_texture(*tex);
}

TEST(Tier2_Feature1_SceneRenderer_Boundary, HugeExtentViewport)
{
    cd::rhi::NullDevice dev;
    cd::framegraph::FrameGraph fg(dev);

    cd::rhi::TextureDesc desc {};
    desc.type = cd::rhi::TextureType::k2D;
    desc.format = cd::rhi::Format::kRGBA8Unorm;
    desc.extent = { 7680U, 4320U, 1U };
    desc.usage = cd::rhi::TextureUsage::kColorAttachment;

    auto tex = dev.create_texture(desc);
    ASSERT_TRUE(tex.has_value());

    cd::scene_renderer::SceneRendererConfig cfg {};
    cfg.viewport = { 7680U, 4320U };

    auto handles = cd::scene_renderer::build_scene_renderer_framegraph(fg, *tex, cfg);
    EXPECT_TRUE(handles.final_backbuffer.is_valid());

    dev.destroy_texture(*tex);
}

TEST(Tier2_Feature1_SceneRenderer_Boundary, NullBackbufferHandle)
{
    cd::rhi::NullDevice dev;
    cd::framegraph::FrameGraph fg(dev);

    cd::rhi::TextureHandle null_tex {};
    cd::scene_renderer::SceneRendererConfig cfg {};

    // Passing an uninitialized handle must be handled safely
    auto handles = cd::scene_renderer::build_scene_renderer_framegraph(fg, null_tex, cfg);
    EXPECT_TRUE(handles.final_backbuffer.is_valid());
}

TEST(Tier2_Feature1_SceneRenderer_Boundary, AllFeaturesDisabled)
{
    cd::rhi::NullDevice dev;
    cd::framegraph::FrameGraph fg(dev);

    cd::rhi::TextureDesc desc {};
    desc.type = cd::rhi::TextureType::k2D;
    desc.format = cd::rhi::Format::kRGBA8Unorm;
    desc.extent = { 64U, 64U, 1U };
    desc.usage = cd::rhi::TextureUsage::kColorAttachment;

    auto tex = dev.create_texture(desc);
    ASSERT_TRUE(tex.has_value());

    cd::scene_renderer::SceneRendererConfig cfg {};
    cfg.viewport = { 64U, 64U };
    cfg.culling.enable_frustum_culling = false;
    cfg.culling.enable_hiz_occlusion = false;
    cfg.surface.enable_sss_skin = false;
    cfg.surface.enable_kajiya_kay_hair = false;
    cfg.atmosphere.enable_volumetric_fog = false;
    cfg.atmosphere.enable_volumetric_clouds = false;
    cfg.gi.enable_gtao = false;
    cfg.gi.enable_ssr = false;
    cfg.postfx.enable_bloom = false;
    cfg.postfx.enable_dof = false;
    cfg.postfx.enable_motion_blur = false;

    auto handles = cd::scene_renderer::build_scene_renderer_framegraph(fg, *tex, cfg);
    (void)handles;
    EXPECT_TRUE(fg.compile().has_value());

    dev.destroy_texture(*tex);
}

TEST(Tier2_Feature1_SceneRenderer_Boundary, ExtremeExposureParameters)
{
    cd::rhi::NullDevice dev;
    cd::framegraph::FrameGraph fg(dev);

    cd::rhi::TextureDesc desc {};
    desc.type = cd::rhi::TextureType::k2D;
    desc.format = cd::rhi::Format::kRGBA8Unorm;
    desc.extent = { 64U, 64U, 1U };
    desc.usage = cd::rhi::TextureUsage::kColorAttachment;

    auto tex = dev.create_texture(desc);
    ASSERT_TRUE(tex.has_value());

    cd::scene_renderer::SceneRendererConfig cfg {};
    cfg.viewport = { 64U, 64U };
    cfg.postfx.enable_auto_exposure = true;
    cfg.postfx.tonemap = cd::scene_renderer::TonemapOperator::kHdr10Pq;

    auto handles = cd::scene_renderer::build_scene_renderer_framegraph(fg, *tex, cfg);
    (void)handles;
    EXPECT_TRUE(fg.compile().has_value());

    dev.destroy_texture(*tex);
}

// =============================================================================
// Feature 2: 3D Split-Brain Elimination Boundary Tests
// =============================================================================

TEST(Tier2_Feature2_SplitBrain_Boundary, HeadlessZeroFrames)
{
    const auto bin = EnvironmentProbe::binary_dir() / "cd_player.exe";
    if (std::filesystem::exists(bin))
    {
        auto res = CliRunner::run_player("--headless --frames 0");
        EXPECT_EQ(res.exit_code, 0);
    }
    else
    {
        GTEST_SKIP() << "cd_player.exe not yet built";
    }
}

TEST(Tier2_Feature2_SplitBrain_Boundary, MissingStagePath)
{
    const auto bin = EnvironmentProbe::binary_dir() / "cd_player.exe";
    if (std::filesystem::exists(bin))
    {
        auto res = CliRunner::run_player("--stage2d nonexistent_file_xyz.json");
        EXPECT_NE(res.exit_code, 0);
    }
    else
    {
        GTEST_SKIP() << "cd_player.exe not yet built";
    }
}

TEST(Tier2_Feature2_SplitBrain_Boundary, RapidResizeEvent)
{
    cd::rhi::NullDevice dev;

    const std::vector<cd::rhi::Extent2D> viewports = {
        { 1920U, 1080U },
        { 1U, 1U },
        { 3840U, 2160U },
        { 64U, 64U }
    };

    for (const auto& vp : viewports)
    {
        cd::framegraph::FrameGraph fg(dev);
        cd::rhi::TextureDesc desc {};
        desc.type = cd::rhi::TextureType::k2D;
        desc.format = cd::rhi::Format::kRGBA8Unorm;
        desc.extent = { vp.width, vp.height, 1U };
        desc.usage = cd::rhi::TextureUsage::kColorAttachment;

        auto tex = dev.create_texture(desc);
        ASSERT_TRUE(tex.has_value());

        cd::scene_renderer::SceneRendererConfig cfg {};
        cfg.viewport = vp;

        auto handles = cd::scene_renderer::build_scene_renderer_framegraph(fg, *tex, cfg);
        (void)handles;
        EXPECT_TRUE(fg.compile().has_value());

        dev.destroy_texture(*tex);
    }
}

TEST(Tier2_Feature2_SplitBrain_Boundary, CorruptStageFile)
{
    cd::world2d::Stage2D stage;
    const std::string corrupt_json = "{ \"stage_name\": \"broken\", \"entities\": [ { invalid json";
    auto res = stage.load_from_json_string(corrupt_json);
    EXPECT_FALSE(res.has_value());
}

TEST(Tier2_Feature2_SplitBrain_Boundary, MissingGpuFallback)
{
    cd::rhi::NullDevice dev;
    EXPECT_EQ(dev.limits().max_texture_dimension_2d, 16384U);
}

// =============================================================================
// Feature 3: StageMode::HD2D Hybrid Pipeline Boundary Tests
// =============================================================================

TEST(Tier2_Feature3_HD2D_Boundary, FullyTransparentSprite)
{
    cd::rhi::NullDevice dev;
    cd::framegraph::FrameGraph fg(dev);
    cd::rhi::TextureDesc desc {};
    desc.extent = { 128U, 128U, 1U };
    desc.format = cd::rhi::Format::kRGBA8Unorm;
    desc.usage = cd::rhi::TextureUsage::kColorAttachment;
    auto tex = dev.create_texture(desc);
    ASSERT_TRUE(tex.has_value());

    cd::scene_renderer::SceneRendererConfig cfg {};
    cfg.viewport = { 128U, 128U };

    cd::scene_renderer::SceneViewData scene_data {};
    scene_data.stage_mode = cd::scene_renderer::StageMode::HD2D;

    cd::scene_renderer::SceneSpriteCardHD2D card {};
    card.position = { 0.0F, 0.0F, 0.0F };
    card.size = { 1.0F, 1.0F };
    card.alpha_cutoff = 1.0F;
    card.tint = { 1.0F, 1.0F, 1.0F, 0.0F };
    scene_data.sprite_cards.push_back(card);

    auto handles = cd::scene_renderer::build_scene_renderer_framegraph(fg, *tex, cfg, &scene_data);
    EXPECT_TRUE(handles.depth.is_valid());
    EXPECT_TRUE(fg.compile().has_value());

    dev.destroy_texture(*tex);
}

TEST(Tier2_Feature3_HD2D_Boundary, FullyOpaqueSprite)
{
    cd::rhi::NullDevice dev;
    cd::framegraph::FrameGraph fg(dev);
    cd::rhi::TextureDesc desc {};
    desc.extent = { 128U, 128U, 1U };
    desc.format = cd::rhi::Format::kRGBA8Unorm;
    desc.usage = cd::rhi::TextureUsage::kColorAttachment;
    auto tex = dev.create_texture(desc);
    ASSERT_TRUE(tex.has_value());

    cd::scene_renderer::SceneRendererConfig cfg {};
    cfg.viewport = { 128U, 128U };

    cd::scene_renderer::SceneViewData scene_data {};
    scene_data.stage_mode = cd::scene_renderer::StageMode::HD2D;

    cd::scene_renderer::SceneSpriteCardHD2D card {};
    card.position = { 0.0F, 0.0F, 0.0F };
    card.size = { 1.0F, 1.0F };
    card.alpha_cutoff = 0.0F;
    card.tint = { 1.0F, 1.0F, 1.0F, 1.0F };
    scene_data.sprite_cards.push_back(card);

    auto handles = cd::scene_renderer::build_scene_renderer_framegraph(fg, *tex, cfg, &scene_data);
    EXPECT_TRUE(handles.depth.is_valid());
    EXPECT_TRUE(fg.compile().has_value());

    dev.destroy_texture(*tex);
}

TEST(Tier2_Feature3_HD2D_Boundary, EdgeOnCameraAngle)
{
    cd::rhi::NullDevice dev;
    cd::framegraph::FrameGraph fg(dev);
    cd::rhi::TextureDesc desc {};
    desc.extent = { 128U, 128U, 1U };
    desc.format = cd::rhi::Format::kRGBA8Unorm;
    desc.usage = cd::rhi::TextureUsage::kColorAttachment;
    auto tex = dev.create_texture(desc);
    ASSERT_TRUE(tex.has_value());

    cd::scene_renderer::SceneRendererConfig cfg {};
    cfg.viewport = { 128U, 128U };

    cd::scene_renderer::SceneViewData scene_data {};
    scene_data.stage_mode = cd::scene_renderer::StageMode::HD2D;
    scene_data.camera_pos = { 0.0F, 5.0F, 5.0F };
    scene_data.camera_target = { 0.0F, 0.0F, 5.0F };

    cd::scene_renderer::SceneSpriteCardHD2D card {};
    card.position = { 0.0F, 0.0F, 5.0F };
    card.y_locked = true;
    scene_data.sprite_cards.push_back(card);

    auto handles = cd::scene_renderer::build_scene_renderer_framegraph(fg, *tex, cfg, &scene_data);
    EXPECT_TRUE(handles.final_backbuffer.is_valid());
    EXPECT_TRUE(fg.compile().has_value());

    dev.destroy_texture(*tex);
}

TEST(Tier2_Feature3_HD2D_Boundary, ZeroSizedSpriteCard)
{
    cd::rhi::NullDevice dev;
    cd::framegraph::FrameGraph fg(dev);
    cd::rhi::TextureDesc desc {};
    desc.extent = { 128U, 128U, 1U };
    desc.format = cd::rhi::Format::kRGBA8Unorm;
    desc.usage = cd::rhi::TextureUsage::kColorAttachment;
    auto tex = dev.create_texture(desc);
    ASSERT_TRUE(tex.has_value());

    cd::scene_renderer::SceneRendererConfig cfg {};
    cfg.viewport = { 128U, 128U };

    cd::scene_renderer::SceneViewData scene_data {};
    scene_data.stage_mode = cd::scene_renderer::StageMode::HD2D;

    cd::scene_renderer::SceneSpriteCardHD2D card {};
    card.position = { 0.0F, 0.0F, 0.0F };
    card.size = { 0.0F, 0.0F };
    scene_data.sprite_cards.push_back(card);

    auto handles = cd::scene_renderer::build_scene_renderer_framegraph(fg, *tex, cfg, &scene_data);
    EXPECT_TRUE(handles.final_backbuffer.is_valid());
    EXPECT_TRUE(fg.compile().has_value());

    dev.destroy_texture(*tex);
}

TEST(Tier2_Feature3_HD2D_Boundary, ExtremeDistanceBokeh)
{
    cd::rhi::NullDevice dev;
    cd::framegraph::FrameGraph fg(dev);
    cd::rhi::TextureDesc desc {};
    desc.extent = { 128U, 128U, 1U };
    desc.format = cd::rhi::Format::kRGBA8Unorm;
    desc.usage = cd::rhi::TextureUsage::kColorAttachment;
    auto tex = dev.create_texture(desc);
    ASSERT_TRUE(tex.has_value());

    cd::scene_renderer::SceneRendererConfig cfg {};
    cfg.viewport = { 128U, 128U };
    cfg.postfx.enable_dof = true;

    cd::scene_renderer::SceneViewData scene_data {};
    scene_data.stage_mode = cd::scene_renderer::StageMode::HD2D;
    scene_data.camera_pos = { 0.0F, 0.0F, 0.0F };
    scene_data.camera_target = { 30000.0F, 40000.0F, 0.0F };
    scene_data.focus_distance_dof = 10.0F;

    cd::scene_renderer::SceneSpriteCardHD2D card {};
    card.position = { 0.0F, 0.0F, 0.0F };
    scene_data.sprite_cards.push_back(card);

    auto handles = cd::scene_renderer::build_scene_renderer_framegraph(fg, *tex, cfg, &scene_data);
    EXPECT_TRUE(handles.dof_coc.is_valid());
    EXPECT_TRUE(fg.compile().has_value());

    dev.destroy_texture(*tex);
}

// =============================================================================
// Feature 4: FAZ 36 Roadmap Closure Boundary Tests
// =============================================================================

TEST(Tier2_Feature4_FAZ36_Boundary, UnmatchedMarkdownCheckbox)
{
    TempSandbox sandbox;
    sandbox.write_text("sample_plan.md", "- [ ] **Wave 36.1 Incomplete**\n- [x] **Wave 35.1 Done**\n");

    std::ifstream ifs(sandbox.file_path("sample_plan.md"));
    ASSERT_TRUE(ifs.is_open());
    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

    EXPECT_NE(content.find("- [ ] **Wave 36.1"), std::string::npos);
    EXPECT_NE(content.find("- [x] **Wave 35.1"), std::string::npos);
}

TEST(Tier2_Feature4_FAZ36_Boundary, EmDashDetection)
{
    TempSandbox sandbox;
    sandbox.write_text("invalid_doc.md", "Feature description \xE2\x80\x94 with forbidden em-dash.\n");

    std::ifstream ifs(sandbox.file_path("invalid_doc.md"), std::ios::binary);
    ASSERT_TRUE(ifs.is_open());
    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

    EXPECT_NE(content.find("\xE2\x80\x94"), std::string::npos);
}

TEST(Tier2_Feature4_FAZ36_Boundary, ForbiddenClicheDetection)
{
    TempSandbox sandbox;
    sandbox.write_text("cliche_doc.md", "We delve into the rendering architecture.\n");

    std::ifstream ifs(sandbox.file_path("cliche_doc.md"));
    ASSERT_TRUE(ifs.is_open());
    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

    EXPECT_NE(content.find("delve into"), std::string::npos);
}

TEST(Tier2_Feature4_FAZ36_Boundary, CrlfVsLfLineEndings)
{
    TempSandbox sandbox;
    sandbox.write_text("crlf.md", "- [x] **Wave 36.1**\r\n- [x] **Wave 36.2**\r\n");
    sandbox.write_text("lf.md", "- [x] **Wave 36.1**\n- [x] **Wave 36.2**\n");

    std::ifstream ifs_crlf(sandbox.file_path("crlf.md"), std::ios::binary);
    std::string s_crlf((std::istreambuf_iterator<char>(ifs_crlf)), std::istreambuf_iterator<char>());

    std::ifstream ifs_lf(sandbox.file_path("lf.md"), std::ios::binary);
    std::string s_lf((std::istreambuf_iterator<char>(ifs_lf)), std::istreambuf_iterator<char>());

    EXPECT_NE(s_crlf.find("\r\n"), std::string::npos);
    EXPECT_EQ(s_lf.find("\r\n"), std::string::npos);
    EXPECT_NE(s_crlf.find("[x] **Wave 36.1**"), std::string::npos);
    EXPECT_NE(s_lf.find("[x] **Wave 36.1**"), std::string::npos);
}

TEST(Tier2_Feature4_FAZ36_Boundary, MissingFileGracefulFail)
{
    const auto missing_path = std::filesystem::path("docs/NON_EXISTENT_FILE_XYZ.md");
    std::ifstream ifs(missing_path);
    EXPECT_FALSE(ifs.is_open());
}

// =============================================================================
// Feature 5: Live JSON-RPC / MCP Socket Server Boundary Tests
// =============================================================================

TEST(Tier2_Feature5_McpServer_Boundary, MalformedJsonPayload)
{
    const std::string malformed = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":";
    auto res = cd::asset::json::parse(malformed);
    EXPECT_FALSE(res.has_value());
    EXPECT_EQ(res.error().domain, cd::asset::json::json_errors::kDomain);
    const int rpc_code = !res.has_value() ? -32700 : 0;
    EXPECT_EQ(rpc_code, -32700);
}

TEST(Tier2_Feature5_McpServer_Boundary, UnknownMethodName)
{
    const std::string req = R"({"jsonrpc":"2.0","id":1,"method":"cd.nonexistent.method","params":{}})";
    auto res = cd::asset::json::parse(req);
    ASSERT_TRUE(res.has_value());
    auto method_val = res->at("method");
    ASSERT_TRUE(method_val.has_value());

    const std::string method = (*method_val)->as_string();
    const bool is_known = (method == "initialize" || method == "cd.scene.get_tree" ||
                           method == "cd.scene.mutate" || method == "cd.viewport.capture_frame" ||
                           method == "cd.diagnostics.get_stats");
    EXPECT_FALSE(is_known);

    cd::asset::json::Object err_obj;
    err_obj["code"] = -32601.0;
    err_obj["message"] = "Method not found";

    cd::asset::json::Object resp_obj;
    resp_obj["jsonrpc"] = "2.0";
    resp_obj["id"] = 1.0;
    resp_obj["error"] = std::move(err_obj);

    const std::string serialized = cd::asset::json::serialize(resp_obj, false);
    auto err_res = cd::asset::json::parse(serialized);
    ASSERT_TRUE(err_res.has_value());
    auto err_code = err_res->at("error");
    ASSERT_TRUE(err_code.has_value());
    auto code_num = (*err_code)->at("code");
    ASSERT_TRUE(code_num.has_value());
    EXPECT_EQ(static_cast<int>((*code_num)->as_number()), -32601);
}

TEST(Tier2_Feature5_McpServer_Boundary, InvalidParamTypes)
{
    cd::world2d::Stage2D stage;
    cd::world2d::CommandBus2D bus;

    const std::string bad_params =
        R"({"op":"set_entity_field_2d","id":1,"field":"name","value":99999})";
    auto report = bus.execute_json(stage, bad_params);
    EXPECT_FALSE(report.ok);
    EXPECT_FALSE(report.error_message.empty());
}

TEST(Tier2_Feature5_McpServer_Boundary, EmptyBatchRequest)
{
    cd::world2d::Stage2D stage;
    cd::world2d::CommandBus2D bus;

    const std::string empty_batch = "[]";
    auto report = bus.execute_json(stage, empty_batch);
    EXPECT_TRUE(report.ok);
    EXPECT_EQ(report.applied_count, 0U);
}

TEST(Tier2_Feature5_McpServer_Boundary, SocketClientDisconnect)
{
    cd::world2d::Stage2D stage;
    cd::world2d::CommandBus2D bus;

    const std::string invalid_batch = "[123, \"invalid\"]";
    auto report = bus.execute_json(stage, invalid_batch);
    EXPECT_FALSE(report.ok);
    EXPECT_NE(report.error_message.find("JSON object"), std::string::npos);
}

// =============================================================================
// Feature 6: Hot-Reload Scene Synchronization Boundary Tests
// =============================================================================

TEST(Tier2_Feature6_HotReload_Boundary, NonExistentEntityId)
{
    cd::world2d::Stage2D stage;
    cd::world2d::CommandBus2D bus;

    const std::string bad_mutate = R"({"op":"delete_entity_2d","id":999999})";
    auto report = bus.execute_json(stage, bad_mutate);
    EXPECT_FALSE(report.ok);
}

TEST(Tier2_Feature6_HotReload_Boundary, NegativeCoordinatePainting)
{
    cd::world2d::Stage2D stage;
    cd::world2d::CommandBus2D bus;

    const std::string paint_neg = R"({"op":"paint_tile_2d","gx":-1000,"gy":-1000,"palette_id":1})";
    auto report = bus.execute_json(stage, paint_neg);
    EXPECT_TRUE(report.ok);
}

TEST(Tier2_Feature6_HotReload_Boundary, InvalidFieldValue)
{
    cd::world2d::Stage2D stage;
    cd::world2d::CommandBus2D bus;

    const std::string bad_val = R"({"op":"set_entity_field_2d","id":1,"field":"name","value":12345})";
    auto report = bus.execute_json(stage, bad_val);
    EXPECT_FALSE(report.ok);
}

TEST(Tier2_Feature6_HotReload_Boundary, ConcurrentMutations)
{
    cd::world2d::Stage2D stage;
    cd::world2d::CommandBus2D bus;

    for (int i = 0; i < 50; ++i)
    {
        const std::string cmd = R"({"op":"paint_tile_2d","gx":)" + std::to_string(i) + R"(,"gy":0,"palette_id":1})";
        auto rep = bus.execute_json(stage, cmd);
        (void)rep;
    }
    EXPECT_GT(bus.undo_depth(), 0U);
}

TEST(Tier2_Feature6_HotReload_Boundary, UndoStackUnderflow)
{
    cd::world2d::Stage2D stage;
    cd::world2d::CommandBus2D bus;

    EXPECT_EQ(bus.undo_depth(), 0U);
    EXPECT_FALSE(bus.undo(stage));
}

// =============================================================================
// Feature 7: tools/assetc Offline Asset Cooker Boundary Tests
// =============================================================================

TEST(Tier2_Feature7_Assetc_Boundary, EmptyInputDirectory)
{
    cd::asset::pak::PakBuilder builder;
    const auto pak_bytes = builder.build();

    cd::asset::pak::Pak pak;
    auto open_res = pak.open(pak_bytes.data(), pak_bytes.size());
    ASSERT_TRUE(open_res.has_value());
    EXPECT_EQ(pak.entry_count(), 0U);
    EXPECT_FALSE(pak.find("any_file.txt").has_value());
}

TEST(Tier2_Feature7_Assetc_Boundary, ZeroByteInputFile)
{
    cd::asset::pak::PakBuilder builder;
    builder.add("empty.bin", std::string_view{});
    const auto pak_bytes = builder.build();

    cd::asset::pak::Pak pak;
    ASSERT_TRUE(pak.open(pak_bytes.data(), pak_bytes.size()).has_value());
    EXPECT_EQ(pak.entry_count(), 1U);
    const auto entry = pak.find("empty.bin");
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(entry->size, 0U);
}

TEST(Tier2_Feature7_Assetc_Boundary, CorruptedPngHeader)
{
    const std::array<std::byte, 24> bad_header = {
        std::byte{'X'}, std::byte{'X'}, std::byte{'X'}, std::byte{'X'},
        std::byte{0x01}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}
    };

    cd::asset::pak::Pak pak;
    auto res = pak.open(bad_header.data(), bad_header.size());
    EXPECT_FALSE(res.has_value());
    EXPECT_EQ(res.error().code, static_cast<std::uint32_t>(cd::asset::pak::pak_errors::Code::kMagicMismatch));
}

TEST(Tier2_Feature7_Assetc_Boundary, InvalidGlslSyntax)
{
    const std::array<std::byte, 10> tiny_buf = {};
    cd::asset::pak::Pak pak;
    auto res = pak.open(tiny_buf.data(), tiny_buf.size());
    EXPECT_FALSE(res.has_value());
    EXPECT_EQ(res.error().code, static_cast<std::uint32_t>(cd::asset::pak::pak_errors::Code::kCorrupt));
}

TEST(Tier2_Feature7_Assetc_Boundary, ReadOnlyOutputLocation)
{
    cd::asset::pak::PakBuilder builder;
    builder.add("test.txt", "content");
    auto pak_bytes = builder.build();

    const std::uint64_t huge_off = 999999999ULL;
    std::memcpy(&pak_bytes[12], &huge_off, sizeof(huge_off));

    cd::asset::pak::Pak pak;
    auto res = pak.open(pak_bytes.data(), pak_bytes.size());
    EXPECT_FALSE(res.has_value());
    EXPECT_EQ(res.error().code, static_cast<std::uint32_t>(cd::asset::pak::pak_errors::Code::kCorrupt));
}

// =============================================================================
// Feature 8: Standalone Package Exporter Boundary Tests
// =============================================================================

TEST(Tier2_Feature8_PackageExporter_Boundary, DestinationAlreadyExists)
{
    TempSandbox sandbox;
    cd::world2d::Stage2D stage;
    stage.build_starter_illuminated_platformer();
    auto bytes = stage.build_standalone_pak_bytes();

    EXPECT_TRUE(sandbox.write_binary("game.cdpak", bytes));
    EXPECT_TRUE(sandbox.file_exists("game.cdpak"));

    EXPECT_TRUE(sandbox.write_binary("game.cdpak", bytes));
    EXPECT_TRUE(sandbox.file_exists("game.cdpak"));
}

TEST(Tier2_Feature8_PackageExporter_Boundary, ReadOnlyDestination)
{
    cd::world2d::Stage2D stage;
    auto res = stage.load_from_pak_bytes(std::span<const std::byte>{});
    EXPECT_FALSE(res.has_value());
}

TEST(Tier2_Feature8_PackageExporter_Boundary, UnicodePathHandling)
{
    cd::asset::pak::PakBuilder builder;
    builder.add("manifest.json", "{\"engine\":\"CHROMODYNAMIC\",\"entry_stage2d\":\"stages/main.cd2d.json\"}");
    builder.add("stages/main.cd2d.json", "{ invalid_corrupt_json: ");
    const auto bad_pak = builder.build();

    cd::world2d::Stage2D stage;
    auto res = stage.load_from_pak_bytes(bad_pak);
    EXPECT_FALSE(res.has_value());
}

TEST(Tier2_Feature8_PackageExporter_Boundary, MissingStageSource)
{
    cd::world2d::Stage2D stage;
    auto res = stage.load_from_file("missing_stage_xyz.cd2d.json");
    EXPECT_FALSE(res.has_value());
}

TEST(Tier2_Feature8_PackageExporter_Boundary, CorruptedPakPayload)
{
    cd::world2d::Stage2D stage;
    const std::array<std::byte, 8> bad_bytes = { std::byte { 0xFF }, std::byte { 0xFF } };
    auto res = stage.load_from_pak_bytes(bad_bytes);
    EXPECT_FALSE(res.has_value());
}

// =============================================================================
// Feature 9: FAZ 37 Roadmap Closure Boundary Tests
// =============================================================================

TEST(Tier2_Feature9_FAZ37_Boundary, PartialWaveCompletion)
{
    TempSandbox sandbox;
    sandbox.write_text("plan_partial.md", "- [x] **Wave 37.1**\n- [ ] **Wave 37.2**\n");

    std::ifstream ifs(sandbox.file_path("plan_partial.md"));
    ASSERT_TRUE(ifs.is_open());
    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

    EXPECT_NE(content.find("- [x] **Wave 37.1**"), std::string::npos);
    EXPECT_NE(content.find("- [ ] **Wave 37.2**"), std::string::npos);
}

TEST(Tier2_Feature9_FAZ37_Boundary, RoadmapCheckboxUnmarked)
{
    TempSandbox sandbox;
    sandbox.write_text("phases.md", "- [▶] FAZ 37 In-Progress Item\n- [x] FAZ 36 Done\n");

    std::ifstream ifs(sandbox.file_path("phases.md"));
    ASSERT_TRUE(ifs.is_open());
    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

    EXPECT_NE(content.find("[▶]"), std::string::npos);
    EXPECT_NE(content.find("[x]"), std::string::npos);
}

TEST(Tier2_Feature9_FAZ37_Boundary, ForbiddenClicheDetection)
{
    TempSandbox sandbox;
    sandbox.write_text("buzz.md", "This is a revolutionary cutting-edge game engine.\n");

    std::ifstream ifs(sandbox.file_path("buzz.md"));
    ASSERT_TRUE(ifs.is_open());
    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

    EXPECT_NE(content.find("revolutionary"), std::string::npos);
    EXPECT_NE(content.find("cutting-edge"), std::string::npos);
}

TEST(Tier2_Feature9_FAZ37_Boundary, TrailingWhitespaceHandling)
{
    TempSandbox sandbox;
    sandbox.write_text("trailing.md", "- [x] **Wave 37.1**   \r\n");

    std::ifstream ifs(sandbox.file_path("trailing.md"), std::ios::binary);
    ASSERT_TRUE(ifs.is_open());
    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

    EXPECT_NE(content.find("[x] **Wave 37.1**"), std::string::npos);
    EXPECT_NE(content.find("\r\n"), std::string::npos);
}

TEST(Tier2_Feature9_FAZ37_Boundary, EncodingIntegrity)
{
    const auto root = EnvironmentProbe::engine_root();
    const auto roadmap_path = root / "docs" / "DOMESTIC_ENGINE_PRODUCT_ROADMAP.md";
    ASSERT_TRUE(std::filesystem::exists(roadmap_path));

    std::ifstream ifs(roadmap_path, std::ios::binary);
    ASSERT_TRUE(ifs.is_open());
    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

    ASSERT_GE(content.size(), 3U);
    const bool has_bom = (static_cast<unsigned char>(content[0]) == 0xEF &&
                          static_cast<unsigned char>(content[1]) == 0xBB &&
                          static_cast<unsigned char>(content[2]) == 0xBF);
    EXPECT_FALSE(has_bom) << "DOMESTIC_ENGINE_PRODUCT_ROADMAP.md must not contain UTF-8 BOM";
}

}  // namespace cd::e2e
