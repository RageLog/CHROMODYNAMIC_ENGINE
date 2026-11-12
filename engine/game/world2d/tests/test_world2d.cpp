// =============================================================================
// CHROMODYNAMIC: test_world2d.cpp
// End-to-end unit tests for cd::world2d (Stage2D, CharacterController2D +
// PhysicsWorld2D, Sandboxed Lua 5.4 behaviors, CommandBus2D Undo/Redo + AI JSON
// protocol, PlaytestRunner2D autopilot telemetry, and .cdpak round-trip).
// =============================================================================
#include <cd/world2d/AudioCue2D.hpp>
#include <cd/world2d/CommandBus2D.hpp>
#include <cd/world2d/GenreTemplates2D.hpp>
#include <cd/world2d/ObjectPool2D.hpp>
#include <cd/world2d/ParticleSystem2D.hpp>
#include <cd/world2d/PlaytestRunner2D.hpp>
#include <cd/world2d/SpatialHash2D.hpp>
#include <cd/world2d/Stage2D.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_set>
#include <vector>

namespace
{

TEST(World2DTest, StarterStageRunsPlaytestLuaAndReachesVictoryDeterministically)
{
    cd::world2d::Stage2D stage {};
    const std::size_t    initial_count = stage.entities().size();
    ASSERT_GT(initial_count, 6U);

    const auto report = cd::world2d::run_playtest_2d(stage, 360U, true, true);
    EXPECT_GT(report.frames_simulated, 30U);
    EXPECT_GT(report.horizontal_distance_covered, 8.0F);
    EXPECT_GE(report.gems_collected, 1);
    EXPECT_EQ(report.active_lua_vms, 1U);
    EXPECT_EQ(report.lua_error_count, 0U);
    EXPECT_FALSE(report.game_over);

    // Verify `restore_stage_after = true` restored exact pre-play authoring state!
    EXPECT_FALSE(stage.is_playing());
    EXPECT_EQ(stage.entities().size(), initial_count);
}

TEST(World2DTest, CommandBusExecutesAiJsonBatchAndSupportsFullUndoRedo)
{
    cd::world2d::Stage2D      stage {};
    cd::world2d::CommandBus2D bus {};

    const std::size_t base_entities = stage.entities().size();
    const std::size_t base_tiles    = stage.tilemap().non_empty_tile_count();

    constexpr std::string_view kAiBatch = R"({
      "commands": [
        { "op": "paint_rect_2d", "x0": 30, "y0": 0, "x1": 35, "y1": 1, "palette_id": 4 },
        {
          "op": "spawn_entity_2d",
          "name": "AI_Lua_HoverOrb",
          "role": 6,
          "pos": [32.0, 4.0],
          "size": [1.5, 0.5],
          "lua_script": "function on_tick(id, x, y, vx, vy, t, dt) return x, 4.0 + math.sin(t * 2.0), 0.0, 0.0 end"
        }
      ]
    })";

    const auto rep = bus.execute_json(stage, kAiBatch);
    ASSERT_TRUE(rep.ok) << rep.error_message;
    EXPECT_EQ(rep.applied_count, 2U);
    EXPECT_EQ(stage.entities().size(), base_entities + 1U);
    EXPECT_EQ(stage.tilemap().non_empty_tile_count(), base_tiles + 12U);

    // Human presses Ctrl+Z -> entire AI batch unrolls atomically!
    ASSERT_TRUE(bus.undo(stage));
    EXPECT_EQ(stage.entities().size(), base_entities);
    EXPECT_EQ(stage.tilemap().non_empty_tile_count(), base_tiles);

    // Human presses Ctrl+Y -> AI batch reapplies cleanly!
    ASSERT_TRUE(bus.redo(stage));
    EXPECT_EQ(stage.entities().size(), base_entities + 1U);
    EXPECT_EQ(stage.tilemap().non_empty_tile_count(), base_tiles + 12U);
}

TEST(World2DTest, StandaloneCdpakArchiveRoundTripsStageLosslessly)
{
    cd::world2d::Stage2D src {};
    src.set_name("Shipped_2D_Adventure");
    const auto pak_bytes = src.build_standalone_pak_bytes();
    ASSERT_GT(pak_bytes.size(), 64U);

    cd::world2d::Stage2D dst {};
    dst.clear();
    ASSERT_TRUE(dst.load_from_pak_bytes(pak_bytes).has_value());
    EXPECT_EQ(dst.name(), "Shipped_2D_Adventure");
    EXPECT_EQ(dst.entities().size(), src.entities().size());
    EXPECT_EQ(dst.tilemap().non_empty_tile_count(), src.tilemap().non_empty_tile_count());
}

// -----------------------------------------------------------------------------
// Milestone 3 Features 15-18 Acceptance Tests
// -----------------------------------------------------------------------------

TEST(World2DTest, ParticleSystem2D_EmissionLifecycleAndZeroAllocationPool)
{
    cd::world2d::ParticleConfig2D cfg {};
    cfg.shape        = cd::world2d::EmitterShape2D::kPoint;
    cfg.lifetime_min = 1.0F;
    cfg.lifetime_max = 1.0F;
    cfg.velocity_min = { 0.0F, 10.0F };
    cfg.velocity_max = { 0.0F, 10.0F };
    cfg.gravity      = { 0.0F, -2.0F };

    cd::world2d::ParticleSystem2D sys { 50U };
    sys.set_config(cfg);
    EXPECT_EQ(sys.active_particle_count(), 0U);

    sys.emit(30U);
    EXPECT_EQ(sys.active_particle_count(), 30U);

    // Over-emission clamped to pool capacity (50)
    sys.emit(40U);
    EXPECT_EQ(sys.active_particle_count(), 50U);

    // Step forward 0.5s: particles still alive
    sys.update(0.5F);
    EXPECT_EQ(sys.active_particle_count(), 50U);

    // Step forward 0.6s (total 1.1s > 1.0s lifetime): all particles expire
    sys.update(0.6F);
    EXPECT_EQ(sys.active_particle_count(), 0U);

    // Emitting to canvas when empty produces 0 commands
    cd::render2d::Canvas2D canvas {};
    sys.emit_to_canvas(canvas);
    EXPECT_EQ(canvas.commands().size(), 0U);
}

TEST(World2DTest, SpatialHash2D_InsertQueryAndDeduplication)
{
    cd::world2d::SpatialHash2D hash { 4.0F };

    // Insert entity 1 at [1, 1] .. [2, 2]
    hash.insert(1U, { { 1.0F, 1.0F }, { 2.0F, 2.0F } });

    // Insert entity 2 straddling the boundary [3.5, 3.5] .. [4.5, 4.5] (spans 4 cells!)
    hash.insert(2U, { { 3.5F, 3.5F }, { 4.5F, 4.5F } });

    // Query covering entity 2
    const auto q_res = hash.query_aabb({ { 3.0F, 3.0F }, { 5.0F, 5.0F } });
    ASSERT_EQ(q_res.size(), 1U);
    EXPECT_EQ(q_res.front(), 2U);  // Deduplicated!

    // Circle query covering entity 1
    const auto c_res = hash.query_circle({ { 1.5F, 1.5F }, 1.0F });
    ASSERT_EQ(c_res.size(), 1U);
    EXPECT_EQ(c_res.front(), 1U);

    // Move entity 1 far away
    hash.update(1U, { { 100.0F, 100.0F }, { 101.0F, 101.0F } });
    const auto empty_res = hash.query_circle({ { 1.5F, 1.5F }, 1.0F });
    EXPECT_TRUE(empty_res.empty());

    hash.clear();
    EXPECT_EQ(hash.entity_count(), 0U);
}

struct DummyProjectile
{
    float x { 0.0F };
    float y { 0.0F };
    bool  alive { false };
};

TEST(World2DTest, ObjectPool2D_AcquireReleaseAndGenerationalSafety)
{
    cd::world2d::ObjectPool2D<DummyProjectile, 3> pool {};
    EXPECT_EQ(pool.capacity(), 3U);
    EXPECT_EQ(pool.active_count(), 0U);

    const auto h0 = pool.acquire();
    const auto h1 = pool.acquire();
    const auto h2 = pool.acquire();
    ASSERT_TRUE(h0.has_value());
    ASSERT_TRUE(h1.has_value());
    ASSERT_TRUE(h2.has_value());
    EXPECT_EQ(pool.active_count(), 3U);

    // Capacity exhaustion returns nullopt
    const auto h3 = pool.acquire();
    EXPECT_FALSE(h3.has_value());

    // Release h1
    EXPECT_TRUE(pool.release(*h1));
    EXPECT_EQ(pool.active_count(), 2U);
    EXPECT_FALSE(pool.is_valid(*h1));  // Old handle invalid

    // Re-acquire slot: receives new generation
    const auto h1_new = pool.acquire();
    ASSERT_TRUE(h1_new.has_value());
    EXPECT_EQ(h1_new->index, h1->index);
    EXPECT_NE(h1_new->generation, h1->generation);  // Generational safety!
    EXPECT_FALSE(pool.is_valid(*h1));               // ABA protection preserved!
    EXPECT_TRUE(pool.is_valid(*h1_new));
}

TEST(World2DTest, AudioCue2D_SynthesizerOutputAndNullBackendSafety)
{
    // 1. Procedural waveform synthesis
    cd::world2d::ProceduralWaveformConfig2D cfg {};
    cfg.wave          = cd::world2d::WaveformKind2D::kSquare;
    cfg.start_freq_hz = 440.0F;
    cfg.end_freq_hz   = 220.0F;
    cfg.duration_sec  = 0.10F;
    const auto samples = cd::world2d::synthesize_waveform_samples(cfg, 44100U);
    EXPECT_EQ(samples.size(), 4410U);

    for (float s : samples)
    {
        EXPECT_GE(s, -1.0F);
        EXPECT_LE(s, 1.0F);
    }

    // 2. Spatial 2D stereo panning
    const cd::math::Vec2f listener_pos { 0.0F, 0.0F };
    const float stereo_width = 10.0F;

    // Emitter far left
    const auto pan_left = cd::world2d::compute_spatial_panning_2d(
        listener_pos, stereo_width, { -15.0F, 0.0F });
    EXPECT_GT(pan_left.gain_left, 0.8F);
    EXPECT_LT(pan_left.gain_right, 0.2F);

    // Emitter centered
    const auto pan_center = cd::world2d::compute_spatial_panning_2d(
        listener_pos, stereo_width, { 0.0F, 0.0F });
    EXPECT_FLOAT_EQ(pan_center.gain_left, 1.0F);
    EXPECT_FLOAT_EQ(pan_center.gain_right, 1.0F);

    // 3. Null backend safety (headless simulation)
    cd::world2d::AudioCueSystem2D cue_sys { nullptr };
    cue_sys.play_cue(cd::world2d::AudioCueKind2D::kHeroJump, { 0.0F, 0.0F });
    EXPECT_EQ(cue_sys.total_cues_dispatched(), 1U);  // Clean no-op without crash!
}

// =============================================================================
// Milestone 3 Adversarial Stress & Robustness Tests
// (Challenger 2 Verification Matrix)
// =============================================================================

TEST(World2DAdversarialTest, SpatialHash2D_ExtremeCoordinatesAndInvertedBounds)
{
    cd::world2d::SpatialHash2D hash { 4.0F };

    // 1. Extreme large negative coordinates
    hash.insert(1001U, { { -100000.0F, -100000.0F }, { -99990.0F, -99990.0F } });
    EXPECT_TRUE(hash.contains(1001U));
    const auto* b1 = hash.find_bounds(1001U);
    ASSERT_NE(b1, nullptr);
    EXPECT_FLOAT_EQ(b1->min.x, -100000.0F);

    auto q_neg = hash.query_aabb({ { -100005.0F, -100005.0F }, { -99980.0F, -99980.0F } });
    ASSERT_EQ(q_neg.size(), 1U);
    EXPECT_EQ(q_neg.front(), 1001U);

    // Query non-overlapping negative region
    auto q_neg_miss = hash.query_aabb({ { -50000.0F, -50000.0F }, { -49990.0F, -49990.0F } });
    EXPECT_TRUE(q_neg_miss.empty());

    // 2. Coincident at origin with negative and positive zero
    hash.insert(1002U, { { -0.0F, -0.0F }, { 0.0F, 0.0F } });
    EXPECT_TRUE(hash.contains(1002U));
    auto q_zero = hash.query_point({ 0.0F, 0.0F });
    ASSERT_GE(q_zero.size(), 1U);
    EXPECT_TRUE(std::find(q_zero.begin(), q_zero.end(), 1002U) != q_zero.end());

    // 3. Extreme large positive coordinates (near INT32 safe ranges)
    hash.insert(1003U, { { 4000000.0F, 4000000.0F }, { 4000010.0F, 4000010.0F } });
    EXPECT_TRUE(hash.contains(1003U));
    auto q_pos = hash.query_aabb({ { 3999990.0F, 3999990.0F }, { 4000020.0F, 4000020.0F } });
    ASSERT_EQ(q_pos.size(), 1U);
    EXPECT_EQ(q_pos.front(), 1003U);

    // 4. Inverted bounds query (min > max): must safely return empty results
    auto q_inv = hash.query_aabb({ { 10.0F, 10.0F }, { -10.0F, -10.0F } });
    EXPECT_TRUE(q_inv.empty());

    // 5. Circle query at extreme coordinates
    auto q_circ_neg = hash.query_circle({ -99995.0F, -99995.0F }, 20.0F);
    ASSERT_EQ(q_circ_neg.size(), 1U);
    EXPECT_EQ(q_circ_neg.front(), 1001U);
}

TEST(World2DAdversarialTest, SpatialHash2D_DenseClusterTenThousandEntities)
{
    cd::world2d::SpatialHash2D hash { 10.0F, 1024U };

    constexpr std::uint32_t kClusterSize = 10000U;
    // Insert 10,000 entities clustered in cell [1.0, 1.0] .. [2.0, 2.0]
    for (std::uint32_t i = 1U; i <= kClusterSize; ++i)
    {
        hash.insert(i, { { 1.0F, 1.0F }, { 2.0F, 2.0F } });
    }

    EXPECT_EQ(hash.size(), kClusterSize);
    EXPECT_EQ(hash.entity_count(), kClusterSize);

    // Query covering the cluster cell
    std::vector<std::uint32_t> results;
    hash.query_aabb({ { 0.0F, 0.0F }, { 5.0F, 5.0F } }, results);
    EXPECT_EQ(results.size(), kClusterSize);

    // Verify all IDs are unique (zero duplicates)
    std::unordered_set<std::uint32_t> unique_ids(results.begin(), results.end());
    EXPECT_EQ(unique_ids.size(), kClusterSize);

    // Query outside the cell: must return empty
    results.clear();
    hash.query_aabb({ { 20.0F, 20.0F }, { 25.0F, 25.0F } }, results);
    EXPECT_TRUE(results.empty());

    // Point query inside
    hash.query_point({ 1.5F, 1.5F }, results);
    EXPECT_EQ(results.size(), kClusterSize);

    // Clear
    hash.clear();
    EXPECT_EQ(hash.size(), 0U);
    EXPECT_TRUE(hash.empty());
}

TEST(World2DAdversarialTest, SpatialHash2D_MovingEntitiesAcrossCellBoundaries)
{
    cd::world2d::SpatialHash2D hash { 2.0F };

    // Entity 42 in cell (0, 0): [0.2, 0.2] .. [0.8, 0.8]
    hash.insert(42U, { { 0.2F, 0.2F }, { 0.8F, 0.8F } });

    auto q_origin = hash.query_aabb({ { 0.0F, 0.0F }, { 1.0F, 1.0F } });
    ASSERT_EQ(q_origin.size(), 1U);
    EXPECT_EQ(q_origin.front(), 42U);

    // Move to cell (5, 5): [10.2, 10.2] .. [10.8, 10.8]
    hash.update(42U, { { 10.2F, 10.2F }, { 10.8F, 10.8F } });

    // Old cell must be empty
    q_origin = hash.query_aabb({ { 0.0F, 0.0F }, { 1.0F, 1.0F } });
    EXPECT_TRUE(q_origin.empty());

    // New cell must contain entity 42
    auto q_new = hash.query_aabb({ { 10.0F, 10.0F }, { 11.0F, 11.0F } });
    ASSERT_EQ(q_new.size(), 1U);
    EXPECT_EQ(q_new.front(), 42U);

    // Move entity to straddle across 4 cells around origin: [-0.5, -0.5] .. [0.5, 0.5]
    // With cell_size = 2.0, this touches cells (-1, -1), (-1, 0), (0, -1), (0, 0)
    hash.update(42U, { { -0.5F, -0.5F }, { 0.5F, 0.5F } });

    // Query covering all 4 cells: must return entity 42 exactly once
    auto q_straddle = hash.query_aabb({ { -1.0F, -1.0F }, { 1.0F, 1.0F } });
    ASSERT_EQ(q_straddle.size(), 1U);
    EXPECT_EQ(q_straddle.front(), 42U);

    // Remove
    EXPECT_TRUE(hash.remove(42U));
    EXPECT_FALSE(hash.contains(42U));
    EXPECT_TRUE(hash.query_aabb({ { -1.0F, -1.0F }, { 1.0F, 1.0F } }).empty());
}

TEST(World2DAdversarialTest, SpatialHash2D_LargeAabbSpanningOver100CellsDeduplication)
{
    cd::world2d::SpatialHash2D hash { 1.0F };

    // Insert 50 entities across a 20x20 area (400 cells total)
    for (std::uint32_t i = 0U; i < 50U; ++i)
    {
        const float x = static_cast<float>((i % 7U) * 2U);
        const float y = static_cast<float>((i / 7U) * 2U);
        hash.insert(i + 1U, { { x - 0.2F, y - 0.2F }, { x + 1.2F, y + 1.2F } });
    }

    // Query box covering [-1.0, -1.0] to [15.0, 15.0] (spans 16x16 = 256 cells!)
    const auto q_res = hash.query_aabb({ { -1.0F, -1.0F }, { 15.0F, 15.0F } });
    EXPECT_EQ(q_res.size(), 50U);

    // Verify deduplication: strictly unique IDs
    std::unordered_set<std::uint32_t> seen;
    for (std::uint32_t id : q_res)
    {
        EXPECT_TRUE(seen.insert(id).second) << "Duplicate ID in query result: " << id;
    }
}

TEST(World2DAdversarialTest, ObjectPool2D_CapacityExhaustionAndRejection)
{
    cd::world2d::ObjectPool2D<DummyProjectile, 4> pool { 4U, false };
    EXPECT_EQ(pool.capacity(), 4U);
    EXPECT_EQ(pool.available_count(), 4U);

    std::vector<cd::world2d::PoolHandle<DummyProjectile>> handles;
    for (int i = 0; i < 4; ++i)
    {
        auto h = pool.acquire();
        ASSERT_TRUE(h.has_value());
        handles.push_back(*h);
    }

    EXPECT_TRUE(pool.full());
    EXPECT_EQ(pool.available_count(), 0U);
    EXPECT_EQ(pool.active_count(), 4U);

    // 5th acquisition MUST return nullopt (exhaustion)
    auto h_fail = pool.acquire();
    EXPECT_FALSE(h_fail.has_value());

    // Release 1 item
    EXPECT_TRUE(pool.release(handles.back()));
    handles.pop_back();
    EXPECT_FALSE(pool.full());
    EXPECT_EQ(pool.available_count(), 1U);
    EXPECT_EQ(pool.active_count(), 3U);

    // Now acquisition succeeds again
    auto h_success = pool.acquire();
    ASSERT_TRUE(h_success.has_value());
    EXPECT_TRUE(pool.is_valid(*h_success));
}

TEST(World2DAdversarialTest, ObjectPool2D_RapidChurn100kCycles)
{
    cd::world2d::ObjectPool2D<DummyProjectile> pool { 64U, false };

    constexpr std::size_t kIterations = 100000U;
    std::vector<cd::world2d::PoolHandle<DummyProjectile>> active_handles;
    active_handles.reserve(64U);

    // Deterministic pseudo-random LCG for reproducible churn
    std::uint32_t lcg = 123456789U;
    auto next_rand = [&lcg]() {
        lcg = lcg * 1664525U + 1013904223U;
        return lcg;
    };

    for (std::size_t iter = 0U; iter < kIterations; ++iter)
    {
        const bool should_acquire = active_handles.empty() ||
            (active_handles.size() < 60U && (next_rand() % 100U) < 55U);

        if (should_acquire)
        {
            auto h = pool.acquire();
            if (h.has_value())
            {
                auto* ptr = pool.get(*h);
                ASSERT_NE(ptr, nullptr);
                ptr->x = static_cast<float>(iter);
                ptr->alive = true;
                active_handles.push_back(*h);
            }
        }
        else
        {
            const std::size_t idx = (next_rand() % active_handles.size());
            const auto h = active_handles[idx];
            ASSERT_TRUE(pool.is_valid(h));
            ASSERT_TRUE(pool.release(h));
            active_handles[idx] = active_handles.back();
            active_handles.pop_back();
        }
    }

    EXPECT_EQ(pool.active_count(), active_handles.size());
    for (const auto& h : active_handles)
    {
        EXPECT_TRUE(pool.is_valid(h));
        EXPECT_NE(pool.get(h), nullptr);
        EXPECT_TRUE(pool.release(h));
    }

    EXPECT_EQ(pool.active_count(), 0U);
    EXPECT_TRUE(pool.empty());
    EXPECT_EQ(pool.available_count(), pool.capacity());
}

TEST(World2DAdversarialTest, ObjectPool2D_DoubleFreeAndStaleHandleSafety)
{
    cd::world2d::ObjectPool2D<DummyProjectile, 8> pool { 8U, false };

    auto h = pool.acquire();
    ASSERT_TRUE(h.has_value());
    const auto handle1 = *h;

    // Write data
    auto* p1 = pool.get(handle1);
    ASSERT_NE(p1, nullptr);
    p1->x = 123.45F;

    // Release first time
    EXPECT_TRUE(pool.release(handle1));
    EXPECT_EQ(pool.active_count(), 0U);

    // Double free MUST return false
    EXPECT_FALSE(pool.release(handle1));
    EXPECT_EQ(pool.active_count(), 0U);

    // Stale handle get must return nullptr
    EXPECT_EQ(pool.get(handle1), nullptr);
    EXPECT_FALSE(pool.is_valid(handle1));

    // Re-acquire slot
    auto h2 = pool.acquire();
    ASSERT_TRUE(h2.has_value());
    const auto handle2 = *h2;
    EXPECT_EQ(handle1.index, handle2.index);

    // Handle 1 is still stale and must NOT access newly acquired object
    EXPECT_FALSE(pool.is_valid(handle1));
    EXPECT_EQ(pool.get(handle1), nullptr);
    EXPECT_FALSE(pool.release(handle1));  // Double-freeing old handle must not free new object!

    // Handle 2 must remain valid
    EXPECT_TRUE(pool.is_valid(handle2));
    EXPECT_NE(pool.get(handle2), nullptr);
    EXPECT_EQ(pool.active_count(), 1U);

    // Out of bounds handle check
    cd::world2d::PoolHandle<DummyProjectile> oob_handle(9999U, 1U);
    EXPECT_FALSE(pool.is_valid(oob_handle));
    EXPECT_EQ(pool.get(oob_handle), nullptr);
    EXPECT_FALSE(pool.release(oob_handle));

    // Null handle check
    cd::world2d::PoolHandle<DummyProjectile> null_handle(0U, 0U);
    EXPECT_TRUE(null_handle.is_null());
    EXPECT_FALSE(pool.is_valid(null_handle));
    EXPECT_EQ(pool.get(null_handle), nullptr);
    EXPECT_FALSE(pool.release(null_handle));
}

TEST(World2DAdversarialTest, ObjectPool2D_GenerationalRolloverBehavior)
{
    // Single-slot pool
    cd::world2d::ObjectPool2D<DummyProjectile, 1> pool { 1U, false };

    // Cycle 65540 times to pass through 0xFFFF (65535) and verify rollover to 1
    constexpr std::uint32_t kCycles = 65540U;
    std::uint16_t last_gen = 0U;

    for (std::uint32_t i = 0U; i < kCycles; ++i)
    {
        auto h = pool.acquire();
        ASSERT_TRUE(h.has_value()) << "Failed at cycle " << i;
        EXPECT_NE(h->generation.value, 0U) << "Generation must NEVER be 0!";
        last_gen = h->generation.value;

        ASSERT_TRUE(pool.release(*h));
    }

    // After 65,540 cycles, last_gen must have wrapped around
    EXPECT_EQ(last_gen, static_cast<std::uint16_t>(kCycles % 65535U));

    // Fresh acquire after rollover works normally
    auto final_h = pool.acquire();
    ASSERT_TRUE(final_h.has_value());
    EXPECT_TRUE(pool.is_valid(*final_h));
    EXPECT_NE(pool.get(*final_h), nullptr);
    EXPECT_TRUE(pool.release(*final_h));
}

TEST(World2DAdversarialTest, ParticleSystem2D_PoolSaturationAndCapacityClamping)
{
    constexpr std::size_t kCap = 32U;
    cd::world2d::ParticleSystem2D sys { kCap };
    EXPECT_EQ(sys.capacity(), kCap);
    EXPECT_EQ(sys.active_count(), 0U);

    // Attempt to emit 500 particles into a 32-capacity pool
    sys.emit(500U);
    EXPECT_EQ(sys.active_count(), kCap);

    // Direct emit_particle when full must safely return false
    const bool emitted = sys.emit_particle({ 0.0F, 0.0F }, sys.config());
    EXPECT_FALSE(emitted);
    EXPECT_EQ(sys.active_count(), kCap);

    // Zero-capacity constructor must clamp to at least 1
    cd::world2d::ParticleSystem2D zero_sys { 0U };
    EXPECT_GE(zero_sys.capacity(), 1U);
}

TEST(World2DAdversarialTest, ParticleSystem2D_NegativeAndZeroDtKinematics)
{
    cd::world2d::ParticleSystem2D sys { 64U };
    sys.emit(10U);
    EXPECT_EQ(sys.active_count(), 10U);

    const auto p0_before = sys.active_particles()[0];

    // dt = 0.0f: state must remain completely unchanged
    sys.update(0.0F);
    EXPECT_EQ(sys.active_count(), 10U);
    EXPECT_FLOAT_EQ(sys.active_particles()[0].position.x, p0_before.position.x);
    EXPECT_FLOAT_EQ(sys.active_particles()[0].age, p0_before.age);

    // Negative dt (-1.0f): state must remain unchanged
    sys.update(-1.0F);
    EXPECT_EQ(sys.active_count(), 10U);
    EXPECT_FLOAT_EQ(sys.active_particles()[0].position.x, p0_before.position.x);
    EXPECT_FLOAT_EQ(sys.active_particles()[0].age, p0_before.age);

    // Extreme negative dt (-1e6): state unchanged
    sys.update(-1.0e6F);
    EXPECT_EQ(sys.active_count(), 10U);
    EXPECT_FLOAT_EQ(sys.active_particles()[0].position.x, p0_before.position.x);
}

TEST(World2DAdversarialTest, ParticleSystem2D_ExtremeGravityDragAndKinematicStability)
{
    cd::world2d::ParticleSystem2D sys { 128U };

    cd::world2d::ParticleConfig2D cfg {};
    cfg.gravity      = { 0.0F, -1.0e6F };   // 1 million m/s^2 downward
    cfg.drag         = 5000.0F;             // Extreme drag
    cfg.lifetime_min = 10.0F;
    cfg.lifetime_max = 10.0F;
    sys.set_config(cfg);

    sys.emit(50U);
    EXPECT_EQ(sys.active_count(), 50U);

    // Simulate 100 steps with dt = 0.05s (where drag * dt = 250 >> 1.0)
    for (int step = 0; step < 100; ++step)
    {
        sys.update(0.05F);
        for (const auto& p : sys.active_particles())
        {
            EXPECT_FALSE(std::isnan(p.position.x));
            EXPECT_FALSE(std::isnan(p.position.y));
            EXPECT_FALSE(std::isinf(p.position.x));
            EXPECT_FALSE(std::isinf(p.position.y));
            EXPECT_FALSE(std::isnan(p.velocity.x));
            EXPECT_FALSE(std::isnan(p.velocity.y));
            EXPECT_FALSE(std::isinf(p.velocity.x));
            EXPECT_FALSE(std::isinf(p.velocity.y));
        }
    }
}

TEST(World2DAdversarialTest, ParticleSystem2D_ZeroAllocationDuringSimulationHotPath)
{
    // Test that continuous emission, update, and particle recycling in a pre-allocated pool
    // never causes vector reallocations
    cd::world2d::ParticleSystem2D sys { 256U };
    const std::size_t initial_cap = sys.capacity();

    cd::world2d::ParticleConfig2D continuous_cfg {};
    continuous_cfg.emission_rate = 50.0F;
    continuous_cfg.lifetime_min  = 0.1F;
    continuous_cfg.lifetime_max  = 0.2F;

    // Simulate 500 frames: continuous emission + expiration
    for (int frame = 0; frame < 500; ++frame)
    {
        sys.update(0.016F, &continuous_cfg, { 0.0F, 0.0F });
        EXPECT_LE(sys.active_count(), sys.capacity());
        // Verify underlying storage capacity never changed
        EXPECT_EQ(sys.capacity(), initial_cap);
    }
}

TEST(World2DAdversarialTest, AudioCue2D_EdgeFrequenciesAndSynthesisRobustness)
{
    // 1. Zero frequency (DC)
    cd::world2d::RetroSynthParams2D p_zero {};
    p_zero.start_freq_hz = 0.0F;
    p_zero.end_freq_hz   = 0.0F;
    p_zero.duration_sec  = 0.05F;
    const auto samples_zero = cd::world2d::synthesize_waveform_samples(p_zero, 48000U);
    ASSERT_GT(samples_zero.size(), 0U);
    for (float s : samples_zero)
    {
        EXPECT_FALSE(std::isnan(s));
        EXPECT_FALSE(std::isinf(s));
        EXPECT_GE(s, -1.0F);
        EXPECT_LE(s, 1.0F);
    }

    // 2. Nyquist frequency (24,000 Hz at 48,000 Hz sample rate)
    cd::world2d::RetroSynthParams2D p_nyquist {};
    p_nyquist.start_freq_hz = 24000.0F;
    p_nyquist.end_freq_hz   = 24000.0F;
    p_nyquist.duration_sec  = 0.05F;
    const auto samples_nyquist = cd::world2d::synthesize_waveform_samples(p_nyquist, 48000U);
    ASSERT_GT(samples_nyquist.size(), 0U);
    for (float s : samples_nyquist)
    {
        EXPECT_FALSE(std::isnan(s));
        EXPECT_FALSE(std::isinf(s));
        EXPECT_GE(s, -1.0F);
        EXPECT_LE(s, 1.0F);
    }

    // 3. Negative frequency and zero duration edge case
    cd::world2d::RetroSynthParams2D p_neg {};
    p_neg.start_freq_hz = -440.0F;
    p_neg.end_freq_hz   = -220.0F;
    p_neg.duration_sec  = -0.10F;  // Clamped internally to minimum
    const auto samples_neg = cd::world2d::synthesize_waveform_samples(p_neg, 48000U);
    ASSERT_GT(samples_neg.size(), 0U);
    for (float s : samples_neg)
    {
        EXPECT_FALSE(std::isnan(s));
        EXPECT_FALSE(std::isinf(s));
        EXPECT_GE(s, -1.0F);
        EXPECT_LE(s, 1.0F);
    }
}

TEST(World2DAdversarialTest, AudioCue2D_ListenerEmitterSeparationExtremes)
{
    const cd::math::Vec2f listener { 10.0F, 20.0F };

    // 1. Coincident listener & emitter
    const auto pan_coincident = cd::world2d::compute_2d_stereo_panning(listener, listener);
    EXPECT_NEAR(pan_coincident.left, 1.0F / std::sqrt(2.0F), 1.0e-4F);
    EXPECT_NEAR(pan_coincident.right, 1.0F / std::sqrt(2.0F), 1.0e-4F);

    const float atten_coincident = cd::world2d::compute_2d_distance_attenuation(listener, listener);
    EXPECT_FLOAT_EQ(atten_coincident, 1.0F);

    // 2. Separation at positive infinity
    const cd::math::Vec2f emitter_inf { 1.0e15F, 1.0e15F };
    const auto pan_inf = cd::world2d::compute_2d_stereo_panning(listener, emitter_inf);
    EXPECT_FALSE(std::isnan(pan_inf.left));
    EXPECT_FALSE(std::isnan(pan_inf.right));
    EXPECT_GE(pan_inf.right, pan_inf.left);

    const float atten_inf = cd::world2d::compute_2d_distance_attenuation(listener, emitter_inf);
    EXPECT_FLOAT_EQ(atten_inf, 0.0F);

    // 3. Separation at negative infinity
    const cd::math::Vec2f emitter_neg_inf { -1.0e15F, -1.0e15F };
    const auto pan_neg_inf = cd::world2d::compute_2d_stereo_panning(listener, emitter_neg_inf);
    EXPECT_FALSE(std::isnan(pan_neg_inf.left));
    EXPECT_FALSE(std::isnan(pan_neg_inf.right));
    EXPECT_GE(pan_neg_inf.left, pan_neg_inf.right);

    // 4. Inverted spatial params (min_distance > max_distance)
    cd::world2d::AudioSpatialParams2D inv_params {};
    inv_params.min_distance = 20.0F;
    inv_params.max_distance = 5.0F;
    const float atten_inv = cd::world2d::compute_2d_distance_attenuation(
        listener, { 10.0F, 28.0F }, inv_params);
    EXPECT_FALSE(std::isnan(atten_inv));
    EXPECT_GE(atten_inv, 0.0F);
    EXPECT_LE(atten_inv, 1.0F);
}

TEST(World2DAdversarialTest, AudioCue2D_NullBackendRapidFiringOneThousandCues)
{
    cd::world2d::AudioCueSystem2D sys { nullptr };
    EXPECT_EQ(sys.backend(), nullptr);
    EXPECT_EQ(sys.total_cues_dispatched(), 0U);

    constexpr std::uint32_t kCues = 1000U;
    for (std::uint32_t i = 0U; i < kCues; ++i)
    {
        const auto preset = static_cast<cd::world2d::AudioCuePreset2D>(i % 7U);
        const cd::math::Vec2f pos { static_cast<float>(i), static_cast<float>(i * 2U) };
        const auto voice = sys.play_cue_at(preset, pos, 0.8F);
        EXPECT_TRUE(voice.is_null());
    }

    EXPECT_EQ(sys.total_cues_dispatched(), kCues);
    EXPECT_TRUE(sys.last_dispatched_cue().has_value());

    // Reset telemetry
    sys.reset_telemetry();
    EXPECT_EQ(sys.total_cues_dispatched(), 0U);
    EXPECT_FALSE(sys.last_dispatched_cue().has_value());
}

TEST(World2DAdversarialTest, SpatialHash2D_OversizedAabbSpanClampingAndQueryStamp)
{
    cd::world2d::SpatialHash2D hash { 2.0F };

    // 1. Extreme oversized entity spanning 100,000 units (50,000 cells!)
    // In un-clamped code, this would attempt 2.5 billion cell allocations and freeze.
    hash.insert(9999U, { { -50000.0F, -50000.0F }, { 50000.0F, 50000.0F } });
    EXPECT_TRUE(hash.contains(9999U));

    // 2. Normal entity at origin spanning 4 cells: [-0.5, -0.5] .. [0.5, 0.5]
    hash.insert(100U, { { -0.5F, -0.5F }, { 0.5F, 0.5F } });
    EXPECT_TRUE(hash.contains(100U));

    // 3. Query at origin: must find both the normal entity and the oversized entity
    auto q_origin = hash.query_point({ 0.0F, 0.0F });
    EXPECT_EQ(q_origin.size(), 2U);
    EXPECT_TRUE(std::find(q_origin.begin(), q_origin.end(), 9999U) != q_origin.end());
    EXPECT_TRUE(std::find(q_origin.begin(), q_origin.end(), 100U) != q_origin.end());

    // 4. Query box spanning 4 cells around origin: must return both, deduplicated (O(1) query stamp)
    auto q_box = hash.query_aabb({ { -1.0F, -1.0F }, { 1.0F, 1.0F } });
    EXPECT_EQ(q_box.size(), 2U);
    EXPECT_TRUE(std::find(q_box.begin(), q_box.end(), 9999U) != q_box.end());
    EXPECT_TRUE(std::find(q_box.begin(), q_box.end(), 100U) != q_box.end());

    // 5. Degenerate query box spanning 200,000 units: must clamp cell span to kMaxCellSpan
    // and complete in under 5 milliseconds rather than freezing.
    auto q_huge = hash.query_aabb({ { -100000.0F, -100000.0F }, { 100000.0F, 100000.0F } });
    EXPECT_GE(q_huge.size(), 1U);
    EXPECT_TRUE(std::find(q_huge.begin(), q_huge.end(), 9999U) != q_huge.end());

    // 6. Safe removal of oversized entity
    EXPECT_TRUE(hash.remove(9999U));
    EXPECT_FALSE(hash.contains(9999U));
    EXPECT_TRUE(hash.contains(100U));

    // After removing 9999, query at origin only returns entity 100
    q_origin = hash.query_point({ 0.0F, 0.0F });
    ASSERT_EQ(q_origin.size(), 1U);
    EXPECT_EQ(q_origin.front(), 100U);

    EXPECT_TRUE(hash.remove(100U));
    EXPECT_FALSE(hash.contains(100U));
    EXPECT_TRUE(hash.empty());
}

TEST(World2DChallengerTest, SpatialHash2D_OversizedQueryAabbBehaviorEmpiricalCheck)
{
    cd::world2d::SpatialHash2D hash { 2.0F };

    // Insert 3 normal entities:
    // Entity 1 at (-490, -490) - near min corner of query
    // Entity 2 at (0, 0) - center of world and query
    // Entity 3 at (490, 490) - near max corner of query
    hash.insert(1U, { { -491.0F, -491.0F }, { -489.0F, -489.0F } });
    hash.insert(2U, { { -1.0F, -1.0F }, { 1.0F, 1.0F } });
    hash.insert(3U, { { 489.0F, 489.0F }, { 491.0F, 491.0F } });

    EXPECT_EQ(hash.size(), 3U);

    // Query AABB spanning [-500, -500] to [500, 500] (span = 501 cells, > kMaxCellSpan = 128)
    const auto q_res = hash.query_aabb({ { -500.0F, -500.0F }, { 500.0F, 500.0F } });

    // Print empirical findings for analysis
    std::cout << "[CHALLENGER] query_aabb([-500,500]) returned count: " << q_res.size() << std::endl;
    for (std::uint32_t id : q_res)
    {
        std::cout << "[CHALLENGER]   returned id: " << id << std::endl;
    }

    // Adversarial expectation: All 3 entities are inside [-500, 500], so all 3 MUST be returned.
    // However, due to cell loop clamping to min_cx + kMaxCellSpan - 1, only entity 1 is returned!
    EXPECT_EQ(q_res.size(), 3U) << "Silent omission: entities 2 (origin) and 3 (max corner) were dropped!";
}

TEST(World2DChallengerTest, SpatialHash2D_OversizedCircleQueryEmpiricalCheck)
{
    cd::world2d::SpatialHash2D hash { 2.0F };

    // Entity 2 at center (0, 0)
    hash.insert(2U, { { -1.0F, -1.0F }, { 1.0F, 1.0F } });

    // Large circle query centered at (0, 0) with radius 500.0F
    // The query box computed inside query_circle is [-500, -500] to [500, 500]
    const auto q_circle = hash.query_circle({ 0.0F, 0.0F }, 500.0F);

    std::cout << "[CHALLENGER] query_circle(radius 500) returned count: " << q_circle.size() << std::endl;
    for (std::uint32_t id : q_circle)
    {
        std::cout << "[CHALLENGER]   circle returned id: " << id << std::endl;
    }

    // Adversarial expectation: Entity 2 is at distance 0 from circle center (0, 0), so it MUST be returned.
    // However, query_circle returns 0 entities because query_aabb truncated the cell loop!
    EXPECT_EQ(q_circle.size(), 1U) << "Silent omission: entity 2 at circle center was dropped!";
}

TEST(World2DChallengerTest, SpatialHash2D_TrueO1QueryStampHighThroughputStress)
{
    cd::world2d::SpatialHash2D hash { 1.0F };

    // Insert 2000 entities across a 10x10 area
    for (std::uint32_t i = 1U; i <= 2000U; ++i)
    {
        const float x = static_cast<float>(i % 10U) * 0.9F;
        const float y = static_cast<float>((i / 10U) % 10U) * 0.9F;
        hash.insert(i, { { x, y }, { x + 1.5F, y + 1.5F } });
    }

    // Perform 500 high-density queries
    for (int q = 0; q < 500; ++q)
    {
        std::vector<std::uint32_t> results;
        hash.query_aabb({ { 2.0F, 2.0F }, { 6.0F, 6.0F } }, results);
        EXPECT_FALSE(results.empty());

        // Deduplication invariant: all returned IDs must be strictly unique
        std::unordered_set<std::uint32_t> seen;
        for (std::uint32_t id : results)
        {
            EXPECT_TRUE(seen.insert(id).second) << "Duplicate ID in query result: " << id;
        }
    }
}

TEST(World2DChallengerTest, SpatialHash2D_InfinityAndNanCoordinateRobustness)
{
    cd::world2d::SpatialHash2D hash { 2.0F };

    constexpr float kInf = std::numeric_limits<float>::infinity();
    constexpr float kNan = std::numeric_limits<float>::quiet_NaN();

    // Inverted bounds with inf/nan
    hash.insert(101U, { { kInf, kInf }, { -kInf, -kInf } });
    EXPECT_TRUE(hash.contains(101U));

    // Finite query should not return degenerate entity
    auto q_finite = hash.query_aabb({ { -10.0F, -10.0F }, { 10.0F, 10.0F } });
    EXPECT_TRUE(q_finite.empty());

    // Query with NaN should safely return empty without crashing or hanging
    auto q_nan = hash.query_aabb({ { kNan, kNan }, { 10.0F, 10.0F } });
    EXPECT_TRUE(q_nan.empty());

    // Point query with NaN
    auto q_nan_pt = hash.query_point({ kNan, kNan });
    // Point query with NaN coordinates
    EXPECT_TRUE(hash.remove(101U));
}

TEST(World2DChallengerTest, SpatialHash2D_BoundarySpan128Vs129Parity)
{
    cd::world2d::SpatialHash2D hash { 2.0F }; // cell_size = 2.0

    // Place entities at cell 0, cell 63, and cell 127
    hash.insert(10U, { { 0.5F, 0.5F }, { 1.5F, 1.5F } });             // cell (0, 0)
    hash.insert(20U, { { 126.5F, 126.5F }, { 127.5F, 127.5F } });     // cell (63, 63)
    hash.insert(30U, { { 254.5F, 254.5F }, { 255.5F, 255.5F } });     // cell (127, 127)

    EXPECT_EQ(hash.size(), 3U);

    // Query 1: Exactly 128 cells span -> [0.0, 255.9] -> min_cx=0, max_cx=127 -> span_x = 128 (Grid Path)
    const auto q_128 = hash.query_aabb({ { 0.0F, 0.0F }, { 255.9F, 255.9F } });
    EXPECT_EQ(q_128.size(), 3U);
    EXPECT_TRUE(std::find(q_128.begin(), q_128.end(), 10U) != q_128.end());
    EXPECT_TRUE(std::find(q_128.begin(), q_128.end(), 20U) != q_128.end());
    EXPECT_TRUE(std::find(q_128.begin(), q_128.end(), 30U) != q_128.end());

    // Query 2: Exactly 129 cells span -> [0.0, 256.5] -> min_cx=0, max_cx=128 -> span_x = 129 (Fallback Linear Scan)
    const auto q_129 = hash.query_aabb({ { 0.0F, 0.0F }, { 256.5F, 256.5F } });
    EXPECT_EQ(q_129.size(), 3U);
    EXPECT_TRUE(std::find(q_129.begin(), q_129.end(), 10U) != q_129.end());
    EXPECT_TRUE(std::find(q_129.begin(), q_129.end(), 20U) != q_129.end());
    EXPECT_TRUE(std::find(q_129.begin(), q_129.end(), 30U) != q_129.end());

    // Parity check: Results from both paths must match exactly
    EXPECT_EQ(q_128.size(), q_129.size());
}

TEST(World2DChallengerTest, SpatialHash2D_DegenerateAndZeroRadiusCircleAndAabbQueries)
{
    cd::world2d::SpatialHash2D hash { 2.0F };

    // Insert entity at [10.0, 10.0] .. [14.0, 14.0]
    hash.insert(1U, { { 10.0F, 10.0F }, { 14.0F, 14.0F } });
    // Insert point entity at [20.0, 20.0] .. [20.0, 20.0]
    hash.insert(2U, { { 20.0F, 20.0F }, { 20.0F, 20.0F } });

    // 1. Point query via AABB with min == max
    auto q_pt = hash.query_aabb({ { 12.0F, 12.0F }, { 12.0F, 12.0F } });
    ASSERT_EQ(q_pt.size(), 1U);
    EXPECT_EQ(q_pt.front(), 1U);

    // Query point outside
    auto q_pt_out = hash.query_aabb({ { 15.0F, 15.0F }, { 15.0F, 15.0F } });
    EXPECT_TRUE(q_pt_out.empty());

    // Query exactly touching point entity 2
    auto q_pt_entity = hash.query_aabb({ { 20.0F, 20.0F }, { 20.0F, 20.0F } });
    ASSERT_EQ(q_pt_entity.size(), 1U);
    EXPECT_EQ(q_pt_entity.front(), 2U);

    // 2. Circle query with radius 0.0 at center (12, 12) inside entity 1
    auto q_c0 = hash.query_circle({ 12.0F, 12.0F }, 0.0F);
    ASSERT_EQ(q_c0.size(), 1U);
    EXPECT_EQ(q_c0.front(), 1U);

    // Circle query with radius 0.0 outside
    auto q_c0_out = hash.query_circle({ 15.0F, 15.0F }, 0.0F);
    EXPECT_TRUE(q_c0_out.empty());

    // 3. Circle query with negative radius -5.0F (must clamp to 0.0F safely)
    auto q_c_neg = hash.query_circle({ 12.0F, 12.0F }, -5.0F);
    ASSERT_EQ(q_c_neg.size(), 1U);
    EXPECT_EQ(q_c_neg.front(), 1U);

    // 4. Circle query touching edge of entity 1: center (16, 12), radius 2.0 (distance to x=14 is 2.0 <= 2.0)
    auto q_c_touch = hash.query_circle({ 16.0F, 12.0F }, 2.0F);
    ASSERT_EQ(q_c_touch.size(), 1U);
    EXPECT_EQ(q_c_touch.front(), 1U);

    // Circle query just missing edge of entity 1: center (16.1, 12), radius 2.0 (distance 2.1 > 2.0)
    auto q_c_miss = hash.query_circle({ 16.1F, 12.0F }, 2.0F);
    EXPECT_TRUE(q_c_miss.empty());

    // 5. Circle query with infinite radius
    constexpr float kInf = std::numeric_limits<float>::infinity();
    auto q_c_inf = hash.query_circle({ 0.0F, 0.0F }, kInf);
    EXPECT_EQ(q_c_inf.size(), 2U);

    // 6. Rapid churn: 500 entity updates & queries
    for (std::uint32_t i = 0U; i < 500U; ++i)
    {
        const float offset = static_cast<float>(i) * 0.1F;
        hash.update(1U, { { 10.0F + offset, 10.0F }, { 14.0F + offset, 14.0F } });
        const auto q = hash.query_circle({ 12.0F + offset, 12.0F }, 1.0F);
        EXPECT_EQ(q.size(), 1U);
    }
}

TEST(World2DTest, GenreTemplates_ActionPlatformer_InitializesExpectedEntitiesAndPhysics)
{
    cd::world2d::Stage2D stage {};
    cd::world2d::GenreTemplates2D::apply_genre_template(stage, cd::world2d::GenreTemplateKind2D::kActionPlatformer);

    EXPECT_EQ(stage.name(), "Neon_Cavern_Platformer_2D");
    EXPECT_FLOAT_EQ(stage.gravity_y(), -28.0F);
    EXPECT_FALSE(stage.is_y_sort_enabled());
    EXPECT_GE(stage.parallax_layers().size(), 3U);

    bool has_hero   = false;
    bool has_portal = false;
    int  gem_count  = 0;
    for (const auto& ent : stage.entities())
    {
        if (ent.role == cd::world2d::EntityRole2D::kPlayerHero)
        {
            has_hero = true;
        }
        else if (ent.role == cd::world2d::EntityRole2D::kGoalPortal)
        {
            has_portal = true;
        }
        else if (ent.role == cd::world2d::EntityRole2D::kCollectibleGem)
        {
            ++gem_count;
        }
    }
    EXPECT_TRUE(has_hero);
    EXPECT_TRUE(has_portal);
    EXPECT_GE(gem_count, 3);

    // Verify play mode stepping
    stage.enter_play();
    EXPECT_TRUE(stage.is_playing());
    cd::world2d::PlayerInput2D in {};
    in.move_x       = 1.0F;
    in.jump_pressed = true;
    stage.step_play(1.0F / 60.0F, in);
    EXPECT_GT(stage.hud().sim_time_sec, 0.0F);
    stage.exit_play();
    EXPECT_FALSE(stage.is_playing());
}

TEST(World2DTest, GenreTemplates_TopDownRpg_InitializesZeroGravityAndDynamicYSort)
{
    cd::world2d::Stage2D stage {};
    cd::world2d::GenreTemplates2D::apply_genre_template(stage, cd::world2d::GenreTemplateKind2D::kTopDownRpgDungeon);

    EXPECT_EQ(stage.name(), "TopDown_Rpg_Dungeon_2D");
    EXPECT_FLOAT_EQ(stage.gravity_y(), 0.0F);
    EXPECT_TRUE(stage.is_y_sort_enabled());
    EXPECT_FALSE(stage.baked_tile_colliders().empty());

    bool has_hero    = false;
    int  light_count = 0;
    int  chest_count = 0;
    for (const auto& ent : stage.entities())
    {
        if (ent.role == cd::world2d::EntityRole2D::kPlayerHero)
        {
            has_hero = true;
            EXPECT_TRUE(ent.y_sort);
        }
        else if (ent.role == cd::world2d::EntityRole2D::kPointLight)
        {
            ++light_count;
        }
        else if (ent.role == cd::world2d::EntityRole2D::kCollectibleGem)
        {
            ++chest_count;
        }
    }
    EXPECT_TRUE(has_hero);
    EXPECT_GE(light_count, 4);
    EXPECT_GE(chest_count, 2);

    // Verify 8-way movement stepping in zero gravity
    stage.enter_play();
    cd::world2d::PlayerInput2D in {};
    in.move_x = 0.707F;
    in.move_y = 0.707F;
    const auto* hero_before = stage.find_entity(1U);
    ASSERT_NE(hero_before, nullptr);
    const auto start_pos = hero_before->position;
    stage.step_play(0.1F, in);

    const auto* hero_after = stage.find_entity(1U);
    ASSERT_NE(hero_after, nullptr);
    EXPECT_GT(hero_after->position.x, start_pos.x);
    EXPECT_GT(hero_after->position.y, start_pos.y);
    stage.exit_play();
}

TEST(World2DTest, GenreTemplates_TwinStickArena_SwarmSpawnerAndProjectilePool)
{
    cd::world2d::Stage2D stage {};
    cd::world2d::GenreTemplates2D::apply_genre_template(stage, cd::world2d::GenreTemplateKind2D::kTwinStickArena);

    EXPECT_EQ(stage.name(), "TwinStick_Arena_2D");
    EXPECT_FLOAT_EQ(stage.gravity_y(), 0.0F);
    EXPECT_TRUE(stage.is_y_sort_enabled());
    EXPECT_TRUE(stage.swarm_spawner().active);
    EXPECT_GT(stage.swarm_spawner().max_swarm_count, 10);

    stage.enter_play();
    EXPECT_TRUE(stage.projectile_pool().empty());

    // Fire weapon towards angle 0.0 (right)
    cd::world2d::PlayerInput2D in {};
    in.fire_pressed  = true;
    in.aim_angle_rad = 0.0F;
    stage.step_play(1.0F / 60.0F, in);

    EXPECT_FALSE(stage.projectile_pool().empty());
    EXPECT_EQ(stage.projectile_pool().active_count(), 1U);

    // Step simulation across 10 frames to move projectile
    for (int i = 0; i < 10; ++i)
    {
        cd::world2d::PlayerInput2D idle {};
        stage.step_play(1.0F / 60.0F, idle);
    }
    stage.exit_play();
}

TEST(World2DTest, DynamicYSorting_OrdersDrawCommandsStrictlyByFootY)
{
    cd::world2d::Stage2D stage {};
    stage.clear();
    stage.set_y_sort_enabled(true);

    cd::world2d::Entity2D north {};
    north.id        = 10U;
    north.name      = "North_Tree";
    north.position  = { 10.0F, 12.0F };
    north.size      = { 2.0F, 4.0F }; // foot Y = 12 - 2 = 10.0F
    north.layer     = 100;
    north.y_sort    = true;
    (void)stage.add_entity(std::move(north));

    cd::world2d::Entity2D south {};
    south.id        = 20U;
    south.name      = "South_Hero";
    south.position  = { 10.0F, 6.0F };
    south.size      = { 1.0F, 2.0F }; // foot Y = 6 - 1 = 5.0F
    south.layer     = 100;
    south.y_sort    = true;
    (void)stage.add_entity(std::move(south));

    stage.sort_entities_by_foot_y();
    // In descending order of foot Y (highest foot Y first, so northern drawn first)
    const auto ents = stage.entities();
    ASSERT_EQ(ents.size(), 2U);
    EXPECT_EQ(ents[0].id, 10U); // North_Tree foot Y = 10.0F
    EXPECT_EQ(ents[1].id, 20U); // South_Hero foot Y = 5.0F

    // Canvas sort keys: lower world_foot_y gives higher clamped -foot_y, thus larger key (drawn in front)
    const std::uint64_t key_north = cd::render2d::make_sort_key_2d(100U, 0, 10.0F, true, 0U);
    const std::uint64_t key_south = cd::render2d::make_sort_key_2d(100U, 0, 5.0F, true, 0U);
    EXPECT_LT(key_north, key_south);
}

TEST(World2DTest, CommandBus2D_GenreSwitching_UndoRedoLossless)
{
    cd::world2d::Stage2D      stage {};
    cd::world2d::CommandBus2D bus {};

    // Initial Platformer
    bus.load_genre_template(stage, cd::world2d::GenreTemplateKind2D::kActionPlatformer);
    EXPECT_EQ(stage.name(), "Neon_Cavern_Platformer_2D");
    EXPECT_FLOAT_EQ(stage.gravity_y(), -28.0F);

    // Switch to RPG
    bus.load_genre_template(stage, cd::world2d::GenreTemplateKind2D::kTopDownRpgDungeon);
    EXPECT_EQ(stage.name(), "TopDown_Rpg_Dungeon_2D");
    EXPECT_FLOAT_EQ(stage.gravity_y(), 0.0F);
    EXPECT_TRUE(stage.is_y_sort_enabled());

    // Undo -> back to Platformer
    EXPECT_TRUE(bus.undo(stage));
    EXPECT_EQ(stage.name(), "Neon_Cavern_Platformer_2D");
    EXPECT_FLOAT_EQ(stage.gravity_y(), -28.0F);

    // Redo -> back to RPG
    EXPECT_TRUE(bus.redo(stage));
    EXPECT_EQ(stage.name(), "TopDown_Rpg_Dungeon_2D");
    EXPECT_FLOAT_EQ(stage.gravity_y(), 0.0F);
}

TEST(World2DTest, EmptyStageTemplateIsCleanAndAuthorable)
{
    cd::world2d::Stage2D stage {};
    cd::world2d::GenreTemplates2D::build_empty_stage(stage);

    EXPECT_EQ(stage.name(), "Empty_2D_Project");
    EXPECT_TRUE(stage.entities().empty());
    EXPECT_TRUE(stage.tilemap().export_tiles().empty());
    EXPECT_EQ(stage.arcade_ruleset(), cd::world2d::ArcadeRuleset2D::kStandard);
}

TEST(World2DTest, PongArcadeRalliesScoresAndRoundTripsPak)
{
    cd::world2d::Stage2D stage {};
    cd::world2d::GenreTemplates2D::build_pong_arcade(stage);
    EXPECT_EQ(stage.arcade_ruleset(), cd::world2d::ArcadeRuleset2D::kPongCourt);

    const auto pak_bytes = stage.build_standalone_pak_bytes();
    cd::world2d::Stage2D loaded {};
    ASSERT_TRUE(loaded.load_from_pak_bytes(pak_bytes).has_value());
    EXPECT_EQ(loaded.arcade_ruleset(), cd::world2d::ArcadeRuleset2D::kPongCourt);
    EXPECT_EQ(loaded.name(), "Neon_Cyber_Pong_2D");

    const auto rep = cd::world2d::run_playtest_2d(loaded, 240U, true, false);
    EXPECT_EQ(rep.frames_simulated, 240U);
    EXPECT_GT(loaded.hud().rally_count + loaded.hud().gems_collected + loaded.hud().opponent_score, 0);
}

TEST(World2DTest, BreakTheBridgesShattersSpansAndCollapsesRaiders)
{
    cd::world2d::Stage2D stage {};
    cd::world2d::GenreTemplates2D::build_break_the_bridges(stage);
    EXPECT_EQ(stage.arcade_ruleset(), cd::world2d::ArcadeRuleset2D::kBridgeDemolition);

    const auto pak_bytes = stage.build_standalone_pak_bytes();
    cd::world2d::Stage2D loaded {};
    ASSERT_TRUE(loaded.load_from_pak_bytes(pak_bytes).has_value());
    EXPECT_EQ(loaded.arcade_ruleset(), cd::world2d::ArcadeRuleset2D::kBridgeDemolition);

    const auto rep = cd::world2d::run_playtest_2d(loaded, 360U, true, false);
    EXPECT_GT(rep.gems_collected, 0);
    EXPECT_GT(rep.score, 200);
    EXPECT_EQ(rep.total_gems, 22);
}

}  // namespace
