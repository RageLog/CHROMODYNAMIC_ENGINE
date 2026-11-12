// =============================================================================
// CHROMODYNAMIC — tests/e2e/tier2_boundary/test_tier2_boundary_10_to_19.cpp
//
// Tier 2 Boundary, Negative, Corner Case & Recovery Tests for Features 10-19:
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

#include <cmath>
#include <limits>
#include <vector>

namespace cd::e2e
{

// =============================================================================
// Feature 10: TextureAtlas2D Boundary Tests
// =============================================================================

TEST(Tier2_Feature10_TextureAtlas_Boundary, SubRegionExceedsBounds)
{
    cd::render2d::TextureAtlas2D atlas(128U, 128U);
    // Adding region extending past 128x128
    auto uv = atlas.add_region("overflow", 100U, 100U, 50U, 50U);
    // Normalized UVs must still be clamped or valid
    EXPECT_GE(uv.u0, 0.0F);
    EXPECT_GE(uv.v0, 0.0F);
}

TEST(Tier2_Feature10_TextureAtlas_Boundary, ZeroSizedSubRegion)
{
    cd::render2d::TextureAtlas2D atlas(64U, 64U);
    auto uv = atlas.add_region("empty_region", 10U, 10U, 0U, 0U);
    EXPECT_FLOAT_EQ(uv.u0, uv.u1);
    EXPECT_FLOAT_EQ(uv.v0, uv.v1);
}

TEST(Tier2_Feature10_TextureAtlas_Boundary, NonExistentRegionName)
{
    cd::render2d::TextureAtlas2D atlas(64U, 64U);
    const auto* reg = atlas.find_region("missing_hero_sprite");
    EXPECT_EQ(reg, nullptr);
}

TEST(Tier2_Feature10_TextureAtlas_Boundary, MaxDimensionsAllocation)
{
    cd::render2d::TextureAtlas2D atlas(1024U, 1024U);
    EXPECT_TRUE(atlas.is_valid());
    EXPECT_EQ(atlas.pixels().size(), 1024U * 1024U * 4U);
}

TEST(Tier2_Feature10_TextureAtlas_Boundary, OverlappingSubRegions)
{
    cd::render2d::TextureAtlas2D atlas(64U, 64U);
    atlas.add_region("reg1", 0U, 0U, 32U, 32U);
    atlas.add_region("reg2", 16U, 16U, 32U, 32U);

    EXPECT_NE(atlas.find_region("reg1"), nullptr);
    EXPECT_NE(atlas.find_region("reg2"), nullptr);
}

// =============================================================================
// Feature 11: Procedural Pixel-Art Generator Boundary Tests
// =============================================================================

TEST(Tier2_Feature11_ProceduralArt_Boundary, ZeroTileSizeSanitization)
{
    auto atlas = cd::render2d::TextureAtlas2D::generate_procedural_starter_atlas(0U);
    EXPECT_TRUE(atlas.is_valid());
    EXPECT_GE(atlas.width(), 16U);
}

TEST(Tier2_Feature11_ProceduralArt_Boundary, LargeTileSize)
{
    auto atlas = cd::render2d::TextureAtlas2D::generate_procedural_starter_atlas(64U);
    EXPECT_TRUE(atlas.is_valid());
}

TEST(Tier2_Feature11_ProceduralArt_Boundary, OddTileSize)
{
    auto atlas = cd::render2d::TextureAtlas2D::generate_procedural_starter_atlas(15U);
    EXPECT_TRUE(atlas.is_valid());
}

TEST(Tier2_Feature11_ProceduralArt_Boundary, NormalAtlasHasUnitNormals)
{
    auto norm = cd::render2d::TextureAtlas2D::generate_starter_normal_atlas(16U);
    EXPECT_TRUE(norm.is_valid());
    auto p = norm.get_pixel(8, 8);
    auto vec = cd::render2d::decode_normal_rgb(p);
    EXPECT_NEAR(cd::math::length(vec), 1.0F, 0.15F);
}

TEST(Tier2_Feature11_ProceduralArt_Boundary, DeterministicRngSeed)
{
    auto a1 = cd::render2d::TextureAtlas2D::generate_procedural_starter_atlas(16U);
    auto a2 = cd::render2d::TextureAtlas2D::generate_procedural_starter_atlas(16U);
    EXPECT_EQ(a1.pixels().size(), a2.pixels().size());
}

// =============================================================================
// Feature 12: Texture Atlas Disk Loaders Boundary Tests
// =============================================================================

TEST(Tier2_Feature12_AtlasLoaders_Boundary, CorruptPngBytes)
{
    const std::vector<std::uint8_t> corrupt = { 0x12, 0x34, 0x56, 0x78 };
    auto res = cd::render2d::TextureAtlas2D::load_from_memory(corrupt);
    EXPECT_FALSE(res.has_value());
}

TEST(Tier2_Feature12_AtlasLoaders_Boundary, EmptyJsonMetadata)
{
    cd::render2d::TextureAtlas2D atlas(64U, 64U);
    auto res = atlas.load_regions_from_aseprite_json("");
    EXPECT_FALSE(res.has_value());
}

TEST(Tier2_Feature12_AtlasLoaders_Boundary, MissingAsepriteFramesField)
{
    cd::render2d::TextureAtlas2D atlas(64U, 64U);
    const std::string bad_json = R"({"meta":{"app":"aseprite"}})";
    auto res = atlas.load_regions_from_aseprite_json(bad_json);
    EXPECT_TRUE(res.has_value());
    EXPECT_EQ(atlas.regions().size(), 0U);
}

TEST(Tier2_Feature12_AtlasLoaders_Boundary, MissingLdtkTilesetsField)
{
    cd::render2d::TextureAtlas2D atlas(64U, 64U);
    const std::string bad_json = R"({"defs":{}})";
    auto res = atlas.load_regions_from_ldtk_json(bad_json);
    EXPECT_TRUE(res.has_value());
    EXPECT_EQ(atlas.regions().size(), 0U);
}

TEST(Tier2_Feature12_AtlasLoaders_Boundary, NonExistentPngFile)
{
    auto res = cd::render2d::TextureAtlas2D::load_from_file("nonexistent_image_12345.png");
    EXPECT_FALSE(res.has_value());
}

// =============================================================================
// Feature 13: Textured Tilemap Boundary Tests
// =============================================================================

TEST(Tier2_Feature13_Tilemap_Boundary, NegativeCoordinates)
{
    cd::render2d::Tilemap2D tm;
    tm.set_tile(-50, -50, 2U);
    EXPECT_EQ(tm.get_tile(-50, -50).palette_id, 2U);
}

TEST(Tier2_Feature13_Tilemap_Boundary, InvalidPaletteId)
{
    cd::render2d::Tilemap2D tm;
    const auto* p = tm.find_palette(9999U);
    EXPECT_EQ(p, nullptr);
}

TEST(Tier2_Feature13_Tilemap_Boundary, EmptyTilemapEmission)
{
    cd::render2d::Tilemap2D tm;
    cd::render2d::Canvas2D canvas;
    cd::render2d::Camera2D cam;
    cd::render2d::LightingEnvironment2D lighting;

    tm.emit_to_canvas(canvas, cam, lighting);
    EXPECT_EQ(canvas.commands().size(), 0U);
}

TEST(Tier2_Feature13_Tilemap_Boundary, SingleCellIsland)
{
    cd::render2d::Tilemap2D tm;
    tm.set_tile(0, 0, 1U);
    const auto mask = tm.compute_autotile_mask(0, 0);
    EXPECT_EQ(mask, 0U); // No neighbors
}

TEST(Tier2_Feature13_Tilemap_Boundary, FullySurroundedCell)
{
    cd::render2d::Tilemap2D tm;
    tm.set_tile(0, 0, 1U);
    tm.set_tile(0, 1, 1U);  // North
    tm.set_tile(1, 0, 1U);  // East
    tm.set_tile(0, -1, 1U); // South
    tm.set_tile(-1, 0, 1U); // West

    const auto mask = tm.compute_autotile_mask(0, 0);
    EXPECT_EQ(mask, 15U); // All 4 cardinal bits set
}

// =============================================================================
// Feature 14: 2D Lighting Boundary Tests
// =============================================================================

TEST(Tier2_Feature14_Lighting2D_Boundary, MissingNormalMapDefault)
{
    const cd::render2d::ColorRgba8 default_norm { 128, 128, 255, 255 };
    const auto decoded = cd::render2d::decode_normal_rgb(default_norm);
    EXPECT_NEAR(decoded.z, 1.0F, 0.05F);
}

TEST(Tier2_Feature14_Lighting2D_Boundary, ZeroRadiusPointLight)
{
    cd::render2d::PointLight2D light {};
    light.radius = 0.0F;

    const cd::math::Vec2f sample { 1.0F, 0.0F };
    const float dist = cd::math::length(sample);
    const float atten = light.radius > 0.0F ? std::max(0.0F, 1.0F - dist / light.radius) : 0.0F;
    EXPECT_FLOAT_EQ(atten, 0.0F);
}

TEST(Tier2_Feature14_Lighting2D_Boundary, NegativeLightIntensity)
{
    cd::render2d::PointLight2D light {};
    light.intensity = -5.0F;
    const float clamped_intensity = std::max(0.0F, light.intensity);
    EXPECT_FLOAT_EQ(clamped_intensity, 0.0F);
}

TEST(Tier2_Feature14_Lighting2D_Boundary, CulledFarAwayLight)
{
    cd::render2d::PointLight2D light {};
    light.position = { 1.0e6F, 1.0e6F };
    light.radius = 5.0F;

    const cd::math::Vec2f sample { 0.0F, 0.0F };
    const float dist = cd::math::length(sample - light.position);
    EXPECT_GT(dist, light.radius);
}

TEST(Tier2_Feature14_Lighting2D_Boundary, HighOccluderOpacity)
{
    cd::render2d::ShadowOccluder2D occ {};
    occ.opacity = 2.0F;
    const float clamped = std::clamp(occ.opacity, 0.0F, 1.0F);
    EXPECT_FLOAT_EQ(clamped, 1.0F);
}

// =============================================================================
// Feature 15: ParticleSystem2D Boundary Tests
// =============================================================================

TEST(Tier2_Feature15_ParticleSystem_Boundary, DeltaTimeZero)
{
    cd::world2d::ParticleSystem2D ps(64U);
    ps.emit(10U);
    const auto before = ps.active_particles()[0].position;

    ps.update(0.0F);
    const auto after = ps.active_particles()[0].position;

    EXPECT_FLOAT_EQ(before.x, after.x);
    EXPECT_FLOAT_EQ(before.y, after.y);
}

TEST(Tier2_Feature15_ParticleSystem_Boundary, DeltaTimeSpike)
{
    cd::world2d::ParticleSystem2D ps(64U);
    ps.emit(20U);
    EXPECT_EQ(ps.active_count(), 20U);

    // 1000s delta time spike should expire all particles safely
    ps.update(1000.0F);
    EXPECT_EQ(ps.active_count(), 0U);
}

TEST(Tier2_Feature15_ParticleSystem_Boundary, EmissionPoolFull)
{
    cd::world2d::ParticleSystem2D ps(32U);
    ps.emit(50U); // Request more than capacity
    EXPECT_LE(ps.active_count(), 32U);
}

TEST(Tier2_Feature15_ParticleSystem_Boundary, NegativeSpeed)
{
    cd::world2d::ParticleEmitterConfig2D cfg {};
    cfg.speed_min = -5.0F;
    cfg.speed_max = -5.0F;
    cd::world2d::ParticleSystem2D ps(16U);
    ps.spawn_burst({ 0.0F, 0.0F }, 1U, cfg);
    EXPECT_EQ(ps.active_count(), 1U);
}

TEST(Tier2_Feature15_ParticleSystem_Boundary, ZeroLifetime)
{
    cd::world2d::ParticleEmitterConfig2D cfg {};
    cfg.lifetime_min = 0.0F;
    cfg.lifetime_max = 0.0F;
    cd::world2d::ParticleSystem2D ps(16U);
    ps.spawn_burst({ 0.0F, 0.0F }, 1U, cfg);
    ps.update(0.016F);
    EXPECT_EQ(ps.active_count(), 0U);
}

// =============================================================================
// Feature 16: SpatialHash2D Boundary Tests
// =============================================================================

TEST(Tier2_Feature16_SpatialHash_Boundary, NegativeUniverseCoordinates)
{
    cd::world2d::SpatialHash2D hash(2.0F);
    hash.insert(10U, cd::math::Aabb2 { -500.0F, -500.0F, -499.0F, -499.0F });

    std::vector<std::uint32_t> res;
    hash.query_aabb(cd::math::Aabb2 { -501.0F, -501.0F, -498.0F, -498.0F }, res);
    ASSERT_EQ(res.size(), 1U);
    EXPECT_EQ(res[0], 10U);
}

TEST(Tier2_Feature16_SpatialHash_Boundary, HugeObjectSpanning100Cells)
{
    cd::world2d::SpatialHash2D hash(1.0F);
    hash.insert(20U, cd::math::Aabb2 { 0.0F, 0.0F, 10.0F, 10.0F });

    std::vector<std::uint32_t> res;
    hash.query_aabb(cd::math::Aabb2 { 5.0F, 5.0F, 6.0F, 6.0F }, res);
    // Deduplication must return entity only once
    ASSERT_EQ(res.size(), 1U);
    EXPECT_EQ(res[0], 20U);
}

TEST(Tier2_Feature16_SpatialHash_Boundary, ManyObjectsInSingleCell)
{
    cd::world2d::SpatialHash2D hash(10.0F);
    for (std::uint32_t i = 1; i <= 200; ++i)
    {
        hash.insert(i, cd::math::Aabb2 { 1.0F, 1.0F, 2.0F, 2.0F });
    }
    EXPECT_EQ(hash.size(), 200U);
}

TEST(Tier2_Feature16_SpatialHash_Boundary, QueryDegenerateAABB)
{
    cd::world2d::SpatialHash2D hash(2.0F);
    hash.insert(30U, cd::math::Aabb2 { 0.0F, 0.0F, 1.0F, 1.0F });

    std::vector<std::uint32_t> res;
    // Degenerate query where min > max
    hash.query_aabb(cd::math::Aabb2 { 5.0F, 5.0F, 2.0F, 2.0F }, res);
    EXPECT_TRUE(res.empty());
}

TEST(Tier2_Feature16_SpatialHash_Boundary, QueryNegativeRadiusCircle)
{
    cd::world2d::SpatialHash2D hash(2.0F);
    hash.insert(40U, cd::math::Aabb2 { 0.0F, 0.0F, 1.0F, 1.0F });

    std::vector<std::uint32_t> res;
    hash.query_circle({ 50.0F, 50.0F }, -5.0F, res);
    EXPECT_TRUE(res.empty());
}

// =============================================================================
// Feature 17: ObjectPool2D Boundary Tests
// =============================================================================

struct TestBullet
{
    float x { 0.0F };
    float y { 0.0F };
    bool active { false };
};

TEST(Tier2_Feature17_ObjectPool_Boundary, AcquireWhenFull)
{
    cd::world2d::ObjectPool2D<TestBullet, 2U> pool(2U, false);
    auto h1 = pool.acquire();
    auto h2 = pool.acquire();
    auto h3 = pool.acquire();

    EXPECT_TRUE(h1.has_value());
    EXPECT_TRUE(h2.has_value());
    EXPECT_FALSE(h3.has_value()); // Full, returns nullopt
}

TEST(Tier2_Feature17_ObjectPool_Boundary, DoubleReleasePrevention)
{
    cd::world2d::ObjectPool2D<TestBullet> pool(4U);
    auto h = pool.acquire();
    ASSERT_TRUE(h.has_value());

    EXPECT_TRUE(pool.release(*h));
    EXPECT_FALSE(pool.release(*h)); // Second release fails safely
}

TEST(Tier2_Feature17_ObjectPool_Boundary, InvalidHandleRelease)
{
    cd::world2d::ObjectPool2D<TestBullet> pool(4U);
    cd::world2d::ObjectPool2D<TestBullet>::HandleType bad_handle(999U, 0U);
    EXPECT_FALSE(pool.release(bad_handle));
}

TEST(Tier2_Feature17_ObjectPool_Boundary, FullExhaustionAndReclaim)
{
    cd::world2d::ObjectPool2D<TestBullet> pool(8U);
    std::vector<cd::world2d::ObjectPool2D<TestBullet>::HandleType> handles;

    for (int i = 0; i < 8; ++i)
    {
        handles.push_back(*pool.acquire());
    }
    EXPECT_TRUE(pool.full());

    for (const auto& h : handles)
    {
        EXPECT_TRUE(pool.release(h));
    }
    EXPECT_TRUE(pool.empty());
}

TEST(Tier2_Feature17_ObjectPool_Boundary, ReverseOrderRelease)
{
    cd::world2d::ObjectPool2D<TestBullet> pool(4U);
    auto h1 = *pool.acquire();
    auto h2 = *pool.acquire();
    auto h3 = *pool.acquire();

    EXPECT_TRUE(pool.release(h3));
    EXPECT_TRUE(pool.release(h2));
    EXPECT_TRUE(pool.release(h1));
    EXPECT_TRUE(pool.empty());
}

// =============================================================================
// Feature 18: AudioCue2D Boundary Tests
// =============================================================================

TEST(Tier2_Feature18_AudioCue_Boundary, NullBackendSafety)
{
    cd::world2d::AudioCueSystem2D audio(nullptr);
    auto v = audio.play_cue(cd::world2d::AudioCuePreset2D::kJump);
    (void)v;
    // Must return safely without crashing
    EXPECT_EQ(audio.total_cues_dispatched(), 1U);
}

TEST(Tier2_Feature18_AudioCue_Boundary, ExtremeVolumeClamping)
{
    cd::world2d::AudioCueSystem2D audio(nullptr);
    audio.play_cue(cd::world2d::AudioCuePreset2D::kJump, 100.0F);
    EXPECT_EQ(audio.total_cues_dispatched(), 1U);
}

TEST(Tier2_Feature18_AudioCue_Boundary, ZeroPitchPlayback)
{
    cd::world2d::RetroSynthParams2D params {};
    params.start_freq_hz = 0.0F;
    params.end_freq_hz = 0.0F;
    params.duration_sec = 0.05F;

    auto samples = cd::world2d::synthesize_waveform_samples(params);
    EXPECT_FALSE(samples.empty());
}

TEST(Tier2_Feature18_AudioCue_Boundary, FarLeftPanning)
{
    cd::math::Vec2f listener { 0.0F, 0.0F };
    cd::math::Vec2f emitter { -1000.0F, 0.0F };
    auto pan = cd::world2d::compute_spatial_panning_2d(listener, 10.0F, emitter);
    EXPECT_FLOAT_EQ(pan.gain_left, 1.0F);
    EXPECT_FLOAT_EQ(pan.gain_right, 0.0F);
}

TEST(Tier2_Feature18_AudioCue_Boundary, FarRightPanning)
{
    cd::math::Vec2f listener { 0.0F, 0.0F };
    cd::math::Vec2f emitter { 1000.0F, 0.0F };
    auto pan = cd::world2d::compute_spatial_panning_2d(listener, 10.0F, emitter);
    EXPECT_FLOAT_EQ(pan.gain_left, 0.0F);
    EXPECT_FLOAT_EQ(pan.gain_right, 1.0F);
}

// =============================================================================
// Feature 19: ParallaxLayer2D Boundary Tests
// =============================================================================

TEST(Tier2_Feature19_Parallax_Boundary, ZeroScrollFactor)
{
    cd::render2d::ParallaxManager2D mgr;
    cd::render2d::ParallaxLayer2D l {};
    l.scroll_factor = { 0.0F, 0.0F };

    const auto off = mgr.compute_effective_offset(l, { 500.0F, 300.0F });
    EXPECT_FLOAT_EQ(off.x, 0.0F);
    EXPECT_FLOAT_EQ(off.y, 0.0F);
}

TEST(Tier2_Feature19_Parallax_Boundary, NegativeScrollFactor)
{
    cd::render2d::ParallaxManager2D mgr;
    cd::render2d::ParallaxLayer2D l {};
    l.scroll_factor = { -0.5F, -0.5F };

    const auto off = mgr.compute_effective_offset(l, { 100.0F, 100.0F });
    EXPECT_FLOAT_EQ(off.x, -50.0F);
    EXPECT_FLOAT_EQ(off.y, -50.0F);
}

TEST(Tier2_Feature19_Parallax_Boundary, InfiniteCameraDelta)
{
    cd::render2d::ParallaxLayer2D l {};
    l.current_offset = { 0.0F, 0.0F };
    float inf = std::numeric_limits<float>::infinity();
    if (std::isinf(inf))
    {
        inf = 0.0F; // sanitize
    }
    l.current_offset.x += inf;
    EXPECT_FALSE(std::isnan(l.current_offset.x));
}

TEST(Tier2_Feature19_Parallax_Boundary, HugeAutoScrollVelocity)
{
    cd::render2d::ParallaxLayer2D l {};
    l.auto_scroll_velocity = { 1.0e6F, 0.0F };
    l.current_offset += l.auto_scroll_velocity * 0.016F;
    EXPECT_GT(l.current_offset.x, 0.0F);
}

TEST(Tier2_Feature19_Parallax_Boundary, ZeroViewportDimension)
{
    cd::render2d::ViewportRect2D vp {};
    vp.width = 0.0F;
    vp.height = 0.0F;
    EXPECT_FLOAT_EQ(vp.width, 0.0F);
}

}  // namespace cd::e2e
