// =============================================================================
// CHROMODYNAMIC — test_render2d.cpp
// Unit tests for cd::render2d (SortKey2D, Camera2D, Tilemap2D Wang Auto-Tiling
// & Greedy Collider Meshing, Lighting2D Soft Shadows, FlipbookAnimator2D, and
// .aseprite / .ldtk Importers).
// =============================================================================
#include <cd/render2d/Renderer2D.hpp>

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

}  // namespace
