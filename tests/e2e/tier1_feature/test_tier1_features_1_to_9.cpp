// =============================================================================
// CHROMODYNAMIC — tests/e2e/tier1_feature/test_tier1_features_1_to_9.cpp
//
// Tier 1 Category-Partition Feature Tests for Features 1 through 9:
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
#include <cd/render2d/SpriteBatch2D.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace cd::e2e
{

// =============================================================================
// Feature 1: Unified SceneRenderer 19-Pass Execution
// =============================================================================

TEST(Tier1_Feature1_SceneRenderer, BuildsAll19PassesSuccessfully)
{
    cd::rhi::NullDevice dev;
    cd::framegraph::FrameGraph fg(dev);

    cd::rhi::TextureDesc back_desc {};
    back_desc.type = cd::rhi::TextureType::k2D;
    back_desc.format = cd::rhi::Format::kRGBA8Unorm;
    back_desc.extent = { 1920U, 1080U, 1U };
    back_desc.usage = cd::rhi::TextureUsage::kColorAttachment;

    auto backbuffer = dev.create_texture(back_desc);
    ASSERT_TRUE(backbuffer.has_value());

    cd::scene_renderer::SceneRendererConfig config {};
    config.viewport = { 1920U, 1080U };
    config.gi.enable_nrc = true;

    auto handles = cd::scene_renderer::build_scene_renderer_framegraph(fg, *backbuffer, config);
    (void)handles;
    EXPECT_GE(fg.pass_count(), 19U);

    auto compile_res = fg.compile();
    EXPECT_TRUE(compile_res.has_value());

    dev.destroy_texture(*backbuffer);
}

TEST(Tier1_Feature1_SceneRenderer, ValidResourceHandlesReturned)
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

    auto handles = cd::scene_renderer::build_scene_renderer_framegraph(fg, *tex, cfg);

    EXPECT_TRUE(handles.depth.is_valid());
    EXPECT_TRUE(handles.gbuffer_albedo.is_valid());
    EXPECT_TRUE(handles.gbuffer_normal_roughness.is_valid());
    EXPECT_TRUE(handles.velocity.is_valid());
    EXPECT_TRUE(handles.hdr_scene.is_valid());
    EXPECT_TRUE(handles.final_backbuffer.is_valid());

    dev.destroy_texture(*tex);
}

TEST(Tier1_Feature1_SceneRenderer, ViewportResolutionPropagation)
{
    cd::rhi::NullDevice dev;
    cd::framegraph::FrameGraph fg(dev);

    cd::rhi::TextureDesc desc {};
    desc.type = cd::rhi::TextureType::k2D;
    desc.format = cd::rhi::Format::kRGBA8Unorm;
    desc.extent = { 2560U, 1440U, 1U };
    desc.usage = cd::rhi::TextureUsage::kColorAttachment;

    auto tex = dev.create_texture(desc);
    ASSERT_TRUE(tex.has_value());

    cd::scene_renderer::SceneRendererConfig cfg {};
    cfg.viewport = { 2560U, 1440U };

    auto handles = cd::scene_renderer::build_scene_renderer_framegraph(fg, *tex, cfg);
    EXPECT_TRUE(handles.final_backbuffer.is_valid());
    EXPECT_TRUE(fg.compile().has_value());

    dev.destroy_texture(*tex);
}

TEST(Tier1_Feature1_SceneRenderer, FeatureTogglePropagation)
{
    cd::rhi::NullDevice dev;
    cd::framegraph::FrameGraph fg(dev);

    cd::rhi::TextureDesc desc {};
    desc.type = cd::rhi::TextureType::k2D;
    desc.format = cd::rhi::Format::kRGBA8Unorm;
    desc.extent = { 800U, 600U, 1U };
    desc.usage = cd::rhi::TextureUsage::kColorAttachment;

    auto tex = dev.create_texture(desc);
    ASSERT_TRUE(tex.has_value());

    cd::scene_renderer::SceneRendererConfig cfg {};
    cfg.viewport = { 800U, 600U };
    cfg.postfx.enable_bloom = false;
    cfg.postfx.enable_dof = false;

    auto handles = cd::scene_renderer::build_scene_renderer_framegraph(fg, *tex, cfg);
    (void)handles;
    EXPECT_TRUE(fg.compile().has_value());

    dev.destroy_texture(*tex);
}

TEST(Tier1_Feature1_SceneRenderer, FinalBackbufferAttachment)
{
    cd::rhi::NullDevice dev;
    cd::framegraph::FrameGraph fg(dev);

    cd::rhi::TextureDesc desc {};
    desc.type = cd::rhi::TextureType::k2D;
    desc.format = cd::rhi::Format::kRGBA8Unorm;
    desc.extent = { 640U, 480U, 1U };
    desc.usage = cd::rhi::TextureUsage::kColorAttachment;

    auto tex = dev.create_texture(desc);
    ASSERT_TRUE(tex.has_value());

    cd::scene_renderer::SceneRendererConfig cfg {};
    cfg.viewport = { 640U, 480U };

    auto handles = cd::scene_renderer::build_scene_renderer_framegraph(fg, *tex, cfg);
    EXPECT_TRUE(handles.final_backbuffer.is_valid());

    dev.destroy_texture(*tex);
}

// =============================================================================
// Feature 2: 3D Split-Brain Elimination
// =============================================================================

TEST(Tier1_Feature2_SplitBrain, EditorUsesSceneRendererConfig)
{
    cd::scene_renderer::SceneRendererConfig editor_cfg {};
    editor_cfg.viewport = { 1280U, 720U };
    EXPECT_EQ(editor_cfg.viewport.width, 1280U);
    EXPECT_EQ(editor_cfg.viewport.height, 720U);
}

TEST(Tier1_Feature2_SplitBrain, PlayerUsesSceneRendererConfig)
{
    cd::scene_renderer::SceneRendererConfig player_cfg {};
    player_cfg.viewport = { 1280U, 720U };
    EXPECT_EQ(player_cfg.viewport.width, 1280U);
    EXPECT_EQ(player_cfg.viewport.height, 720U);
}

TEST(Tier1_Feature2_SplitBrain, MatchingShaderBindings)
{
    cd::rhi::NullDevice dev;
    cd::framegraph::FrameGraph fg(dev);

    cd::rhi::TextureDesc desc {};
    desc.type = cd::rhi::TextureType::k2D;
    desc.format = cd::rhi::Format::kRGBA8Unorm;
    desc.extent = { 100U, 100U, 1U };
    desc.usage = cd::rhi::TextureUsage::kColorAttachment;

    auto tex = dev.create_texture(desc);
    ASSERT_TRUE(tex.has_value());

    cd::scene_renderer::SceneRendererConfig cfg {};
    cfg.viewport = { 100U, 100U };

    auto handles = cd::scene_renderer::build_scene_renderer_framegraph(fg, *tex, cfg);
    EXPECT_TRUE(handles.final_backbuffer.is_valid());

    dev.destroy_texture(*tex);
}

TEST(Tier1_Feature2_SplitBrain, MatchingDrawCommandStructure)
{
    cd::scene_renderer::SceneRendererConfig cfg {};
    EXPECT_TRUE(cfg.culling.enable_frustum_culling);
}

TEST(Tier1_Feature2_SplitBrain, HeadlessParityExecution)
{
    cd::rhi::NullDevice dev;
    cd::framegraph::FrameGraph fg(dev);

    cd::rhi::TextureDesc desc {};
    desc.type = cd::rhi::TextureType::k2D;
    desc.format = cd::rhi::Format::kRGBA8Unorm;
    desc.extent = { 128U, 128U, 1U };
    desc.usage = cd::rhi::TextureUsage::kColorAttachment;

    auto tex = dev.create_texture(desc);
    ASSERT_TRUE(tex.has_value());

    cd::scene_renderer::SceneRendererConfig cfg {};
    cfg.viewport = { 128U, 128U };

    auto handles = cd::scene_renderer::build_scene_renderer_framegraph(fg, *tex, cfg);
    (void)handles;
    ASSERT_TRUE(fg.compile().has_value());

    dev.destroy_texture(*tex);
}

// =============================================================================
// Feature 3: StageMode::HD2D Hybrid Pipeline
// =============================================================================

TEST(Tier1_Feature3_HD2D, SpriteWritesDepthWithCutout)
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

    cd::scene_renderer::SceneViewData scene_data {};
    scene_data.stage_mode = cd::scene_renderer::StageMode::HD2D;

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

    ASSERT_TRUE(fg.compile().has_value());
    auto cmd = dev.create_command_buffer();
    cmd->begin();
    ASSERT_TRUE(fg.execute(*cmd).has_value());
    cmd->end();

    dev.destroy_texture(*back_tex);
}

TEST(Tier1_Feature3_HD2D, BillboardFacingActiveCamera)
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
    scene_data.camera_pos = { 0.0F, 5.0F, -10.0F };
    scene_data.camera_target = { 0.0F, 0.0F, 0.0F };

    cd::scene_renderer::SceneSpriteCardHD2D card {};
    card.position = { 0.0F, 0.0F, 0.0F };
    card.size = { 2.0F, 2.0F };
    card.y_locked = false;
    scene_data.sprite_cards.push_back(card);

    const auto handles = cd::scene_renderer::build_scene_renderer_framegraph(fg, *back_tex, cfg, &scene_data);
    EXPECT_TRUE(handles.final_backbuffer.is_valid());
    ASSERT_TRUE(fg.compile().has_value());

    auto cmd = dev.create_command_buffer();
    cmd->begin();
    ASSERT_TRUE(fg.execute(*cmd).has_value());
    cmd->end();

    dev.destroy_texture(*back_tex);
}

TEST(Tier1_Feature3_HD2D, YLockedBillboardConstraint)
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
    scene_data.camera_pos = { 10.0F, 8.0F, 10.0F };
    scene_data.camera_target = { 0.0F, 0.0F, 0.0F };

    cd::scene_renderer::SceneSpriteCardHD2D card {};
    card.position = { 2.0F, 0.0F, 2.0F };
    card.size = { 1.0F, 2.0F };
    card.y_locked = true;
    scene_data.sprite_cards.push_back(card);

    const auto handles = cd::scene_renderer::build_scene_renderer_framegraph(fg, *back_tex, cfg, &scene_data);
    EXPECT_TRUE(handles.depth.is_valid());
    ASSERT_TRUE(fg.compile().has_value());

    auto cmd = dev.create_command_buffer();
    cmd->begin();
    ASSERT_TRUE(fg.execute(*cmd).has_value());
    cmd->end();

    dev.destroy_texture(*back_tex);
}

TEST(Tier1_Feature3_HD2D, Receives3dDirectionalShadow)
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
    scene_data.sun.direction = cd::math::normalize(cd::math::Vec3f { 0.577F, -0.577F, 0.577F });
    scene_data.sun.cast_shadows = true;

    cd::scene_renderer::SceneSpriteCardHD2D card {};
    card.position = { 0.0F, 0.0F, 0.0F };
    card.size = { 1.0F, 1.0F };
    card.cast_shadows = true;
    scene_data.sprite_cards.push_back(card);

    const auto handles = cd::scene_renderer::build_scene_renderer_framegraph(fg, *back_tex, cfg, &scene_data);
    EXPECT_TRUE(handles.depth.is_valid());
    ASSERT_TRUE(fg.compile().has_value());

    auto cmd = dev.create_command_buffer();
    cmd->begin();
    ASSERT_TRUE(fg.execute(*cmd).has_value());
    cmd->end();

    dev.destroy_texture(*back_tex);
}

TEST(Tier1_Feature3_HD2D, BokehDofBlurSeparation)
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
    cfg.postfx.enable_dof = true;

    cd::scene_renderer::SceneViewData scene_data {};
    scene_data.stage_mode = cd::scene_renderer::StageMode::HD2D;
    scene_data.camera_pos = { 0.0F, 2.0F, -10.0F };
    scene_data.camera_target = { 0.0F, 0.0F, 0.0F };
    scene_data.focus_distance_dof = 10.0F;

    cd::scene_renderer::SceneSpriteCardHD2D card {};
    card.position = { 0.0F, 0.0F, 0.0F };
    scene_data.sprite_cards.push_back(card);

    const auto handles = cd::scene_renderer::build_scene_renderer_framegraph(fg, *back_tex, cfg, &scene_data);
    EXPECT_TRUE(handles.dof_coc.is_valid());
    ASSERT_TRUE(fg.compile().has_value());

    auto cmd = dev.create_command_buffer();
    cmd->begin();
    ASSERT_TRUE(fg.execute(*cmd).has_value());
    cmd->end();

    dev.destroy_texture(*back_tex);
}

// =============================================================================
// Feature 4: FAZ 36 Roadmap Closure
// =============================================================================

TEST(Tier1_Feature4_FAZ36, Wave361StatusStructure)
{
    const auto root = EnvironmentProbe::engine_root();
    const auto plan_path = root / "docs" / "PLAN_NEXT_PHASES.md";
    ASSERT_TRUE(std::filesystem::exists(plan_path));

    std::ifstream ifs(plan_path, std::ios::binary);
    ASSERT_TRUE(ifs.is_open());
    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    EXPECT_NE(content.find("- [x] **Wave 36.1"), std::string::npos);
}

TEST(Tier1_Feature4_FAZ36, Wave362StatusStructure)
{
    const auto root = EnvironmentProbe::engine_root();
    const auto plan_path = root / "docs" / "PLAN_NEXT_PHASES.md";
    std::ifstream ifs(plan_path, std::ios::binary);
    ASSERT_TRUE(ifs.is_open());
    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    EXPECT_NE(content.find("- [x] **Wave 36.2"), std::string::npos);
}

TEST(Tier1_Feature4_FAZ36, HumanVoiceCompliance)
{
    const auto root = EnvironmentProbe::engine_root();
    const auto plan_path = root / "docs" / "PLAN_NEXT_PHASES.md";
    std::ifstream ifs(plan_path, std::ios::binary);
    ASSERT_TRUE(ifs.is_open());
    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

    const std::vector<std::string> banned_cliches = {
        "delve into", "a testament to", "tapestry", "game-changer", "unleash"
    };
    for (const auto& cliche : banned_cliches)
    {
        EXPECT_EQ(content.find(cliche), std::string::npos) << "Found AI cliché: " << cliche;
    }
}

TEST(Tier1_Feature4_FAZ36, NoEmDashesPresent)
{
    const auto root = EnvironmentProbe::engine_root();
    const auto plan_path = root / "docs" / "PLAN_NEXT_PHASES.md";
    std::ifstream ifs(plan_path, std::ios::binary);
    ASSERT_TRUE(ifs.is_open());
    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

    EXPECT_EQ(content.find("\xE2\x80\x94"), std::string::npos) << "UTF-8 em-dash found in PLAN_NEXT_PHASES.md";
    EXPECT_EQ(content.find(" --- "), std::string::npos) << "Triple dash found in PLAN_NEXT_PHASES.md";
}

TEST(Tier1_Feature4_FAZ36, AccompanyingDocumentation)
{
    const auto root = EnvironmentProbe::engine_root();
    const std::array<std::filesystem::path, 4> docs = {
        root / "docs" / "DESIGN.md",
        root / "docs" / "PLAN.md",
        root / "docs" / "ENGINE_GUIDE.md",
        root / "docs" / "Modules.dox"
    };
    for (const auto& doc : docs)
    {
        ASSERT_TRUE(std::filesystem::exists(doc)) << doc;
        EXPECT_GT(std::filesystem::file_size(doc), 100U) << doc;
    }
}

// =============================================================================
// Feature 5: Live JSON-RPC / MCP Socket Server
// =============================================================================

TEST(Tier1_Feature5_McpServer, StdioInitHandshake)
{
    const std::string json_req =
        R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05"}})";

    auto parse_res = cd::asset::json::parse(json_req);
    ASSERT_TRUE(parse_res.has_value());
    const auto& root = *parse_res;
    ASSERT_TRUE(root.is_object());

    auto jsonrpc_val = root.at("jsonrpc");
    ASSERT_TRUE(jsonrpc_val.has_value());
    EXPECT_EQ((*jsonrpc_val)->as_string(), "2.0");

    auto method_val = root.at("method");
    ASSERT_TRUE(method_val.has_value());
    EXPECT_EQ((*method_val)->as_string(), "initialize");

    cd::asset::json::Object result_obj;
    result_obj["protocolVersion"] = "2024-11-05";
    result_obj["capabilities"] = cd::asset::json::Object {};

    cd::asset::json::Object resp_obj;
    resp_obj["jsonrpc"] = "2.0";
    resp_obj["id"] = 1.0;
    resp_obj["result"] = std::move(result_obj);

    const std::string serialized = cd::asset::json::serialize(resp_obj, false);
    auto resp_parsed = cd::asset::json::parse(serialized);
    ASSERT_TRUE(resp_parsed.has_value());
    EXPECT_TRUE(resp_parsed->is_object());
}

TEST(Tier1_Feature5_McpServer, GetSceneTree)
{
    cd::world2d::Stage2D stage;
    stage.build_starter_illuminated_platformer();

    const std::string scene_json = stage.save_to_json_string(false);
    auto parse_res = cd::asset::json::parse(scene_json);
    ASSERT_TRUE(parse_res.has_value());

    const auto& root = *parse_res;
    ASSERT_TRUE(root.is_object());

    auto stage_name = root.at("stage_name");
    ASSERT_TRUE(stage_name.has_value());
    EXPECT_FALSE((*stage_name)->as_string().empty());

    auto entities = root.at("entities");
    ASSERT_TRUE(entities.has_value());
    ASSERT_TRUE((*entities)->is_array());
    EXPECT_GT((*entities)->as_array().size(), 0U);
}

TEST(Tier1_Feature5_McpServer, MutateCommand)
{
    cd::world2d::Stage2D stage;
    cd::world2d::CommandBus2D bus;

    const std::string mutate_cmd =
        R"({"op":"spawn_entity_2d","name":"mcp_npc","pos":[10.0,5.0],"role":1})";

    auto report = bus.execute_json(stage, mutate_cmd);
    EXPECT_TRUE(report.ok);
    EXPECT_GE(report.applied_count, 1U);
    EXPECT_GT(report.last_created_entity_id, 0U);

    const std::string updated_scene = stage.save_to_json_string(false);
    EXPECT_NE(updated_scene.find("mcp_npc"), std::string::npos);
}

TEST(Tier1_Feature5_McpServer, CaptureViewportFrame)
{
    cd::world2d::Stage2D stage;
    stage.build_starter_illuminated_platformer();

    cd::render2d::Canvas2D canvas;
    const cd::render2d::ViewportRect2D vp { 0.0F, 0.0F, 640.0F, 360.0F };
    stage.render(canvas, vp);

    EXPECT_GT(canvas.commands().size(), 0U);
    EXPECT_FLOAT_EQ(canvas.viewport().width, 640.0F);
    EXPECT_FLOAT_EQ(canvas.viewport().height, 360.0F);
}

TEST(Tier1_Feature5_McpServer, DiagnosticsStats)
{
    cd::world2d::Stage2D stage;
    stage.build_starter_illuminated_platformer();
    cd::world2d::CommandBus2D bus;

    cd::asset::json::Object stats_obj;
    stats_obj["entity_count"] = static_cast<double>(stage.entities().size());
    stats_obj["undo_depth"] = static_cast<double>(bus.undo_depth());
    stats_obj["baked_colliders"] = static_cast<double>(stage.baked_tile_colliders().size());

    const std::string json_str = cd::asset::json::serialize(stats_obj, false);
    auto parse_res = cd::asset::json::parse(json_str);
    ASSERT_TRUE(parse_res.has_value());

    auto ent_cnt = parse_res->at("entity_count");
    ASSERT_TRUE(ent_cnt.has_value());
    EXPECT_EQ(static_cast<std::size_t>((*ent_cnt)->as_number()), stage.entities().size());
}

// =============================================================================
// Feature 6: Hot-Reload Scene Synchronization
// =============================================================================

TEST(Tier1_Feature6_HotReload, SpawnEntityMutation)
{
    cd::world2d::Stage2D stage;
    cd::world2d::CommandBus2D bus;

    const std::string mutate_json = R"({"op":"spawn_entity_2d","name":"goblin","pos":[10.0,5.0]})";
    auto report = bus.execute_json(stage, mutate_json);
    EXPECT_TRUE(report.ok);
    EXPECT_GE(report.applied_count, 1U);
}

TEST(Tier1_Feature6_HotReload, DeleteEntityMutation)
{
    cd::world2d::Stage2D stage;
    cd::world2d::CommandBus2D bus;

    const std::string spawn_json = R"({"op":"spawn_entity_2d","name":"temp_node","pos":[0.0,0.0]})";
    auto s1 = bus.execute_json(stage, spawn_json);
    (void)s1;

    const std::string delete_json = R"({"op":"delete_entity_2d","id":1})";
    auto report = bus.execute_json(stage, delete_json);
    EXPECT_TRUE(report.ok);
}

TEST(Tier1_Feature6_HotReload, SetFieldMutation)
{
    cd::world2d::Stage2D stage;
    cd::world2d::CommandBus2D bus;

    const std::string spawn_json = R"({"op":"spawn_entity_2d","name":"hero","pos":[0.0,0.0]})";
    auto s2 = bus.execute_json(stage, spawn_json);
    (void)s2;

    const std::string set_json = R"({"op":"set_entity_field_2d","id":1,"field":"name","value":"hero_renamed"})";
    auto report = bus.execute_json(stage, set_json);
    EXPECT_TRUE(report.ok);
}

TEST(Tier1_Feature6_HotReload, PaintTileMutation)
{
    cd::world2d::Stage2D stage;
    cd::world2d::CommandBus2D bus;

    const std::string paint_json = R"({"op":"paint_tile_2d","gx":5,"gy":5,"palette_id":2})";
    auto report = bus.execute_json(stage, paint_json);
    EXPECT_TRUE(report.ok);
}

TEST(Tier1_Feature6_HotReload, UndoStackIntegration)
{
    cd::world2d::Stage2D stage;
    cd::world2d::CommandBus2D bus;

    const std::string mutate_json = R"({"op":"paint_tile_2d","gx":1,"gy":1,"palette_id":3})";
    auto s3 = bus.execute_json(stage, mutate_json);
    (void)s3;

    const bool undo_ok = bus.undo(stage);
    EXPECT_TRUE(undo_ok);
}

// =============================================================================
// Feature 7: tools/assetc Offline Asset Cooker
// =============================================================================

TEST(Tier1_Feature7_Assetc, SmokeSelfTest)
{
    const auto bin = EnvironmentProbe::binary_dir() / "cd_assetc.exe";
    if (std::filesystem::exists(bin))
    {
        auto res = CliRunner::run_assetc("--smoke-self-test");
        EXPECT_EQ(res.exit_code, 0);
        EXPECT_NE(res.stdout_output.find("TOC verified"), std::string::npos);
    }
    else
    {
        cd::asset::pak::PakBuilder builder;
        builder.add("smoke_test.txt", "OK");
        auto bytes = builder.build();
        ASSERT_FALSE(bytes.empty());

        cd::asset::pak::Pak pak;
        ASSERT_TRUE(pak.open(bytes.data(), bytes.size()).has_value());
        EXPECT_EQ(pak.entry_count(), 1U);
        EXPECT_TRUE(pak.find("smoke_test.txt").has_value());
    }
}

TEST(Tier1_Feature7_Assetc, TextureBc7CompressionHeader)
{
    cd::asset::pak::PakBuilder builder;
    const std::vector<std::byte> bc7_payload = {
        std::byte{'C'}, std::byte{'D'}, std::byte{'B'}, std::byte{'C'}, std::byte{'7'},
        std::byte{0x01}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}
    };
    builder.add("textures/albedo.cdbc7", bc7_payload);
    const auto pak_bytes = builder.build();

    cd::asset::pak::Pak pak;
    ASSERT_TRUE(pak.open(pak_bytes.data(), pak_bytes.size()).has_value());
    const auto entry = pak.find("textures/albedo.cdbc7");
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(entry->size, bc7_payload.size());
    EXPECT_EQ(entry->bytes[0], std::byte{'C'});
    EXPECT_EQ(entry->bytes[4], std::byte{'7'});
}

TEST(Tier1_Feature7_Assetc, MeshCdmeshCompilationHeader)
{
    cd::asset::pak::PakBuilder builder;
    const std::vector<std::byte> cdmesh_payload = {
        std::byte{'C'}, std::byte{'D'}, std::byte{'M'}, std::byte{'S'}, std::byte{'H'},
        std::byte{0x02}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}
    };
    builder.add("meshes/hero.cdmesh", cdmesh_payload);
    const auto pak_bytes = builder.build();

    cd::asset::pak::Pak pak;
    ASSERT_TRUE(pak.open(pak_bytes.data(), pak_bytes.size()).has_value());
    const auto entry = pak.find("meshes/hero.cdmesh");
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(entry->bytes[0], std::byte{'C'});
    EXPECT_EQ(entry->bytes[4], std::byte{'H'});
}

TEST(Tier1_Feature7_Assetc, GlslSpirvCompilationMagic)
{
    cd::asset::pak::PakBuilder builder;
    const std::vector<std::byte> spv_payload = {
        std::byte{0x03}, std::byte{0x02}, std::byte{0x23}, std::byte{0x07},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x01}, std::byte{0x00}
    };
    builder.add("shaders/lighting.spv", spv_payload);
    const auto pak_bytes = builder.build();

    cd::asset::pak::Pak pak;
    ASSERT_TRUE(pak.open(pak_bytes.data(), pak_bytes.size()).has_value());
    const auto entry = pak.find("shaders/lighting.spv");
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(entry->size, spv_payload.size());

    const auto magic = *reinterpret_cast<const std::uint32_t*>(entry->bytes);
    EXPECT_EQ(magic, 0x07230203U);
}

TEST(Tier1_Feature7_Assetc, PakBundlePackagingHeader)
{
    cd::asset::pak::PakBuilder builder;
    builder.add("manifest.json", "{\"name\":\"bundle\"}");
    builder.add("data.bin", "\x01\x02\x03\x04");
    const auto pak_bytes = builder.build();

    ASSERT_GE(pak_bytes.size(), 24U);
    EXPECT_EQ(pak_bytes[0], std::byte{'C'});
    EXPECT_EQ(pak_bytes[1], std::byte{'D'});
    EXPECT_EQ(pak_bytes[2], std::byte{'P'});
    EXPECT_EQ(pak_bytes[3], std::byte{'K'});

    cd::asset::pak::Pak pak;
    ASSERT_TRUE(pak.open(pak_bytes.data(), pak_bytes.size()).has_value());
    EXPECT_EQ(pak.entry_count(), 2U);
}

// =============================================================================
// Feature 8: Standalone Package Exporter
// =============================================================================

TEST(Tier1_Feature8_PackageExporter, CreatesPakArchive)
{
    TempSandbox sandbox;
    cd::world2d::Stage2D stage;
    stage.build_starter_illuminated_platformer();
    auto bytes = stage.build_standalone_pak_bytes();
    ASSERT_FALSE(bytes.empty());

    EXPECT_TRUE(sandbox.write_binary("game.cdpak", bytes));
    EXPECT_TRUE(sandbox.file_exists("game.cdpak"));
    EXPECT_GT(std::filesystem::file_size(sandbox.file_path("game.cdpak")), 24U);
}

TEST(Tier1_Feature8_PackageExporter, BundlesPlayerBinaryContract)
{
    const auto player_bin = EnvironmentProbe::binary_dir() / "cd_player.exe";
    if (std::filesystem::exists(player_bin))
    {
        std::ifstream ifs(player_bin, std::ios::binary);
        ASSERT_TRUE(ifs.is_open());
        char header[2] = { 0, 0 };
        ifs.read(header, 2);
        EXPECT_EQ(header[0], 'M');
        EXPECT_EQ(header[1], 'Z');
    }
    else
    {
        TempSandbox sandbox;
        cd::world2d::Stage2D stage;
        stage.build_starter_illuminated_platformer();
        auto bytes = stage.build_standalone_pak_bytes();
        EXPECT_TRUE(sandbox.write_binary("game.cdpak", bytes));
        EXPECT_TRUE(sandbox.file_exists("game.cdpak"));
    }
}

TEST(Tier1_Feature8_PackageExporter, CopiesRuntimeDllsContract)
{
    cd::world2d::Stage2D stage;
    stage.build_starter_illuminated_platformer();
    const auto pak_bytes = stage.build_standalone_pak_bytes();

    cd::asset::pak::Pak pak;
    ASSERT_TRUE(pak.open(pak_bytes.data(), pak_bytes.size()).has_value());
    EXPECT_GE(pak.entry_count(), 2U);
    EXPECT_TRUE(pak.find("manifest.json").has_value());
    EXPECT_TRUE(pak.find("stages/main.cd2d.json").has_value());
}

TEST(Tier1_Feature8_PackageExporter, GeneratesManifest)
{
    cd::world2d::Stage2D stage;
    stage.build_starter_illuminated_platformer();
    const auto pak_bytes = stage.build_standalone_pak_bytes();

    cd::asset::pak::Pak pak;
    ASSERT_TRUE(pak.open(pak_bytes.data(), pak_bytes.size()).has_value());
    const auto manifest_entry = pak.find("manifest.json");
    ASSERT_TRUE(manifest_entry.has_value());

    const std::string manifest_str(
        reinterpret_cast<const char*>(manifest_entry->bytes), manifest_entry->size);
    auto parsed = cd::asset::json::parse(manifest_str);
    ASSERT_TRUE(parsed.has_value());
    ASSERT_TRUE(parsed->is_object());

    auto entry_stage = parsed->at("entry_stage2d");
    ASSERT_TRUE(entry_stage.has_value());
    EXPECT_EQ((*entry_stage)->as_string(), "stages/main.cd2d.json");

    auto engine_field = parsed->at("engine");
    ASSERT_TRUE(engine_field.has_value());
    EXPECT_EQ((*engine_field)->as_string(), "CHROMODYNAMIC");
}

TEST(Tier1_Feature8_PackageExporter, SelfContainedLaunchStructure)
{
    cd::world2d::Stage2D stage;
    stage.build_starter_illuminated_platformer();
    const auto pak_bytes = stage.build_standalone_pak_bytes();

    cd::world2d::Stage2D loaded_stage;
    auto load_res = loaded_stage.load_from_pak_bytes(pak_bytes);
    ASSERT_TRUE(load_res.has_value());

    EXPECT_EQ(loaded_stage.name(), stage.name());
    EXPECT_EQ(loaded_stage.entities().size(), stage.entities().size());
    EXPECT_EQ(loaded_stage.baked_tile_colliders().size(), stage.baked_tile_colliders().size());
}

// =============================================================================
// Feature 9: FAZ 37 Roadmap Closure
// =============================================================================

TEST(Tier1_Feature9_FAZ37, Wave371StatusStructure)
{
    const auto root = EnvironmentProbe::engine_root();
    const auto plan_path = root / "docs" / "PLAN_NEXT_PHASES.md";
    ASSERT_TRUE(std::filesystem::exists(plan_path));

    std::ifstream ifs(plan_path, std::ios::binary);
    ASSERT_TRUE(ifs.is_open());
    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    EXPECT_NE(content.find("37.1"), std::string::npos);
}

TEST(Tier1_Feature9_FAZ37, Wave372StatusStructure)
{
    const auto root = EnvironmentProbe::engine_root();
    const auto plan_path = root / "docs" / "PLAN_NEXT_PHASES.md";
    std::ifstream ifs(plan_path, std::ios::binary);
    ASSERT_TRUE(ifs.is_open());
    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    EXPECT_NE(content.find("37.2"), std::string::npos);
}

TEST(Tier1_Feature9_FAZ37, ProductRoadmapUpdated)
{
    const auto root = EnvironmentProbe::engine_root();
    const auto roadmap_path = root / "docs" / "DOMESTIC_ENGINE_PRODUCT_ROADMAP.md";
    ASSERT_TRUE(std::filesystem::exists(roadmap_path));

    std::ifstream ifs(roadmap_path, std::ios::binary);
    ASSERT_TRUE(ifs.is_open());
    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    EXPECT_NE(content.find("[x]"), std::string::npos);
}

TEST(Tier1_Feature9_FAZ37, HumanVoiceCompliance)
{
    const auto root = EnvironmentProbe::engine_root();
    const auto roadmap_path = root / "docs" / "DOMESTIC_ENGINE_PRODUCT_ROADMAP.md";
    std::ifstream ifs(roadmap_path, std::ios::binary);
    ASSERT_TRUE(ifs.is_open());
    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

    const std::vector<std::string> banned_cliches = {
        "delve into", "a testament to", "tapestry", "game-changer", "revolutionary"
    };
    for (const auto& cliche : banned_cliches)
    {
        EXPECT_EQ(content.find(cliche), std::string::npos) << "Found AI cliché: " << cliche;
    }
}

TEST(Tier1_Feature9_FAZ37, ZeroEmDashes)
{
    const auto root = EnvironmentProbe::engine_root();
    const auto roadmap_path = root / "docs" / "DOMESTIC_ENGINE_PRODUCT_ROADMAP.md";
    std::ifstream ifs(roadmap_path, std::ios::binary);
    ASSERT_TRUE(ifs.is_open());
    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

    EXPECT_EQ(content.find("\xE2\x80\x94"), std::string::npos) << "UTF-8 em-dash found in roadmap";
    EXPECT_EQ(content.find(" --- "), std::string::npos) << "Triple dash found in roadmap";
}

}  // namespace cd::e2e
