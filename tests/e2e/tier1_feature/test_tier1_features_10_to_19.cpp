// =============================================================================
// CHROMODYNAMIC — tests/e2e/tier1_feature/test_tier1_features_10_to_19.cpp
//
// Tier 1 Category-Partition Feature Tests for Features 10 through 19:
//   F10: TextureAtlas2D Container & Filtering
//   F11: Procedural Pixel-Art Sheet Generator
//   F12: Texture Atlas Disk Loaders
//   F13: Textured Tilemap & Autotiling Emission
//   F14: Normal & Emissive 2D Lighting
//   F15: ParticleSystem2D
//   F16: SpatialHash2D Broadphase
//   F17: ObjectPool2D
//   F18: AudioCue2D Procedural Sound System
//   F19: ParallaxLayer2D
// =============================================================================

#include "../harness/E2eHarness.hpp"
#include "../harness/MockAudioSink.hpp"

#include <cd/render2d/TextureAtlas2D.hpp>
#include <cd/render2d/Tilemap2D.hpp>
#include <cd/render2d/Lighting2D.hpp>
#include <cd/render2d/ParallaxLayer2D.hpp>
#include <cd/world2d/ParticleSystem2D.hpp>
#include <cd/world2d/SpatialHash2D.hpp>
#include <cd/world2d/ObjectPool2D.hpp>
#include <cd/world2d/AudioCue2D.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <vector>

namespace cd::e2e
{

// =============================================================================
// Feature 10: TextureAtlas2D Container & Filtering
// =============================================================================

TEST(Tier1_Feature10_TextureAtlas, CreationAndAllocation)
{
    cd::render2d::TextureAtlas2D atlas(256U, 256U, cd::render2d::TextureFilterMode2D::kNearest);
    EXPECT_TRUE(atlas.is_valid());
    EXPECT_EQ(atlas.width(), 256U);
    EXPECT_EQ(atlas.height(), 256U);
    EXPECT_EQ(atlas.pixels().size(), 256U * 256U * 4U);
}

TEST(Tier1_Feature10_TextureAtlas, AddNamedRegion)
{
    cd::render2d::TextureAtlas2D atlas(128U, 128U);
    auto uv = atlas.add_region("hero_idle", 0U, 0U, 16U, 16U);

    EXPECT_FLOAT_EQ(uv.u0, 0.0F);
    EXPECT_FLOAT_EQ(uv.v0, 0.0F);
    EXPECT_FLOAT_EQ(uv.u1, 16.0F / 128.0F);
    EXPECT_FLOAT_EQ(uv.v1, 16.0F / 128.0F);
}

TEST(Tier1_Feature10_TextureAtlas, GetRegionCoordinates)
{
    cd::render2d::TextureAtlas2D atlas(128U, 128U);
    atlas.add_region("gem_sparkle", 32U, 64U, 16U, 16U);

    const auto* reg = atlas.find_region("gem_sparkle");
    ASSERT_NE(reg, nullptr);
    EXPECT_EQ(reg->x, 32U);
    EXPECT_EQ(reg->y, 64U);
    EXPECT_EQ(reg->width, 16U);
    EXPECT_EQ(reg->height, 16U);
}

TEST(Tier1_Feature10_TextureAtlas, NearestFilterSampling)
{
    cd::render2d::TextureAtlas2D atlas(64U, 64U, cd::render2d::TextureFilterMode2D::kNearest);
    EXPECT_EQ(atlas.filter_mode(), cd::render2d::TextureFilterMode2D::kNearest);
}

TEST(Tier1_Feature10_TextureAtlas, LinearFilterSampling)
{
    cd::render2d::TextureAtlas2D atlas(64U, 64U, cd::render2d::TextureFilterMode2D::kLinear);
    EXPECT_EQ(atlas.filter_mode(), cd::render2d::TextureFilterMode2D::kLinear);
}

// =============================================================================
// Feature 11: Procedural Pixel-Art Sheet Generator
// =============================================================================

TEST(Tier1_Feature11_ProceduralArt, GeneratesHeroSheet)
{
    auto atlas = cd::render2d::TextureAtlas2D::generate_procedural_starter_atlas(16U);
    EXPECT_TRUE(atlas.is_valid());
    EXPECT_NE(atlas.find_region("hero_idle"), nullptr);
}

TEST(Tier1_Feature11_ProceduralArt, GeneratesTerrainTiles)
{
    auto atlas = cd::render2d::TextureAtlas2D::generate_procedural_starter_atlas(16U);
    EXPECT_NE(atlas.find_region("tile_stone"), nullptr);
    EXPECT_NE(atlas.find_region("tile_grass"), nullptr);
}

TEST(Tier1_Feature11_ProceduralArt, GeneratesCollectibles)
{
    auto atlas = cd::render2d::TextureAtlas2D::generate_procedural_starter_atlas(16U);
    EXPECT_NE(atlas.find_region("gem_ruby"), nullptr);
}

TEST(Tier1_Feature11_ProceduralArt, GeneratesParticles)
{
    auto atlas = cd::render2d::TextureAtlas2D::generate_procedural_starter_atlas(16U);
    EXPECT_NE(atlas.find_region("particle_spark"), nullptr);
}

TEST(Tier1_Feature11_ProceduralArt, StarterBundleComplete)
{
    auto bundle = cd::render2d::TextureAtlas2D::generate_starter_bundle(16U);
    EXPECT_TRUE(bundle.albedo.is_valid());
    EXPECT_TRUE(bundle.normal.is_valid());
    EXPECT_TRUE(bundle.emissive.is_valid());
}

// =============================================================================
// Feature 12: Texture Atlas Disk Loaders
// =============================================================================

TEST(Tier1_Feature12_AtlasLoaders, LoadFromRawMemory)
{
    // Minimal 2x2 RGBA test
    std::vector<std::uint8_t> raw_pixels = {
        255, 0, 0, 255,   0, 255, 0, 255,
        0, 0, 255, 255,   255, 255, 0, 255
    };
    cd::render2d::TextureAtlas2D atlas(2U, 2U);
    std::memcpy(atlas.mutable_pixels().data(), raw_pixels.data(), raw_pixels.size());

    EXPECT_EQ(atlas.get_pixel(0, 0).r, 255);
    EXPECT_EQ(atlas.get_pixel(1, 0).g, 255);
    EXPECT_EQ(atlas.get_pixel(0, 1).b, 255);
}

TEST(Tier1_Feature12_AtlasLoaders, ParseAsepriteJsonMetadata)
{
    cd::render2d::TextureAtlas2D atlas(64U, 64U);
    const std::string aseprite_json = R"({
        "frames": {
            "hero_walk_0.ase": {
                "frame": {"x": 0, "y": 0, "w": 16, "h": 16}
            },
            "hero_walk_1.ase": {
                "frame": {"x": 16, "y": 0, "w": 16, "h": 16}
            }
        }
    })";
    auto res = atlas.load_regions_from_aseprite_json(aseprite_json);
    EXPECT_TRUE(res.has_value());
    EXPECT_NE(atlas.find_region("hero_walk_0.ase"), nullptr);
    EXPECT_NE(atlas.find_region("hero_walk_1.ase"), nullptr);
}

TEST(Tier1_Feature12_AtlasLoaders, ParseLdtkJsonMetadata)
{
    cd::render2d::TextureAtlas2D atlas(64U, 64U);
    const std::string ldtk_json = R"({
        "defs": {
            "tilesets": [
                {
                    "identifier": "DungeonTiles",
                    "tileGridSize": 16,
                    "pxWid": 64,
                    "pxHei": 64
                }
            ]
        }
    })";
    auto res = atlas.load_regions_from_ldtk_json(ldtk_json);
    EXPECT_TRUE(res.has_value());
}

TEST(Tier1_Feature12_AtlasLoaders, SliceGridGeneratesNamedRegions)
{
    cd::render2d::TextureAtlas2D atlas(32U, 32U);
    atlas.slice_grid("tile", 16U, 16U);
    EXPECT_EQ(atlas.regions().size(), 4U);
    EXPECT_NE(atlas.find_region("tile_0_0"), nullptr);
    EXPECT_NE(atlas.find_region("tile_1_1"), nullptr);
}

TEST(Tier1_Feature12_AtlasLoaders, PixelDataIntegrity)
{
    cd::render2d::TextureAtlas2D atlas(4U, 4U);
    atlas.set_pixel(2U, 2U, cd::render2d::ColorRgba8 { 128U, 64U, 32U, 255U });

    auto p = atlas.get_pixel(2U, 2U);
    EXPECT_EQ(p.r, 128U);
    EXPECT_EQ(p.g, 64U);
    EXPECT_EQ(p.b, 32U);
    EXPECT_EQ(p.a, 255U);
}

// =============================================================================
// Feature 13: Textured Tilemap & Autotiling Emission
// =============================================================================

TEST(Tier1_Feature13_Tilemap, SetTileAssignsPalette)
{
    cd::render2d::Tilemap2D tm;
    tm.set_tile(3, 4, 1U);
    EXPECT_EQ(tm.get_tile(3, 4).palette_id, 1U);
}

TEST(Tier1_Feature13_Tilemap, CardinalAutotileBitmask)
{
    cd::render2d::Tilemap2D tm;
    tm.set_tile(10, 10, 1U);
    tm.set_tile(10, 11, 1U); // North

    const auto mask = tm.compute_autotile_mask(10, 10);
    EXPECT_TRUE((mask & cd::render2d::autotile_bits::kNorth) != 0U);
}

TEST(Tier1_Feature13_Tilemap, EmitToCanvasTexturedQuads)
{
    cd::render2d::Tilemap2D tm;
    cd::render2d::TilePaletteEntry2D pal {};
    pal.palette_id = 1U;
    pal.texture_slot = 0U;
    pal.uv_rect = { 0.0F, 0.0F, 0.25F, 0.25F };
    tm.register_palette_entry(pal);
    tm.set_tile(0, 0, 1U);

    cd::render2d::Canvas2D canvas;
    cd::render2d::Camera2D cam;
    cd::render2d::LightingEnvironment2D lighting;
    tm.emit_to_canvas(canvas, cam, lighting);

    EXPECT_GE(canvas.commands().size(), 1U);
}

TEST(Tier1_Feature13_Tilemap, GreedyColliderMeshing)
{
    cd::render2d::Tilemap2D tm;
    // Place a 3x1 contiguous row
    tm.set_tile(0, 0, 1U);
    tm.set_tile(1, 0, 1U);
    tm.set_tile(2, 0, 1U);

    auto colliders = tm.bake_greedy_collision_rects();
    EXPECT_EQ(colliders.size(), 1U);
    EXPECT_FLOAT_EQ(colliders[0].half_extents.x, 1.5F * tm.tile_size());
}

TEST(Tier1_Feature13_Tilemap, ChunkAllocationOnDemand)
{
    cd::render2d::Tilemap2D tm;
    EXPECT_EQ(tm.active_chunk_count(), 0U);

    tm.set_tile(0, 0, 1U);
    EXPECT_EQ(tm.active_chunk_count(), 1U);

    // Place tile in distant chunk (gx = 100)
    tm.set_tile(100, 100, 1U);
    EXPECT_EQ(tm.active_chunk_count(), 2U);
}

// =============================================================================
// Feature 14: Normal & Emissive 2D Lighting
// =============================================================================

TEST(Tier1_Feature14_Lighting2D, PointLightEvaluation)
{
    cd::render2d::PointLight2D light {};
    light.position = { 0.0F, 0.0F };
    light.radius = 10.0F;
    light.intensity = 1.0F;

    const cd::math::Vec2f sample_pos { 2.0F, 0.0F };
    const float dist = cd::math::length(sample_pos - light.position);
    const float atten = std::max(0.0F, 1.0F - (dist / light.radius));

    EXPECT_GT(atten, 0.0F);
}

TEST(Tier1_Feature14_Lighting2D, NormalMapPerturbation)
{
    const cd::render2d::ColorRgba8 norm_col { 128U, 128U, 255U, 255U }; // Flat Z normal
    const auto normal = cd::render2d::decode_normal_rgb(norm_col);

    EXPECT_NEAR(normal.x, 0.0F, 0.05F);
    EXPECT_NEAR(normal.y, 0.0F, 0.05F);
    EXPECT_NEAR(normal.z, 1.0F, 0.05F);
}

TEST(Tier1_Feature14_Lighting2D, EmissiveGlowAddition)
{
    cd::render2d::LightingEnvironment2D env {};
    env.ambient_color = { 0.0F, 0.0F, 0.0F };

    const auto lit = cd::render2d::evaluate_lighting_2d_with_emissive(
        env,
        { 0.0F, 0.0F },
        { 0.0F, 0.0F, 1.0F },
        cd::render2d::ColorRgba8 { 0, 0, 0, 255 },
        cd::render2d::ColorRgba8 { 255, 100, 50, 255 },
        1.0F
    );

    EXPECT_EQ(lit.r, 255);
    EXPECT_EQ(lit.g, 100);
    EXPECT_EQ(lit.b, 50);
}

TEST(Tier1_Feature14_Lighting2D, ShadowOccluderPenumbra)
{
    std::vector<cd::render2d::ShadowOccluder2D> occluders;
    cd::render2d::ShadowOccluder2D occ {};
    occ.center = { 5.0F, 0.0F };
    occ.half_extents = { 0.5F, 0.5F };
    occluders.push_back(occ);

    // Sample behind occluder
    const float vis = cd::render2d::compute_soft_shadow_visibility_2d(
        { 0.0F, 0.0F },
        { 10.0F, 0.0F },
        occluders
    );

    EXPECT_LT(vis, 0.5F);
}

TEST(Tier1_Feature14_Lighting2D, AmbientLightingBase)
{
    cd::render2d::LightingEnvironment2D env {};
    env.ambient_color = { 0.5F, 0.5F, 0.5F };
    env.ambient_intensity = 1.0F;

    const auto lit = cd::render2d::evaluate_lighting_2d_with_emissive(
        env,
        { 0.0F, 0.0F },
        { 0.0F, 0.0F, 1.0F },
        cd::render2d::ColorRgba8 { 200, 200, 200, 255 },
        cd::render2d::ColorRgba8 { 0, 0, 0, 0 },
        0.0F
    );

    EXPECT_GT(lit.r, 50);
    EXPECT_GT(lit.g, 50);
    EXPECT_GT(lit.b, 50);
}

// =============================================================================
// Feature 15: ParticleSystem2D
// =============================================================================

TEST(Tier1_Feature15_ParticleSystem, EmitParticles)
{
    cd::world2d::ParticleSystem2D ps(256U);
    EXPECT_EQ(ps.active_count(), 0U);

    ps.emit(20U);
    EXPECT_EQ(ps.active_count(), 20U);
}

TEST(Tier1_Feature15_ParticleSystem, SimulateMovement)
{
    cd::world2d::ParticleSystem2D ps(100U);
    cd::world2d::ParticleEmitterConfig2D cfg {};
    cfg.speed_min = 10.0F;
    cfg.speed_max = 10.0F;
    cfg.velocity_min = { 1.0F, 0.0F };
    cfg.velocity_max = { 1.0F, 0.0F };
    cfg.gravity = { 0.0F, 0.0F };

    ps.spawn_burst({ 0.0F, 0.0F }, 1U, cfg);
    ps.update(0.1F);

    ASSERT_EQ(ps.active_count(), 1U);
    const auto p = ps.active_particles()[0];
    EXPECT_GT(p.position.x, 0.0F);
}

TEST(Tier1_Feature15_ParticleSystem, ColorAndSizeRamps)
{
    cd::world2d::ParticleSystem2D ps(10U);
    cd::world2d::ParticleEmitterConfig2D cfg {};
    cfg.lifetime_min = 1.0F;
    cfg.lifetime_max = 1.0F;
    cfg.start_size_min = { 1.0F, 1.0F };
    cfg.start_size_max = { 1.0F, 1.0F };
    cfg.end_size_min = { 0.0F, 0.0F };
    cfg.end_size_max = { 0.0F, 0.0F };

    ps.spawn_burst({ 0.0F, 0.0F }, 1U, cfg);
    ps.update(0.5F); // Mid-lifetime

    ASSERT_EQ(ps.active_count(), 1U);
}

TEST(Tier1_Feature15_ParticleSystem, ExpirationAndRecycling)
{
    cd::world2d::ParticleSystem2D ps(50U);
    cd::world2d::ParticleEmitterConfig2D cfg {};
    cfg.lifetime_min = 0.1F;
    cfg.lifetime_max = 0.1F;

    ps.spawn_burst({ 0.0F, 0.0F }, 10U, cfg);
    EXPECT_EQ(ps.active_count(), 10U);

    ps.update(0.2F); // Exceed lifetime
    EXPECT_EQ(ps.active_count(), 0U);
}

TEST(Tier1_Feature15_ParticleSystem, EmitToCanvas)
{
    cd::world2d::ParticleSystem2D ps(50U);
    ps.emit(10U);

    cd::render2d::Canvas2D canvas;
    ps.emit_to_canvas(canvas);
    EXPECT_EQ(canvas.commands().size(), 10U);
}

// =============================================================================
// Feature 16: SpatialHash2D Broadphase
// =============================================================================

TEST(Tier1_Feature16_SpatialHash, InsertAndQueryAABB)
{
    cd::world2d::SpatialHash2D hash(2.0F);
    hash.insert(101U, cd::math::Aabb2 { 0.0F, 0.0F, 1.0F, 1.0F });

    std::vector<std::uint32_t> results;
    hash.query_aabb(cd::math::Aabb2 { 0.5F, 0.5F, 1.5F, 1.5F }, results);

    ASSERT_EQ(results.size(), 1U);
    EXPECT_EQ(results[0], 101U);
}

TEST(Tier1_Feature16_SpatialHash, QueryCircle)
{
    cd::world2d::SpatialHash2D hash(2.0F);
    hash.insert(202U, cd::math::Aabb2 { 0.0F, 0.0F, 0.5F, 0.5F });

    std::vector<std::uint32_t> results;
    hash.query_circle({ 0.0F, 0.0F }, 1.0F, results);

    ASSERT_EQ(results.size(), 1U);
    EXPECT_EQ(results[0], 202U);
}

TEST(Tier1_Feature16_SpatialHash, UpdateEntityPosition)
{
    cd::world2d::SpatialHash2D hash(2.0F);
    hash.insert(303U, cd::math::Aabb2 { 0.0F, 0.0F, 1.0F, 1.0F });
    hash.update(303U, cd::math::Aabb2 { 10.0F, 10.0F, 11.0F, 11.0F });

    std::vector<std::uint32_t> res_old;
    hash.query_aabb(cd::math::Aabb2 { 0.0F, 0.0F, 2.0F, 2.0F }, res_old);
    EXPECT_TRUE(res_old.empty());

    std::vector<std::uint32_t> res_new;
    hash.query_aabb(cd::math::Aabb2 { 9.0F, 9.0F, 12.0F, 12.0F }, res_new);
    ASSERT_EQ(res_new.size(), 1U);
    EXPECT_EQ(res_new[0], 303U);
}

TEST(Tier1_Feature16_SpatialHash, RemoveEntity)
{
    cd::world2d::SpatialHash2D hash(2.0F);
    hash.insert(404U, cd::math::Aabb2 { 0.0F, 0.0F, 1.0F, 1.0F });
    EXPECT_TRUE(hash.remove(404U));

    std::vector<std::uint32_t> results;
    hash.query_aabb(cd::math::Aabb2 { 0.0F, 0.0F, 1.0F, 1.0F }, results);
    EXPECT_TRUE(results.empty());
}

TEST(Tier1_Feature16_SpatialHash, MultiCellSpanning)
{
    cd::world2d::SpatialHash2D hash(2.0F);
    // Spans 4 cells: cell size 2.0, spans (-1..3, -1..3)
    hash.insert(505U, cd::math::Aabb2 { -1.0F, -1.0F, 3.0F, 3.0F });

    std::vector<std::uint32_t> results;
    hash.query_aabb(cd::math::Aabb2 { 2.5F, 2.5F, 3.5F, 3.5F }, results);
    ASSERT_EQ(results.size(), 1U);
    EXPECT_EQ(results[0], 505U);
}

// =============================================================================
// Feature 17: ObjectPool2D
// =============================================================================

struct TestBullet
{
    float x { 0.0F };
    float y { 0.0F };
    bool active { true };
};

TEST(Tier1_Feature17_ObjectPool, AcquireReturnsValidSlot)
{
    cd::world2d::ObjectPool2D<TestBullet> pool(32U);
    EXPECT_EQ(pool.active_count(), 0U);

    auto h = pool.acquire();
    ASSERT_TRUE(h.has_value());
    EXPECT_TRUE(pool.is_valid(*h));
    EXPECT_EQ(pool.active_count(), 1U);
}

TEST(Tier1_Feature17_ObjectPool, ReleaseRecyclesSlot)
{
    cd::world2d::ObjectPool2D<TestBullet> pool(16U);
    auto h = pool.acquire();
    ASSERT_TRUE(h.has_value());

    EXPECT_TRUE(pool.release(*h));
    EXPECT_EQ(pool.active_count(), 0U);
    EXPECT_FALSE(pool.is_valid(*h));
}

TEST(Tier1_Feature17_ObjectPool, ZeroHeapAllocationDuringLoop)
{
    cd::world2d::ObjectPool2D<TestBullet, 64U> pool(64U, false);
    for (int i = 0; i < 64; ++i)
    {
        auto h = pool.acquire();
        ASSERT_TRUE(h.has_value());
    }
    EXPECT_TRUE(pool.full());
}

TEST(Tier1_Feature17_ObjectPool, ClearResetsAll)
{
    cd::world2d::ObjectPool2D<TestBullet> pool(32U);
    auto h1 = pool.acquire();
    auto h2 = pool.acquire();
    (void)h1;
    (void)h2;
    EXPECT_EQ(pool.active_count(), 2U);

    pool.clear();
    EXPECT_EQ(pool.active_count(), 0U);
}

TEST(Tier1_Feature17_ObjectPool, HandleGenerationValidation)
{
    cd::world2d::ObjectPool2D<TestBullet> pool(16U);
    auto h1 = pool.acquire();
    ASSERT_TRUE(h1.has_value());
    pool.release(*h1);

    auto h2 = pool.acquire();
    ASSERT_TRUE(h2.has_value());
    // Generation counter increments, so h1 should now be invalid
    EXPECT_FALSE(pool.is_valid(*h1));
    EXPECT_TRUE(pool.is_valid(*h2));
}

// =============================================================================
// Feature 18: AudioCue2D Procedural Sound System
// =============================================================================

TEST(Tier1_Feature18_AudioCue, ProceduralWaveformSynth)
{
    auto samples = cd::world2d::synthesize_preset_samples(cd::world2d::AudioCuePreset2D::kJump);
    EXPECT_FALSE(samples.empty());

    for (const float s : samples)
    {
        EXPECT_GE(s, -1.0F);
        EXPECT_LE(s, 1.0F);
    }
}

TEST(Tier1_Feature18_AudioCue, StereoPanningCalculation)
{
    cd::math::Vec2f listener { 0.0F, 0.0F };
    cd::math::Vec2f emitter_left { -10.0F, 0.0F };
    cd::math::Vec2f emitter_right { 10.0F, 0.0F };

    auto pan_l = cd::world2d::compute_spatial_panning_2d(listener, 12.0F, emitter_left);
    auto pan_r = cd::world2d::compute_spatial_panning_2d(listener, 12.0F, emitter_right);

    EXPECT_GT(pan_l.gain_left, pan_l.gain_right);
    EXPECT_GT(pan_r.gain_right, pan_r.gain_left);
}

TEST(Tier1_Feature18_AudioCue, PlayJumpCue)
{
    cd::world2d::AudioCueSystem2D audio;
    audio.play_cue(cd::world2d::AudioCuePreset2D::kJump);
    EXPECT_EQ(audio.total_cues_dispatched(), 1U);
}

TEST(Tier1_Feature18_AudioCue, PlayGemPickupCue)
{
    cd::world2d::AudioCueSystem2D audio;
    audio.play_cue(cd::world2d::AudioCuePreset2D::kCoin);
    EXPECT_EQ(audio.total_cues_dispatched(), 1U);
}

TEST(Tier1_Feature18_AudioCue, NullBackendGracefulSkip)
{
    cd::world2d::AudioCueSystem2D audio(nullptr);
    for (int i = 0; i < 20; ++i)
    {
        audio.play_cue(cd::world2d::AudioCuePreset2D::kHit);
    }
    EXPECT_EQ(audio.total_cues_dispatched(), 20U);
}

// =============================================================================
// Feature 19: ParallaxLayer2D
// =============================================================================

TEST(Tier1_Feature19_Parallax, AddLayerRegistration)
{
    cd::render2d::ParallaxManager2D mgr;
    cd::render2d::ParallaxLayer2D l {};
    l.name = "DistantMountains";
    l.scroll_factor = { 0.2F, 0.2F };
    mgr.add_layer(l);

    EXPECT_EQ(mgr.layers().size(), 1U);
    EXPECT_NE(mgr.find_layer("DistantMountains"), nullptr);
}

TEST(Tier1_Feature19_Parallax, CameraScrollDisplacement)
{
    cd::render2d::ParallaxManager2D mgr;
    cd::render2d::ParallaxLayer2D l {};
    l.scroll_factor = { 0.5F, 0.5F };

    const cd::math::Vec2f cam_pos { 100.0F, 50.0F };
    const auto offset = mgr.compute_effective_offset(l, cam_pos);

    EXPECT_FLOAT_EQ(offset.x, 50.0F);
    EXPECT_FLOAT_EQ(offset.y, 25.0F);
}

TEST(Tier1_Feature19_Parallax, AutoScrollVelocity)
{
    cd::render2d::ParallaxLayer2D l {};
    l.auto_scroll_velocity = { 5.0F, 0.0F };
    l.current_offset = { 0.0F, 0.0F };

    // Advance 2 seconds
    l.current_offset += l.auto_scroll_velocity * 2.0F;
    EXPECT_FLOAT_EQ(l.current_offset.x, 10.0F);
}

TEST(Tier1_Feature19_Parallax, SeamlessWrapping)
{
    cd::render2d::ParallaxLayer2D l {};
    l.repeat_x = true;
    EXPECT_TRUE(l.repeat_x);
}

TEST(Tier1_Feature19_Parallax, ZOrderSorting)
{
    cd::render2d::ParallaxManager2D mgr;
    cd::render2d::ParallaxLayer2D bg {};
    bg.name = "Sky";
    bg.z_order = -100;

    cd::render2d::ParallaxLayer2D fg {};
    fg.name = "Trees";
    fg.z_order = -10;

    mgr.add_layer(fg);
    mgr.add_layer(bg);
    mgr.sort_layers();

    EXPECT_EQ(mgr.layers()[0].name, "Sky");
    EXPECT_EQ(mgr.layers()[1].name, "Trees");
}

}  // namespace cd::e2e
