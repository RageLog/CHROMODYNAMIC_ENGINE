// =============================================================================
// CHROMODYNAMIC — test_render2d.cpp
// Unit tests for cd::render2d (SortKey2D, Camera2D, Tilemap2D Wang Auto-Tiling
// & Greedy Collider Meshing, Lighting2D Soft Shadows, FlipbookAnimator2D, and
// .aseprite / .ldtk Importers).
// =============================================================================
#include <cd/render2d/Renderer2D.hpp>
#include <cd/render2d/ParallaxLayer2D.hpp>
#include <cd/render2d/TextureAtlas2D.hpp>

#include <gtest/gtest.h>

namespace
{

TEST(Render2DTest, SortKeyOrdersLayerZBiasAndTopDownYSortDeterministically)
{
    const auto bg_key  = cd::render2d::make_sort_key_2d(10U, 0, 0.0F, false, 1U);
    const auto far_y   = cd::render2d::make_sort_key_2d(100U, 0, 12.0F, true, 1U);
    const auto near_y  = cd::render2d::make_sort_key_2d(100U, 0, 2.0F, true, 1U);
    const auto fg_key  = cd::render2d::make_sort_key_2d(200U, 0, 50.0F, true, 1U);

    EXPECT_LT(bg_key, far_y);
    EXPECT_LT(far_y, near_y);  // Higher Y (further up) draws BEFORE lower Y (closer to viewer)
    EXPECT_LT(near_y, fg_key);
}

TEST(Render2DTest, Camera2DWorldScreenRoundTripAndSmoothFollow)
{
    cd::render2d::Camera2D cam {};
    cam.set_position({ 10.0F, 5.0F });
    cam.set_zoom(2.0F);
    const cd::render2d::ViewportRect2D vp { 100.0F, 50.0F, 800.0F, 600.0F };

    const cd::math::Vec2f world_pt { 13.25F, 7.5F };
    const cd::math::Vec2f screen_pt = cam.world_to_screen(world_pt, vp);
    const cd::math::Vec2f roundtrip = cam.screen_to_world(screen_pt, vp);

    EXPECT_NEAR(roundtrip.x, world_pt.x, 1.0e-3F);
    EXPECT_NEAR(roundtrip.y, world_pt.y, 1.0e-3F);

    for (int i = 0; i < 60; ++i)
    {
        cam.follow_target({ 20.0F, 8.0F }, { 4.0F, 0.0F }, 1.0F / 60.0F);
    }
    EXPECT_GT(cam.position().x, 18.5F);
    EXPECT_GT(cam.position().y, 7.0F);
}

TEST(Render2DTest, TilemapWangAutotileAndGreedyColliderMeshingMergeContiguousBlocks)
{
    cd::render2d::Tilemap2D map {};
    // Paint a solid 10x4 block across negative & positive chunk boundaries: [-3..6] x [0..3] = 40 tiles!
    map.paint_rect(-3, 0, 6, 3, 1U, true);
    EXPECT_EQ(map.non_empty_tile_count(), 40U);

    // Interior cell (0, 1) has all 4 cardinal neighbors -> mask == 15
    const auto interior = map.get_tile(0, 1);
    EXPECT_EQ(interior.autotile_mask, 15U);

    // Top-left corner (-3, 3) has East (+X) and South (-Y) neighbors only -> kEast | kSouth
    const auto top_left = map.get_tile(-3, 3);
    EXPECT_EQ(top_left.autotile_mask,
              static_cast<std::uint8_t>(cd::render2d::autotile_bits::kEast | cd::render2d::autotile_bits::kSouth));

    // Greedy collider mesher MUST collapse all 40 contiguous tiles into 1 single CollisionRect2D!
    const auto colliders = map.bake_greedy_collision_rects();
    ASSERT_EQ(colliders.size(), 1U);
    EXPECT_FLOAT_EQ(colliders.front().half_extents.x, 5.0F);
    EXPECT_FLOAT_EQ(colliders.front().half_extents.y, 2.0F);
}

TEST(Render2DTest, Lighting2DComputesSoftOccluderShadowsAndNormalShading)
{
    cd::render2d::LightingEnvironment2D env {};
    env.ambient_color         = { 0.10F, 0.10F, 0.10F };
    env.ambient_intensity     = 1.0F;
    env.directional_intensity = 0.0F;
    env.point_lights.push_back(cd::render2d::PointLight2D {
        .position     = { 0.0F, 5.0F },
        .color        = { 1.0F, 1.0F, 1.0F },
        .intensity    = 2.0F,
        .radius       = 15.0F,
        .cast_shadows = true,
    });
    env.occluders.push_back(cd::render2d::ShadowOccluder2D {
        .center       = { 4.0F, 5.0F },
        .half_extents = { 0.8F, 2.0F },
        .opacity      = 0.90F,
    });

    const auto unoccluded = cd::render2d::evaluate_lighting_2d(
        env, { 2.0F, 5.0F }, { 0.0F, 0.0F, 1.0F }, { 200U, 200U, 200U, 255U });
    const auto shadowed = cd::render2d::evaluate_lighting_2d(
        env, { 8.0F, 5.0F }, { 0.0F, 0.0F, 1.0F }, { 200U, 200U, 200U, 255U });

    EXPECT_GT(unoccluded.r, shadowed.r);
}

TEST(Render2DTest, ParsesAsepriteBinaryTagsAndImportsLdtkLevelJson)
{
    const std::vector<std::uint16_t> durations { 80U, 80U, 120U, 120U };
    const std::vector<cd::render2d::AsepriteTag2D> tags {
        { .name = "Idle", .from_frame = 0U, .to_frame = 1U, .direction = 0U },
        { .name = "Run",  .from_frame = 2U, .to_frame = 3U, .direction = 0U },
    };

    const auto blob   = cd::render2d::build_aseprite_header_and_tags_blob(64U, 64U, durations, tags);
    const auto parsed = cd::render2d::parse_aseprite_binary(blob);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->width, 64U);
    EXPECT_EQ(parsed->frame_count, 4U);
    ASSERT_EQ(parsed->tags.size(), 2U);

    const auto clips = parsed->build_horizontal_strip_clips();
    ASSERT_EQ(clips.size(), 2U);
    cd::render2d::FlipbookAnimator2D anim {};
    for (const auto& c : clips) anim.add_clip(c);
    ASSERT_TRUE(anim.play("Run"));
    anim.step(0.13F);
    EXPECT_EQ(anim.current_frame_index(), 1U);

    constexpr std::string_view kSampleLdtk = R"({
      "levels": [{
        "identifier": "Cavern_01",
        "pxWid": 64,
        "pxHei": 48,
        "layerInstances": [
          {
            "__gridSize": 16,
            "__cWid": 4,
            "__cHei": 3,
            "intGridCsv": [
              0, 0, 0, 0,
              0, 2, 2, 0,
              1, 1, 1, 1
            ],
            "entityInstances": [
              { "__identifier": "PlayerStart", "px": [16, 16], "width": 16, "height": 24 }
            ]
          }
        ]
      }]
    })";

    cd::render2d::Tilemap2D ldtk_map {};
    const auto              imported = cd::render2d::import_ldtk_level_json(kSampleLdtk, ldtk_map);
    ASSERT_TRUE(imported.has_value());
    EXPECT_EQ(imported->level_identifier, "Cavern_01");
    EXPECT_EQ(imported->imported_tiles, 6U);
    ASSERT_EQ(imported->entities.size(), 1U);
    EXPECT_EQ(imported->entities.front().identifier, "PlayerStart");
    // Bottom row in LDtk (top-down row 2) maps to gy == 0 in Y-up engine world!
    EXPECT_EQ(ldtk_map.get_tile(0, 0).palette_id, 1U);
}

// -----------------------------------------------------------------------------
// Milestone 3 Features 10-14 & 19 Acceptance Tests
// -----------------------------------------------------------------------------

TEST(Render2DTest, TextureAtlas2D_PixelBufferAndUVMapping)
{
    cd::render2d::TextureAtlas2D atlas { 256U, 256U, cd::render2d::TextureFilterMode2D::kNearest };
    EXPECT_EQ(atlas.width(), 256U);
    EXPECT_EQ(atlas.height(), 256U);
    EXPECT_EQ(atlas.raw_rgba().size(), 256U * 256U * 4U);

    // Set & Get pixel
    const cd::render2d::ColorRgba8 red { 255U, 0U, 0U, 255U };
    atlas.set_pixel(10U, 20U, red);
    const auto read_back = atlas.get_pixel(10U, 20U);
    EXPECT_EQ(read_back.r, 255U);
    EXPECT_EQ(read_back.g, 0U);
    EXPECT_EQ(read_back.b, 0U);
    EXPECT_EQ(read_back.a, 255U);

    // Register region and verify UV coordinates
    const auto uv = atlas.add_region("hero_idle", 0U, 0U, 32U, 32U);
    EXPECT_FLOAT_EQ(uv.u0, 0.0F);
    EXPECT_FLOAT_EQ(uv.v0, 0.0F);
    EXPECT_FLOAT_EQ(uv.u1, 32.0F / 256.0F);
    EXPECT_FLOAT_EQ(uv.v1, 32.0F / 256.0F);

    const auto* reg = atlas.find_region("hero_idle");
    ASSERT_NE(reg, nullptr);
    EXPECT_EQ(reg->name, "hero_idle");
    EXPECT_EQ(reg->pixel_w, 32U);
}

TEST(Render2DTest, TextureAtlas2D_FilterModeAndBoundsClamping)
{
    cd::render2d::TextureAtlas2D atlas { 64U, 64U };
    EXPECT_EQ(atlas.filter_mode(), cd::render2d::TextureFilterMode2D::kNearest);
    atlas.set_filter_mode(cd::render2d::TextureFilterMode2D::kLinear);
    EXPECT_EQ(atlas.filter_mode(), cd::render2d::TextureFilterMode2D::kLinear);

    // Setting pixel out of bounds must not crash or corrupt memory
    atlas.set_pixel(100U, 100U, { 255U, 255U, 255U, 255U });
    const auto dummy = atlas.get_pixel(100U, 100U);
    EXPECT_EQ(dummy.a, 0U);  // Out-of-bounds returns transparent pixel
}

TEST(Render2DTest, ProceduralPixelArtSheet_GeneratesExpectedSpritesAndRegions)
{
    const auto atlas = cd::render2d::generate_procedural_pixel_art_atlas(16U);
    EXPECT_GE(atlas.width(), 64U);
    EXPECT_GE(atlas.height(), 64U);
    EXPECT_FALSE(atlas.raw_rgba().empty());

    // Essential regions for 2D platformer / RPG / arena
    const std::vector<std::string_view> required_regions = {
        "hero_idle", "hero_walk_0", "hero_walk_1", "hero_jump",
        "tile_grass", "tile_dirt", "tile_stone",
        "gem_ruby", "coin_gold", "enemy_drone", "particle_spark"
    };

    for (const auto& name : required_regions)
    {
        const auto* reg = atlas.find_region(name);
        ASSERT_NE(reg, nullptr) << "Missing required procedural region: " << name;
        EXPECT_GT(reg->pixel_w, 0U);
        EXPECT_GT(reg->pixel_h, 0U);
        EXPECT_LT(reg->uv.u0, reg->uv.u1);
        EXPECT_LT(reg->uv.v0, reg->uv.v1);
    }

    // Verify non-empty pixels exist (alpha channel > 0)
    bool has_visible_pixels = false;
    for (std::size_t i = 3U; i < atlas.raw_rgba().size(); i += 4U)
    {
        if (atlas.raw_rgba()[i] > 0U)
        {
            has_visible_pixels = true;
            break;
        }
    }
    EXPECT_TRUE(has_visible_pixels);
}

TEST(Render2DTest, TextureAtlas2D_LoadPngFromMemoryAndMissingFile)
{
    // 1. Missing file returns error
    const auto fail_res = cd::render2d::load_texture_atlas_png("non_existent_atlas_12345.png");
    EXPECT_FALSE(fail_res.has_value());

    // 2. Load from memory: 2x2 solid RGBA pixel stream
    const std::array<std::uint8_t, 16> src_pixels = {
        255, 0, 0, 255,   0, 255, 0, 255,
        0, 0, 255, 255,   255, 255, 0, 255
    };
    const auto mem_res = cd::render2d::load_texture_atlas_png_memory(src_pixels, 2U, 2U);
    ASSERT_TRUE(mem_res.has_value());
    EXPECT_EQ(mem_res->width(), 2U);
    EXPECT_EQ(mem_res->height(), 2U);
    EXPECT_EQ(mem_res->get_pixel(0U, 0U).r, 255U);
    EXPECT_EQ(mem_res->get_pixel(1U, 0U).g, 255U);
}

TEST(Render2DTest, Tilemap2D_TexturedQuadEmission)
{
    cd::render2d::Tilemap2D map {};
    cd::render2d::TilePaletteEntry2D textured_entry {};
    textured_entry.palette_id   = 7U;
    textured_entry.name         = "Textured Brick";
    textured_entry.texture_slot = 0U;
    textured_entry.uv_rect      = { 0.25F, 0.25F, 0.50F, 0.50F };
    map.register_palette_entry(textured_entry);

    map.set_tile(0, 0, 7U);
    EXPECT_EQ(map.non_empty_tile_count(), 1U);

    cd::render2d::Canvas2D canvas {};
    cd::render2d::Camera2D camera {};
    cd::render2d::LightingEnvironment2D lighting {};
    map.emit_to_canvas(canvas, camera, lighting);

    bool found_textured_cmd = false;
    for (const auto& cmd : canvas.commands())
    {
        if (cmd.kind == cd::render2d::CanvasCmdKind2D::kTexturedQuad && cmd.texture_slot == 0U)
        {
            found_textured_cmd = true;
            EXPECT_FLOAT_EQ(cmd.uv.u0, 0.25F);
            EXPECT_FLOAT_EQ(cmd.uv.v0, 0.25F);
            EXPECT_FLOAT_EQ(cmd.uv.u1, 0.50F);
            EXPECT_FLOAT_EQ(cmd.uv.v1, 0.50F);
            break;
        }
    }
    EXPECT_TRUE(found_textured_cmd);
}

TEST(Render2DTest, Tilemap2D_FloodFillAndStampTools)
{
    cd::render2d::Tilemap2D map {};
    // Create enclosed 5x5 room: borders are palette 1, inside (1..3, 1..3) is air (0)
    map.paint_rect(0, 0, 4, 4, 1U);
    map.paint_rect(1, 1, 3, 3, 0U);
    EXPECT_EQ(map.non_empty_tile_count(), 16U);

    // Flood fill inside with palette 2
    const std::size_t filled = map.flood_fill(2, 2, 2U);
    EXPECT_EQ(filled, 9U);
    EXPECT_EQ(map.get_tile(1, 1).palette_id, 2U);
    EXPECT_EQ(map.get_tile(3, 3).palette_id, 2U);
    EXPECT_EQ(map.get_tile(0, 0).palette_id, 1U);  // Border untouched

    // Stamp a 2x2 pattern at (10, 10)
    const std::array<std::uint16_t, 4> pattern = { 3U, 3U, 4U, 4U };
    map.stamp(10, 10, 2, 2, pattern);
    EXPECT_EQ(map.get_tile(10, 10).palette_id, 3U);
    EXPECT_EQ(map.get_tile(11, 10).palette_id, 3U);
    EXPECT_EQ(map.get_tile(10, 11).palette_id, 4U);
    EXPECT_EQ(map.get_tile(11, 11).palette_id, 4U);
}

TEST(Render2DTest, Lighting2D_NormalMapAndEmissiveShading)
{
    cd::render2d::LightingEnvironment2D env {};
    env.ambient_color          = { 0.0F, 0.0F, 0.0F };  // Total darkness
    env.ambient_intensity      = 0.0F;
    env.directional_intensity  = 0.0F;

    env.point_lights.push_back(cd::render2d::PointLight2D {
        .position  = { 0.0F, 5.0F },
        .color     = { 1.0F, 1.0F, 1.0F },
        .intensity = 1.0F,
        .radius    = 10.0F,
    });

    // Surface facing directly toward light (+Y)
    const auto lit_normal = cd::render2d::evaluate_lighting_2d(
        env, { 0.0F, 0.0F }, { 0.0F, 1.0F, 0.0F }, { 255U, 255U, 255U, 255U });

    // Surface facing away from light (-Y)
    const auto back_normal = cd::render2d::evaluate_lighting_2d(
        env, { 0.0F, 0.0F }, { 0.0F, -1.0F, 0.0F }, { 255U, 255U, 255U, 255U });

    EXPECT_GT(lit_normal.r, back_normal.r);
    EXPECT_EQ(back_normal.r, 0U);  // Clamped cosine attenuation

    // Emissive test: with zero ambient and zero lights, emissive material still glows
    cd::render2d::LightingEnvironment2D dark_env {};
    const cd::render2d::ColorRgba8 emissive_color { 0U, 255U, 128U, 255U };
    const auto glowing = cd::render2d::evaluate_emissive_lighting_2d(
        dark_env, emissive_color, 1.5F);
    EXPECT_EQ(glowing.g, 255U);
    EXPECT_EQ(glowing.b, 192U);

    const auto unboosted = cd::render2d::evaluate_emissive_lighting_2d(
        dark_env, emissive_color, 1.0F);
    EXPECT_EQ(unboosted.b, 128U);
}

TEST(Render2DTest, ParallaxLayer2D_ScrollFactorAndLayerSorting)
{
    cd::render2d::ParallaxManager2D mgr {};

    cd::render2d::ParallaxLayer2D bg {};
    bg.name                 = "Far Mountains";
    bg.scroll_factor        = { 0.2F, 0.0F };
    bg.auto_scroll_velocity = { 1.0F, 0.0F };
    bg.z_order              = -200;

    cd::render2d::ParallaxLayer2D mg {};
    mg.name                 = "Near Trees";
    mg.scroll_factor        = { 0.7F, 0.0F };
    mg.z_order              = -50;

    cd::render2d::ParallaxLayer2D hud {};
    hud.name                = "Fixed HUD";
    hud.scroll_factor       = { 0.0F, 0.0F };
    hud.z_order             = 100;

    mgr.add_layer(hud);  // Intentionally added out of order
    mgr.add_layer(bg);
    mgr.add_layer(mg);

    mgr.sort_layers();
    ASSERT_EQ(mgr.layers().size(), 3U);
    EXPECT_EQ(mgr.layers()[0].z_order, -200);
    EXPECT_EQ(mgr.layers()[1].z_order, -50);
    EXPECT_EQ(mgr.layers()[2].z_order, 100);

    // Effective offset calculation at camera X = 100.0F
    const cd::math::Vec2f cam_pos { 100.0F, 0.0F };
    const auto off_bg = mgr.compute_effective_offset(mgr.layers()[0], cam_pos);
    const auto off_hud = mgr.compute_effective_offset(mgr.layers()[2], cam_pos);
    EXPECT_FLOAT_EQ(off_bg.x, 20.0F);
    EXPECT_FLOAT_EQ(off_hud.x, 0.0F);

    // Auto-scroll update
    mgr.update(2.0F);
    EXPECT_FLOAT_EQ(mgr.layers()[0].current_offset.x, 2.0F);
}

// -----------------------------------------------------------------------------
// Adversarial Stress Tests (Milestone 3 — Challenger 1)
// -----------------------------------------------------------------------------

TEST(Render2DAdversarialTest, TextureAtlas2D_ZeroAndOnePixelBoundaries)
{
    // 1. Default-constructed atlas (0x0, empty buffer)
    cd::render2d::TextureAtlas2D default_atlas {};
    EXPECT_FALSE(default_atlas.is_valid());
    EXPECT_EQ(default_atlas.width(), 0U);
    EXPECT_EQ(default_atlas.height(), 0U);
    EXPECT_TRUE(default_atlas.pixels().empty());

    // Out-of-bounds queries on 0x0 atlas must safely return transparent pixel without crashing
    const auto oob_0 = default_atlas.get_pixel(0U, 0U);
    EXPECT_EQ(oob_0.r, 0U);
    EXPECT_EQ(oob_0.a, 0U);

    // Set pixel on 0x0 atlas must be a safe no-op
    default_atlas.set_pixel(0U, 0U, { 255U, 0U, 0U, 255U });

    // Compute UV on 0x0 atlas
    const auto uv_0 = default_atlas.compute_uv(0U, 0U, 10U, 10U);
    EXPECT_FLOAT_EQ(uv_0.u0, 0.0F);
    EXPECT_FLOAT_EQ(uv_0.v0, 0.0F);

    // Grid slicing on 0x0 atlas or with 0 tile dimensions
    default_atlas.slice_grid("tile", 0U, 0U);
    default_atlas.slice_grid("tile", 16U, 16U);
    EXPECT_EQ(default_atlas.regions().size(), 0U);

    // 2. Atlas constructed with 0x0 clamped to 1x1
    cd::render2d::TextureAtlas2D clamped_atlas { 0U, 0U };
    EXPECT_TRUE(clamped_atlas.is_valid());
    EXPECT_EQ(clamped_atlas.width(), 1U);
    EXPECT_EQ(clamped_atlas.height(), 1U);
    EXPECT_EQ(clamped_atlas.pixels().size(), 4U);

    clamped_atlas.set_pixel(0U, 0U, { 128U, 64U, 32U, 255U });
    const auto p1 = clamped_atlas.get_pixel(0U, 0U);
    EXPECT_EQ(p1.r, 128U);
    EXPECT_EQ(p1.g, 64U);
    EXPECT_EQ(p1.b, 32U);
    EXPECT_EQ(p1.a, 255U);

    // 3. Out-of-bounds reads/writes at extreme coordinates
    const auto p_far = clamped_atlas.get_pixel(0xFFFFFFFFU, 0xFFFFFFFFU);
    EXPECT_EQ(p_far.a, 0U);
    clamped_atlas.set_pixel(0xFFFFFFFFU, 0xFFFFFFFFU, { 255U, 255U, 255U, 255U });
    EXPECT_EQ(clamped_atlas.pixels().size(), 4U); // No reallocation or corruption
}

TEST(Render2DAdversarialTest, TextureAtlas2D_CorruptPngAndMalformedJsonRobustness)
{
    // 1. Completely corrupt byte streams must not crash or trigger undefined behavior
    const std::array<std::uint8_t, 8> garbage_bytes = { 0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03, 0x04 };
    const auto garbage_res = cd::render2d::load_texture_atlas_png_memory(garbage_bytes);
    EXPECT_FALSE(garbage_res.has_value());

    // 2. Truncated header
    const std::array<std::uint8_t, 4> trunc_header = { 0x89, 'P', 'N', 'G' };
    const auto trunc_res = cd::render2d::load_texture_atlas_png_memory(trunc_header);
    EXPECT_FALSE(trunc_res.has_value());

    // 3. Empty buffer
    const auto empty_res = cd::render2d::load_texture_atlas_png_memory({});
    EXPECT_FALSE(empty_res.has_value());

    // 4. Raw buffer length smaller than width * height * 4
    const std::array<std::uint8_t, 10> tiny_buf = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
    const auto undersized_res = cd::render2d::load_texture_atlas_png_memory(tiny_buf, 10U, 10U);
    EXPECT_FALSE(undersized_res.has_value());

    // 5. Malformed JSON for Aseprite and LDtk loaders
    cd::render2d::TextureAtlas2D atlas { 64U, 64U };
    constexpr std::string_view kMalformedJson = R"({ "frames": [ { "broken": )";
    const auto ase_malformed = atlas.load_regions_from_aseprite_json(kMalformedJson);
    EXPECT_FALSE(ase_malformed.has_value());

    const auto ldtk_malformed = atlas.load_regions_from_ldtk_json(kMalformedJson);
    EXPECT_FALSE(ldtk_malformed.has_value());

    // 6. JSON with wrong types (e.g., frames is boolean, not object or array)
    constexpr std::string_view kTypeMismatchJson = R"({ "frames": true, "defs": 12345 })";
    const auto ase_type_err = atlas.load_regions_from_aseprite_json(kTypeMismatchJson);
    EXPECT_TRUE(ase_type_err.has_value()); // Gracefully ignores non-matching fields
    EXPECT_EQ(atlas.regions().size(), 0U);

    const auto ldtk_type_err = atlas.load_regions_from_ldtk_json(kTypeMismatchJson);
    EXPECT_TRUE(ldtk_type_err.has_value());
    EXPECT_EQ(atlas.regions().size(), 0U);
}

TEST(Render2DAdversarialTest, TextureAtlas2D_RegionCollisionAndZeroAreaQueries)
{
    cd::render2d::TextureAtlas2D atlas { 128U, 128U };

    // Register region
    atlas.add_region("player_idle", 0U, 0U, 32U, 32U);
    EXPECT_EQ(atlas.regions().size(), 1U);

    // Overwrite existing region: must update in place without duplicates
    atlas.add_region("player_idle", 32U, 32U, 64U, 64U);
    EXPECT_EQ(atlas.regions().size(), 1U);
    const auto* reg = atlas.find_region("player_idle");
    ASSERT_NE(reg, nullptr);
    EXPECT_EQ(reg->x, 32U);
    EXPECT_EQ(reg->y, 32U);
    EXPECT_EQ(reg->pixel_w, 64U);
    EXPECT_EQ(reg->pixel_h, 64U);

    // Missing region returns nullptr
    EXPECT_EQ(atlas.find_region("non_existent_key_xyz"), nullptr);

    // Zero-dimension region
    const auto zero_uv = atlas.compute_uv(10U, 10U, 0U, 0U);
    EXPECT_FLOAT_EQ(zero_uv.u0, zero_uv.u1);
    EXPECT_FLOAT_EQ(zero_uv.v0, zero_uv.v1);

    // Out-of-bounds UV calculation clamped to [0, 1]
    const auto oob_uv = atlas.compute_uv(200U, 200U, 100U, 100U);
    EXPECT_FLOAT_EQ(oob_uv.u0, 1.0F);
    EXPECT_FLOAT_EQ(oob_uv.v0, 1.0F);
    EXPECT_FLOAT_EQ(oob_uv.u1, 1.0F);
    EXPECT_FLOAT_EQ(oob_uv.v1, 1.0F);
}

TEST(Render2DAdversarialTest, Tilemap2D_FloodFillBoundariesAndCycleGuard)
{
    cd::render2d::Tilemap2D map {};

    // 1. Flood fill with same replacement_id as target_id must immediately return 0 (no-op cycle guard)
    const std::size_t no_op_filled = map.flood_fill(0, 0, 0U);
    EXPECT_EQ(no_op_filled, 0U);

    // 2. Flood fill with max_cells = 0 must return 0
    const std::size_t zero_max_filled = map.flood_fill(0, 0, 1U, 0U);
    EXPECT_EQ(zero_max_filled, 0U);

    // 3. Flood fill on negative coordinate space across chunk boundaries
    // Paint a 10x10 square in negative coordinates [-25..-16] x [-25..-16]
    map.paint_rect(-25, -25, -16, -16, 5U);
    EXPECT_EQ(map.non_empty_tile_count(), 100U);

    // Flood fill only this enclosed negative square to palette 9
    const std::size_t neg_filled = map.flood_fill(-20, -20, 9U, 1000U);
    EXPECT_EQ(neg_filled, 100U);
    EXPECT_EQ(map.get_tile(-25, -25).palette_id, 9U);
    EXPECT_EQ(map.get_tile(-16, -16).palette_id, 9U);
    EXPECT_EQ(map.get_tile(-26, -26).palette_id, 0U); // Unmodified outside

    // 4. Large unconstrained flood fill strictly limited by max_cells guard
    // Fill open air from (100, 100) with max_cells = 64
    const std::size_t air_filled = map.flood_fill(100, 100, 2U, 64U);
    EXPECT_EQ(air_filled, 64U);
    EXPECT_EQ(map.get_tile(100, 100).palette_id, 2U);
}

TEST(Render2DAdversarialTest, Tilemap2D_StampAdversarialParametersAndClipping)
{
    cd::render2d::Tilemap2D map {};

    // 1. Invalid stamp dimensions (<= 0) or empty span
    const std::array<std::uint16_t, 4> pattern = { 1U, 2U, 3U, 4U };
    EXPECT_EQ(map.stamp(0, 0, 0, 2, pattern), 0U);
    EXPECT_EQ(map.stamp(0, 0, 2, -1, pattern), 0U);
    EXPECT_EQ(map.stamp(0, 0, 2, 2, {}), 0U);

    // 2. Pattern span smaller than width * height (truncated pattern)
    // Should stamp available elements safely without buffer overrun
    const std::array<std::uint16_t, 2> short_pattern = { 7U, 8U };
    const std::size_t stamped_short = map.stamp(5, 5, 2, 2, short_pattern);
    EXPECT_EQ(stamped_short, 2U);
    EXPECT_EQ(map.get_tile(5, 5).palette_id, 7U);
    EXPECT_EQ(map.get_tile(6, 5).palette_id, 8U);
    EXPECT_EQ(map.get_tile(5, 6).palette_id, 0U);

    // 3. Stamp across negative/positive origin boundary (-1, -1)
    const std::array<std::uint16_t, 4> cross_pattern = { 10U, 11U, 12U, 13U };
    const std::size_t stamped_cross = map.stamp(-1, -1, 2, 2, cross_pattern);
    EXPECT_EQ(stamped_cross, 4U);
    EXPECT_EQ(map.get_tile(-1, -1).palette_id, 10U);
    EXPECT_EQ(map.get_tile(0, -1).palette_id, 11U);
    EXPECT_EQ(map.get_tile(-1, 0).palette_id, 12U);
    EXPECT_EQ(map.get_tile(0, 0).palette_id, 13U);
}

TEST(Render2DAdversarialTest, ParallaxLayer2D_ZeroAndNegativeAndHugeDt)
{
    cd::render2d::ParallaxManager2D mgr {};

    cd::render2d::ParallaxLayer2D layer {};
    layer.name                 = "StressLayer";
    layer.scroll_factor        = { 0.0F, 0.0F };  // Zero scroll factor: camera motion has zero effect
    layer.auto_scroll_velocity = { -50.0F, 20.0F };
    layer.current_offset       = { 0.0F, 0.0F };
    layer.tile_size            = { 0.0F, 0.0F };  // Zero tile size must be safely clamped
    layer.texture_slot         = 1U;

    mgr.add_layer(layer);

    // 1. Effective offset at large camera coordinate
    const cd::math::Vec2f cam_pos { 999999.0F, -999999.0F };
    const auto off0 = mgr.compute_effective_offset(mgr.layers()[0], cam_pos);
    EXPECT_FLOAT_EQ(off0.x, 0.0F);
    EXPECT_FLOAT_EQ(off0.y, 0.0F);

    // 2. Huge time delta dt = 1000.0f
    mgr.update(1000.0F);
    EXPECT_FLOAT_EQ(mgr.layers()[0].current_offset.x, -50000.0F);
    EXPECT_FLOAT_EQ(mgr.layers()[0].current_offset.y, 20000.0F);

    // 3. Canvas emission with zero-dimension viewport and negative offsets
    cd::render2d::Canvas2D canvas {};
    cd::render2d::Camera2D cam {};
    const cd::render2d::ViewportRect2D zero_vp { 0.0F, 0.0F, 0.0F, 0.0F };
    mgr.emit_to_canvas(canvas, cam, zero_vp);
    EXPECT_EQ(canvas.commands().size(), 0U);

    // 4. Valid viewport emission with zero tile_size in layer (safely clamped internally)
    const cd::render2d::ViewportRect2D valid_vp { 0.0F, 0.0F, 640.0F, 360.0F };
    mgr.emit_to_canvas(canvas, cam, valid_vp);
    EXPECT_GT(canvas.commands().size(), 0U);
}

TEST(Render2DAdversarialTest, Lighting2D_DegenerateNormalsAndExtremeRadiance)
{
    cd::render2d::LightingEnvironment2D env {};
    env.ambient_color          = { 0.0F, 0.0F, 0.0F };
    env.ambient_intensity      = 0.0F;
    env.directional_intensity  = 0.0F;

    // 1. Degenerate zero normal vector (0, 0, 0) must not produce NaN or division by zero
    const cd::math::Vec3f zero_normal { 0.0F, 0.0F, 0.0F };
    const cd::render2d::ColorRgba8 pure_white { 255U, 255U, 255U, 255U };
    const auto zero_lit = cd::render2d::evaluate_lighting_2d(
        env, { 0.0F, 0.0F }, zero_normal, pure_white);
    EXPECT_EQ(zero_lit.r, 0U);
    EXPECT_EQ(zero_lit.g, 0U);
    EXPECT_EQ(zero_lit.b, 0U);
    EXPECT_EQ(zero_lit.a, 255U);

    // 2. Extreme normal vectors with huge magnitudes
    const cd::math::Vec3f huge_normal { 1.0e15F, 1.0e15F, 1.0e15F };
    const auto huge_lit = cd::render2d::evaluate_lighting_2d(
        env, { 0.0F, 0.0F }, huge_normal, pure_white);
    EXPECT_EQ(huge_lit.a, 255U);

    // 3. Unattenuated emissive lighting with zero ambient and zero lights
    const cd::render2d::ColorRgba8 neon_pink { 255U, 20U, 147U, 255U };
    const auto emissive_lit = cd::render2d::evaluate_emissive_lighting_2d(
        env, neon_pink, 2.0F); // 2.0x boost, clamped to 255
    EXPECT_EQ(emissive_lit.r, 255U);
    EXPECT_EQ(emissive_lit.g, 40U);
    EXPECT_EQ(emissive_lit.b, 255U);

    // Negative emissive intensity clamped to zero
    const auto neg_emissive = cd::render2d::evaluate_emissive_lighting_2d(
        env, neon_pink, -1.0F);
    EXPECT_EQ(neg_emissive.r, 0U);
    EXPECT_EQ(neg_emissive.g, 0U);
    EXPECT_EQ(neg_emissive.b, 0U);

    // 4. Combined lighting with emissive boost in total darkness
    const auto combined_dark = cd::render2d::evaluate_lighting_2d_with_emissive(
        env, { 0.0F, 0.0F }, { 0.0F, 0.0F, 1.0F }, pure_white, neon_pink, 1.0F);
    EXPECT_EQ(combined_dark.r, 255U);
    EXPECT_EQ(combined_dark.g, 20U);
    EXPECT_EQ(combined_dark.b, 147U);

    // 5. Decode normal RGB roundtrip and bounds
    const cd::render2d::ColorRgba8 flat_norm_pixel { 128U, 128U, 255U, 255U };
    const cd::math::Vec3f decoded = cd::render2d::decode_normal_rgb(flat_norm_pixel);
    EXPECT_NEAR(decoded.x, 0.0F, 0.01F);
    EXPECT_NEAR(decoded.y, 0.0F, 0.01F);
    EXPECT_NEAR(decoded.z, 1.0F, 0.01F);
}

TEST(Render2DAdversarialTest, ComprehensiveHighThroughputStressHarness)
{
    // Stress loop running 2000 varied operations across all subsystems
    cd::render2d::TextureAtlas2D atlas { 128U, 128U };
    cd::render2d::Tilemap2D map {};
    cd::render2d::ParallaxManager2D parallax {};

    cd::render2d::ParallaxLayer2D pl {};
    pl.tile_size = { 32.0F, 32.0F };
    parallax.add_layer(pl);

    for (int i = 0; i < 2000; ++i)
    {
        const std::uint32_t px = static_cast<std::uint32_t>((i * 73) % 150);
        const std::uint32_t py = static_cast<std::uint32_t>((i * 101) % 150);
        atlas.set_pixel(px, py, { 255U, 128U, 64U, 255U });
        (void)atlas.get_pixel(px, py);

        const int gx = (i % 64) - 32;
        const int gy = ((i * 3) % 64) - 32;
        map.set_tile(gx, gy, static_cast<std::uint16_t>((i % 5) + 1));
    }

    EXPECT_GT(map.non_empty_tile_count(), 0U);
    const auto colliders = map.bake_greedy_collision_rects();
    EXPECT_FALSE(colliders.empty());

    cd::render2d::Canvas2D canvas {};
    cd::render2d::Camera2D cam {};
    cd::render2d::LightingEnvironment2D env {};
    map.emit_to_canvas(canvas, cam, env);
    EXPECT_GT(canvas.commands().size(), 0U);

    parallax.update(0.016F);
    parallax.emit_to_canvas(canvas, cam, { 0.0F, 0.0F, 800.0F, 600.0F });
    canvas.sort_commands();
    EXPECT_GT(canvas.commands().size(), 0U);
}

TEST(Render2DAdversarialTest, TextureAtlas2D_StarterBundleAndProceduralParameterExtremes)
{
    // 1. Procedural generation with zero, tiny, and large tile sizes
    const auto atlas_0 = cd::render2d::TextureAtlas2D::generate_procedural_starter_atlas(0U);
    EXPECT_GE(atlas_0.width(), 64U);
    EXPECT_TRUE(atlas_0.is_valid());

    const auto atlas_4 = cd::render2d::TextureAtlas2D::generate_procedural_starter_atlas(4U);
    EXPECT_GE(atlas_4.width(), 64U);

    const auto atlas_32 = cd::render2d::TextureAtlas2D::generate_procedural_starter_atlas(32U);
    EXPECT_EQ(atlas_32.width(), 256U);
    EXPECT_EQ(atlas_32.height(), 256U);

    // 2. Starter bundle generates all 3 companion atlases
    const auto bundle = cd::render2d::TextureAtlas2D::generate_starter_bundle(16U);
    EXPECT_TRUE(bundle.albedo.is_valid());
    EXPECT_TRUE(bundle.normal.is_valid());
    EXPECT_TRUE(bundle.emissive.is_valid());
    EXPECT_EQ(bundle.albedo.regions().size(), bundle.normal.regions().size());
    EXPECT_EQ(bundle.albedo.regions().size(), bundle.emissive.regions().size());

    // 3. Normal atlas decoded pixels must form valid non-degenerate surface normals
    const auto* drone_norm_reg = bundle.normal.find_region("enemy_drone");
    ASSERT_NE(drone_norm_reg, nullptr);
    const auto norm_px = bundle.normal.get_pixel(drone_norm_reg->x + drone_norm_reg->width / 2U,
                                                 drone_norm_reg->y + drone_norm_reg->height / 2U);
    const auto n_vec = cd::render2d::decode_normal_rgb(norm_px);
    const float n_len = std::sqrt(n_vec.x * n_vec.x + n_vec.y * n_vec.y + n_vec.z * n_vec.z);
    EXPECT_NEAR(n_len, 1.0F, 0.05F);

    // 4. LDtk importer with zero tileGridSize must not divide by zero
    cd::render2d::TextureAtlas2D ldtk_atlas { 128U, 128U };
    constexpr std::string_view kZeroGridLdtk = R"({
      "defs": {
        "tilesets": [
          { "identifier": "ZeroGrid", "tileGridSize": 0, "pxWid": 64, "pxHei": 64 }
        ]
      }
    })";
    const auto ldtk_res = ldtk_atlas.load_regions_from_ldtk_json(kZeroGridLdtk);
    EXPECT_TRUE(ldtk_res.has_value());
    EXPECT_GT(ldtk_atlas.regions().size(), 0U);
}

TEST(Render2DAdversarialTest, Tilemap2D_SparseChunkMemoryReclaimAndCheckerboardGreedyMesher)
{
    cd::render2d::Tilemap2D map {};
    EXPECT_EQ(map.active_chunk_count(), 0U);

    // 1. Placing a single tile in chunk (5, 5) creates exactly 1 chunk
    map.set_tile(80, 80, 1U);
    EXPECT_EQ(map.active_chunk_count(), 1U);
    EXPECT_EQ(map.non_empty_tile_count(), 1U);

    // Erasing the tile must delete the chunk from memory
    map.erase_tile(80, 80);
    EXPECT_EQ(map.non_empty_tile_count(), 0U);
    EXPECT_EQ(map.active_chunk_count(), 0U);

    // 2. Checkerboard pattern across 2x2 chunks (16x16 area): 128 non-touching tiles
    // Greedy mesher must handle worst-case non-mergeable geometry without hanging or stack overflow
    for (int y = 0; y < 16; ++y)
    {
        for (int x = 0; x < 16; ++x)
        {
            if ((x + y) % 2 == 0)
            {
                map.set_tile(x, y, 1U, false);
            }
        }
    }
    EXPECT_EQ(map.non_empty_tile_count(), 128U);
    const auto colliders = map.bake_greedy_collision_rects();
    // In alternating checkerboard, no two tiles can merge horizontally or vertically
    EXPECT_EQ(colliders.size(), 128U);
}

TEST(Render2DAdversarialTest, Tilemap2D_ExtremeCoordinatesAndInvertedPaints)
{
    cd::render2d::Tilemap2D map {};

    // 1. Extreme negative and positive coordinates
    map.set_tile(-100000, 100000, 2U);
    EXPECT_EQ(map.get_tile(-100000, 100000).palette_id, 2U);
    EXPECT_EQ(map.get_tile(-99999, 100000).palette_id, 0U);

    // 2. Inverted rectangle paint: min > max in arguments
    map.paint_rect(10, 10, 5, 5, 3U);
    EXPECT_EQ(map.get_tile(5, 5).palette_id, 3U);
    EXPECT_EQ(map.get_tile(10, 10).palette_id, 3U);
    EXPECT_EQ(map.get_tile(7, 7).palette_id, 3U);
    EXPECT_EQ(map.get_tile(4, 4).palette_id, 0U);

    // 3. Tile size boundary clamping
    map.set_tile_size(-10.0F);
    EXPECT_FLOAT_EQ(map.tile_size(), 0.125F);
    map.set_tile_size(0.0F);
    EXPECT_FLOAT_EQ(map.tile_size(), 0.125F);
    map.set_tile_size(2.0F);
    EXPECT_FLOAT_EQ(map.tile_size(), 2.0F);
}

TEST(Render2DAdversarialTest, Tilemap2D_DistantChunksBakeGreedyCollidersComplexity)
{
    cd::render2d::Tilemap2D map {};
    map.set_tile(0, 0, 1U);
    map.set_tile(200, 200, 1U);
    const auto colliders = map.bake_greedy_collision_rects();
    EXPECT_EQ(colliders.size(), 2U);
}

TEST(Render2DAdversarialTest, FlipbookAnimator2D_HugeDtSpinLoopPerformance)
{
    cd::render2d::FlipbookAnimator2D anim {};
    cd::render2d::FlipbookClip2D clip {};
    clip.name = "IdleLoop";
    clip.frames.push_back({ {}, 0.01F });
    clip.frames.push_back({ {}, 0.01F });
    clip.loop = true;
    anim.add_clip(clip);
    ASSERT_TRUE(anim.play("IdleLoop"));

    // Step with huge dt (e.g. 10,000 seconds after system resume/unpause)
    anim.step(10000.0F);
    EXPECT_LE(anim.current_frame_index(), 1U);
}

TEST(Render2DAdversarialTest, Lighting2D_DenseLightsOccludersPerformanceAndPenumbraClamping)
{
    cd::render2d::LightingEnvironment2D env {};
    env.ambient_color = { 0.1F, 0.1F, 0.15F };
    env.ambient_intensity = 0.5F;
    env.enable_lighting = true;

    // 1. Add 50 point lights and 20 occluders
    for (int i = 0; i < 50; ++i)
    {
        env.point_lights.push_back(cd::render2d::PointLight2D {
            .position = { static_cast<float>(i * 2), static_cast<float>((i % 5) * 2) },
            .color = { 1.0F, 0.9F, 0.7F },
            .intensity = 1.0F,
            .radius = 8.0F,
            .cast_shadows = (i % 2 == 0),
            .enabled = true,
        });
    }

    for (int j = 0; j < 20; ++j)
    {
        env.occluders.push_back(cd::render2d::ShadowOccluder2D {
            .center = { static_cast<float>(j * 4 + 1), static_cast<float>((j % 4) * 2 + 1) },
            .half_extents = { 0.6F, 0.6F },
            .opacity = 0.85F,
            .enabled = true,
        });
    }

    // Evaluate 500 sample points
    const cd::render2d::ColorRgba8 albedo { 200U, 200U, 200U, 255U };
    const cd::math::Vec3f norm { 0.0F, 0.0F, 1.0F };

    for (int k = 0; k < 500; ++k)
    {
        const cd::math::Vec2f pt { static_cast<float>(k % 50), static_cast<float>(k / 50) };
        const auto lit = cd::render2d::evaluate_lighting_2d(env, pt, norm, albedo);
        EXPECT_GT(lit.a, 0U);
    }

    // 2. Light exactly on boundary of occluder
    const float vis = cd::render2d::compute_soft_shadow_visibility_2d(
        { 1.0F, 1.0F }, { 1.0F, 5.0F }, env.occluders);
    EXPECT_GE(vis, 0.0F);
    EXPECT_LE(vis, 1.0F);
}

TEST(Render2DAdversarialTest, Renderer2D_DegenerateViewportsAndMassiveSpriteBatchEmission)
{
    cd::render2d::Renderer2D renderer {};
    cd::render2d::Canvas2D canvas {};
    cd::render2d::Camera2D camera {};
    cd::render2d::Tilemap2D tilemap {};
    cd::render2d::LightingEnvironment2D lighting {};

    tilemap.paint_rect(-5, -5, 5, 5, 1U);

    // 1. Degenerate viewport dimensions (0x0, negative, 1x1, 2x2)
    renderer.render_scene(canvas, { 0.0F, 0.0F, 0.0F, 0.0F }, camera, tilemap, lighting, {}, {});
    EXPECT_EQ(canvas.commands().size(), 0U);

    renderer.render_scene(canvas, { 0.0F, 0.0F, -100.0F, -50.0F }, camera, tilemap, lighting, {}, {});
    EXPECT_EQ(canvas.commands().size(), 0U);

    renderer.render_scene(canvas, { 0.0F, 0.0F, 1.0F, 1.0F }, camera, tilemap, lighting, {}, {});
    EXPECT_EQ(canvas.commands().size(), 0U);

    // 2. Massive sprite batch emission: 1000 sprites with varied layers & styles
    std::vector<cd::render2d::Sprite2D> sprites;
    sprites.reserve(1000U);
    for (int i = 0; i < 1000; ++i)
    {
        cd::render2d::Sprite2D sp {};
        sp.position = { static_cast<float>((i % 40) - 20), static_cast<float>((i / 40) - 12) };
        sp.size = { 1.0F, 1.0F };
        sp.layer = static_cast<std::uint8_t>((i % 50) + 10);
        sp.z_index = static_cast<std::int16_t>((i % 20) - 10);
        sp.y_sort = (i % 2 == 0);
        sp.style = static_cast<cd::render2d::SpriteVisualStyle>((i % 8));
        sprites.push_back(sp);
    }

    const cd::render2d::ViewportRect2D vp { 0.0F, 0.0F, 1280.0F, 720.0F };
    renderer.render_scene(canvas, vp, camera, tilemap, lighting, sprites, {});
    EXPECT_GT(canvas.commands().size(), 0U);

    // Verify sort order: strictly non-decreasing 64-bit sort key
    const auto cmds = canvas.commands();
    for (std::size_t i = 1U; i < cmds.size(); ++i)
    {
        EXPECT_LE(cmds[i - 1U].sort_key, cmds[i].sort_key);
    }

    // 3. Clean empty 2D scene (0 tiles, 0 sprites, 0 parallax layers, overlays off)
    // must emit ONLY the clean sky backdrop gradient (1 command) without unsolicited clutter.
    cd::render2d::Tilemap2D empty_tilemap {};
    cd::render2d::Render2DDebugOptions clean_opts {};
    clean_opts.show_grid             = false;
    clean_opts.show_greedy_colliders = false;
    clean_opts.show_light_gizmos     = false;
    clean_opts.show_camera_frame     = false;
    renderer.render_scene(canvas, vp, camera, empty_tilemap, lighting, {}, {}, clean_opts, {});
    EXPECT_EQ(canvas.commands().size(), 1U);
}

}  // namespace
