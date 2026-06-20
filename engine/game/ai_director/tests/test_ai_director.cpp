// =============================================================================
// CHROMODYNAMIC — tests/test_ai_director.cpp
// Phase 620 — cd::game::ai_director unit tests.
//
// Covers the 5+ contract requirements from the M8 W3B brief:
//   1. configure_templates + state defaults.
//   2. notify_player_event increments intensity.
//   3. tick advances time_in_state_ms.
//   4. Tension state transitions: idle→build→peak→relief→idle.
//   5. request_next_encounter respects difficulty tier.
//   6. (extra) Intensity clamped to [0, 1].
//   7. (extra) Relief exit: intensity falls below kThresholdBuildUp → kIdle.
//   8. (extra) request_next_encounter returns nullopt when no templates configured.
// =============================================================================
#include <cd/game/ai_director/AiDirector.hpp>

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <vector>

namespace
{

using cd::game::ai_director::AiDirector;
using cd::game::ai_director::DirectorState;
using cd::game::ai_director::EncounterTemplate;
using cd::game::ai_director::TensionState;

// Helper: build a simple two-tier template library.
//   tier 0 -> "easy_patrol"   intensity_contribution=0.05
//   tier 1 -> "hard_ambush"   intensity_contribution=0.20
//   tier 2 -> "boss_rush"     intensity_contribution=0.40
inline std::vector<EncounterTemplate> make_templates()
{
    return {
        EncounterTemplate{"easy_patrol",  0, 0.05F, {"goblin"}},
        EncounterTemplate{"hard_ambush",  1, 0.20F, {"orc", "archer"}},
        EncounterTemplate{"boss_rush",    2, 0.40F, {"dragon"}},
    };
}

// =============================================================================
// 1) configure_templates + state defaults
// =============================================================================
TEST(AiDirector, ConfigureAndDefaultState)
{
    AiDirector director;

    // Before configuration, state must be fully zeroed / idle.
    const DirectorState s0 = director.state();
    EXPECT_EQ(s0.tension, TensionState::kIdle);
    EXPECT_FLOAT_EQ(s0.intensity, 0.0F);
    EXPECT_FLOAT_EQ(s0.time_in_state_ms, 0.0F);
    EXPECT_EQ(s0.player_score, 0U);

    // After configuration the observable state must be unchanged.
    const auto tmplts = make_templates();
    director.configure_templates(tmplts);

    const DirectorState s1 = director.state();
    EXPECT_EQ(s1.tension, TensionState::kIdle);
    EXPECT_FLOAT_EQ(s1.intensity, 0.0F);
    EXPECT_FLOAT_EQ(s1.time_in_state_ms, 0.0F);
    EXPECT_EQ(s1.player_score, 0U);
}

// =============================================================================
// 2) notify_player_event increments intensity
// =============================================================================
TEST(AiDirector, NotifyIncreasesIntensity)
{
    AiDirector director;
    EXPECT_FLOAT_EQ(director.state().intensity, 0.0F);

    director.notify_player_event("enemy_killed", 0.10F);
    EXPECT_NEAR(director.state().intensity, 0.10F, 1e-5F);

    director.notify_player_event("damage_taken", 0.15F);
    EXPECT_NEAR(director.state().intensity, 0.25F, 1e-5F);
}

TEST(AiDirector, NotifyNegativeDeltaDecreasesIntensity)
{
    AiDirector director;
    director.notify_player_event("objective_complete", 0.50F);
    EXPECT_NEAR(director.state().intensity, 0.50F, 1e-5F);

    director.notify_player_event("rest_bonus", -0.20F);
    EXPECT_NEAR(director.state().intensity, 0.30F, 1e-5F);
}

TEST(AiDirector, IntensityClampedToUnitInterval)
{
    AiDirector director;

    // Drive well above 1.
    director.notify_player_event("spike", 2.0F);
    EXPECT_FLOAT_EQ(director.state().intensity, 1.0F);

    // Drive well below 0.
    director.notify_player_event("recovery", -5.0F);
    EXPECT_FLOAT_EQ(director.state().intensity, 0.0F);
}

// =============================================================================
// 3) tick advances time_in_state_ms
// =============================================================================
TEST(AiDirector, TickAdvancesTimeInState)
{
    AiDirector director;

    director.tick(100.0F);
    EXPECT_NEAR(director.state().time_in_state_ms, 100.0F, 1.0F);

    director.tick(250.0F);
    EXPECT_NEAR(director.state().time_in_state_ms, 350.0F, 1.0F);
}

TEST(AiDirector, TickDecaysIntensity)
{
    AiDirector director;
    director.notify_player_event("event", 0.50F);
    EXPECT_NEAR(director.state().intensity, 0.50F, 1e-5F);

    // After 1000 ms the decay should have reduced intensity noticeably.
    director.tick(1000.0F);
    EXPECT_LT(director.state().intensity, 0.50F);
}

// =============================================================================
// 4) Tension state transitions: idle → buildUp → peak → relief → idle
// =============================================================================
TEST(AiDirector, TensionTransitions_IdleToBuildUp)
{
    AiDirector director;
    EXPECT_EQ(director.state().tension, TensionState::kIdle);

    // Cross the kIdle/kBuildUp boundary (0.25).
    director.notify_player_event("event", AiDirector::kThresholdBuildUp + 0.01F);
    EXPECT_EQ(director.state().tension, TensionState::kBuildUp);
    // time_in_state should have reset.
    EXPECT_NEAR(director.state().time_in_state_ms, 0.0F, 1e-4F);
}

TEST(AiDirector, TensionTransitions_BuildUpToPeak)
{
    AiDirector director;
    director.notify_player_event("event", AiDirector::kThresholdPeak + 0.01F);
    EXPECT_EQ(director.state().tension, TensionState::kPeak);
}

TEST(AiDirector, TensionTransitions_PeakToRelief)
{
    AiDirector director;
    director.notify_player_event("event", AiDirector::kThresholdRelief + 0.01F);
    EXPECT_EQ(director.state().tension, TensionState::kRelief);
}

TEST(AiDirector, TensionTransitions_ReliefToIdle)
{
    AiDirector director;
    // Enter relief.
    director.notify_player_event("spike", AiDirector::kThresholdRelief + 0.05F);
    ASSERT_EQ(director.state().tension, TensionState::kRelief);

    // In relief the combined decay rate (kDecayRatePerMs + kReliefDecayBonus)
    // = 0.0004 per ms.  Starting intensity ≈ 0.90.
    // To reach 0.0 we need at least 0.90 / 0.0004 = 2250 ms.
    // Tick 3000 ms to be safe.
    director.tick(3000.0F);

    EXPECT_EQ(director.state().tension, TensionState::kIdle);
    EXPECT_FLOAT_EQ(director.state().intensity, 0.0F);
}

TEST(AiDirector, TimeInStateResetsOnTransition)
{
    AiDirector director;
    // Accumulate some time in kIdle.
    director.tick(500.0F);
    EXPECT_NEAR(director.state().time_in_state_ms, 500.0F, 1.0F);

    // Trigger a transition to kBuildUp.
    director.notify_player_event("event", AiDirector::kThresholdBuildUp + 0.05F);
    ASSERT_EQ(director.state().tension, TensionState::kBuildUp);
    // time_in_state_ms should have reset to 0.
    EXPECT_NEAR(director.state().time_in_state_ms, 0.0F, 1e-4F);

    // Accumulate time in kBuildUp.
    director.tick(200.0F);
    EXPECT_NEAR(director.state().time_in_state_ms, 200.0F, 2.0F);
}

// =============================================================================
// 5) request_next_encounter respects difficulty tier
// =============================================================================
TEST(AiDirector, RequestEncounterNulloptWhenNoTemplates)
{
    AiDirector director;
    director.notify_player_event("event", 0.8F);  // high intensity
    EXPECT_FALSE(director.request_next_encounter().has_value());
}

TEST(AiDirector, RequestEncounterNulloptAtZeroIntensity)
{
    AiDirector director;
    const auto tmplts = make_templates();
    director.configure_templates(tmplts);

    // intensity == 0 → no encounter.
    EXPECT_FLOAT_EQ(director.state().intensity, 0.0F);
    EXPECT_FALSE(director.request_next_encounter().has_value());
}

TEST(AiDirector, RequestEncounterLowIntensityPicksTier0)
{
    AiDirector director;
    const auto tmplts = make_templates();
    director.configure_templates(tmplts);

    // Intensity 0.10 → max_tier = floor(0.10 * 2) = 0 → only tier-0 eligible.
    director.notify_player_event("event", 0.10F);
    const auto enc = director.request_next_encounter();
    ASSERT_TRUE(enc.has_value());
    EXPECT_EQ(enc->difficulty_tier, 0U);
    EXPECT_EQ(enc->template_id, "easy_patrol");
}

TEST(AiDirector, RequestEncounterHighIntensityPicksHighestTier)
{
    AiDirector director;
    const auto tmplts = make_templates();
    director.configure_templates(tmplts);

    // Intensity 1.0 → max_tier = floor(1.0 * 2) = 2 → tier-2 eligible.
    director.notify_player_event("event", 1.0F);
    const auto enc = director.request_next_encounter();
    ASSERT_TRUE(enc.has_value());
    EXPECT_EQ(enc->difficulty_tier, 2U);
    EXPECT_EQ(enc->template_id, "boss_rush");
}

TEST(AiDirector, RequestEncounterMidIntensityPicksTier1)
{
    AiDirector director;
    const auto tmplts = make_templates();
    director.configure_templates(tmplts);

    // Intensity 0.60 → max_tier = floor(0.60 * 2) = 1 → tier-1 eligible.
    director.notify_player_event("event", 0.60F);
    const auto enc = director.request_next_encounter();
    ASSERT_TRUE(enc.has_value());
    EXPECT_EQ(enc->difficulty_tier, 1U);
    EXPECT_EQ(enc->template_id, "hard_ambush");
}

// =============================================================================
// 6) player_score accumulates from positive deltas
// =============================================================================
TEST(AiDirector, PlayerScoreAccumulates)
{
    AiDirector director;
    // Each unit of positive delta → kScorePerUnit (10) points.
    // delta=2.0 → 2 units → 20 points.
    director.notify_player_event("event", 2.0F);
    // intensity clamped to 1.0, score based on floor(2.0) * 10 = 20.
    EXPECT_EQ(director.state().player_score, 20U);

    director.notify_player_event("event2", 3.0F);
    // Additional floor(3.0)*10 = 30, total = 50.
    EXPECT_EQ(director.state().player_score, 50U);

    // Negative delta does not affect score.
    director.notify_player_event("heal", -1.0F);
    EXPECT_EQ(director.state().player_score, 50U);
}

// =============================================================================
// 7) tick edge cases: zero dt, negative dt, extreme dt
// =============================================================================
TEST(AiDirector, TickZeroDtIsNoOp)
{
    // Arrange
    AiDirector director;
    director.notify_player_event("event", 0.50F);
    const DirectorState before = director.state();

    // Act
    director.tick(0.0F);

    // Assert — nothing changes
    const DirectorState after = director.state();
    EXPECT_FLOAT_EQ(after.intensity, before.intensity);
    EXPECT_FLOAT_EQ(after.time_in_state_ms, before.time_in_state_ms);
    EXPECT_EQ(after.tension, before.tension);
}

TEST(AiDirector, TickNegativeDtIsNoOp)
{
    // Arrange
    AiDirector director;
    director.notify_player_event("event", 0.50F);
    const DirectorState before = director.state();

    // Act
    director.tick(-100.0F);

    // Assert — negative dt must not mutate state
    const DirectorState after = director.state();
    EXPECT_FLOAT_EQ(after.intensity, before.intensity);
    EXPECT_FLOAT_EQ(after.time_in_state_ms, before.time_in_state_ms);
    EXPECT_EQ(after.tension, before.tension);
}

TEST(AiDirector, TickExtremeLargeDtClampsIntensityToZero)
{
    // Arrange
    AiDirector director;
    director.notify_player_event("event", 1.0F);
    ASSERT_FLOAT_EQ(director.state().intensity, 1.0F);

    // Act — a 10-million-ms tick should fully drain intensity; never go negative
    director.tick(10'000'000.0F);

    // Assert
    EXPECT_FLOAT_EQ(director.state().intensity, 0.0F);
    EXPECT_EQ(director.state().tension, TensionState::kIdle);
}

// =============================================================================
// 8) intensity saturation at exact 0.0 and 1.0 bounds
// =============================================================================
TEST(AiDirector, IntensitySaturatesAtOne_ExactBoundary)
{
    AiDirector director;
    director.notify_player_event("event", 1.0F);
    EXPECT_FLOAT_EQ(director.state().intensity, 1.0F);

    // Another positive delta on top must not exceed 1.0
    director.notify_player_event("event2", 0.5F);
    EXPECT_FLOAT_EQ(director.state().intensity, 1.0F);
}

TEST(AiDirector, IntensitySaturatesAtZero_ExactBoundary)
{
    AiDirector director;
    // Start at zero, apply large negative delta
    director.notify_player_event("event", -100.0F);
    EXPECT_FLOAT_EQ(director.state().intensity, 0.0F);
}

// =============================================================================
// 9) score: saturating multiply + sub-unit delta
// =============================================================================
TEST(AiDirector, PlayerScoreSubUnitDeltaGivesZeroScore)
{
    // floor(0.5) = 0, so score contribution must be 0
    AiDirector director;
    director.notify_player_event("event", 0.5F);
    EXPECT_EQ(director.state().player_score, 0U);
    EXPECT_NEAR(director.state().intensity, 0.5F, 1e-5F);
}

TEST(AiDirector, PlayerScoreOverflowSaturates)
{
    // Extreme float delta: triggers saturating multiply guard
    AiDirector director;
    // delta_units from 1e18F vastly exceeds UINT32_MAX / kScorePerUnit (429496729)
    // so score_add is capped at UINT32_MAX, then headroom = UINT32_MAX - 0 = UINT32_MAX,
    // so player_score lands at UINT32_MAX.
    director.notify_player_event("event", 1e18F);
    EXPECT_EQ(director.state().player_score, std::numeric_limits<uint32_t>::max());

    // A second extreme delta must still NOT wrap around (saturating add)
    director.notify_player_event("event2", 1e18F);
    EXPECT_EQ(director.state().player_score, std::numeric_limits<uint32_t>::max());
}

TEST(AiDirector, PlayerScoreLargeInRangeMultiply)
{
    // A large but FLOAT-EXACT delta below the saturation guard exercises the
    // non-saturating multiply path. (The exact UINT32_MAX/kScorePerUnit boundary
    // is not float-representable, so 1e6 — which is exact and well under the
    // guard — is used instead.)
    AiDirector director;
    constexpr float kDelta = 1000000.0F;  // float-exact; 1e6 * 10 fits in uint32
    director.notify_player_event("event", kDelta);
    EXPECT_EQ(director.state().player_score, 1000000U * AiDirector::kScorePerUnit);
}

// =============================================================================
// 10) configure_templates: replace on second call, clear on empty span
// =============================================================================
TEST(AiDirector, ConfigureTemplatesReplacesOnSecondCall)
{
    AiDirector director;
    const auto first = make_templates();
    director.configure_templates(first);

    // Request at full intensity — should pick boss_rush (tier 2) from first set
    director.notify_player_event("event", 1.0F);
    {
        const auto enc = director.request_next_encounter();
        ASSERT_TRUE(enc.has_value());
        EXPECT_EQ(enc->template_id, "boss_rush");
    }

    // Replace with a single simpler template set
    const std::vector<EncounterTemplate> second = {
        EncounterTemplate{"patrol", 0, 0.05F, {"guard"}},
    };
    director.configure_templates(second);

    // Now only tier-0 exists; intensity is still high
    {
        const auto enc = director.request_next_encounter();
        ASSERT_TRUE(enc.has_value());
        EXPECT_EQ(enc->template_id, "patrol");
    }
}

TEST(AiDirector, ConfigureTemplatesWithEmptySpanClearsTemplates)
{
    AiDirector director;
    const auto tmplts = make_templates();
    director.configure_templates(tmplts);

    // Set high intensity so non-empty set would return a value
    director.notify_player_event("event", 0.8F);
    ASSERT_TRUE(director.request_next_encounter().has_value());

    // Clear via empty span
    director.configure_templates(std::span<const EncounterTemplate>{});
    EXPECT_FALSE(director.request_next_encounter().has_value());
}

// =============================================================================
// 11) encounter selection: single template, tier gaps, all-same-tier tie-break
// =============================================================================
TEST(AiDirector, RequestEncounterSingleTemplate_AnyPositiveIntensity)
{
    AiDirector director;
    const std::vector<EncounterTemplate> single = {
        EncounterTemplate{"solo", 0, 0.10F, {"archer"}},
    };
    director.configure_templates(single);

    // Any positive intensity should return the sole template
    director.notify_player_event("event", 0.01F);
    const auto enc = director.request_next_encounter();
    ASSERT_TRUE(enc.has_value());
    EXPECT_EQ(enc->template_id, "solo");
    EXPECT_EQ(enc->difficulty_tier, 0U);
}

TEST(AiDirector, RequestEncounterTierGap_SkipsHighTier)
{
    // Tiers 0 and 5 only; at intensity 0.5:
    //   max_tier = floor(0.5 * 5) = 2 → tier 5 excluded → tier 0 selected
    AiDirector director;
    const std::vector<EncounterTemplate> gapped = {
        EncounterTemplate{"easy", 0, 0.05F, {}},
        EncounterTemplate{"extreme", 5, 0.99F, {"titan"}},
    };
    director.configure_templates(gapped);

    director.notify_player_event("event", 0.50F);
    const auto enc = director.request_next_encounter();
    ASSERT_TRUE(enc.has_value());
    EXPECT_EQ(enc->template_id, "easy");
    EXPECT_EQ(enc->difficulty_tier, 0U);
}

TEST(AiDirector, RequestEncounterTierGap_HighIntensityPicksHighTier)
{
    // At intensity 1.0: max_tier = floor(1.0 * 5) = 5 → tier 5 now eligible
    AiDirector director;
    const std::vector<EncounterTemplate> gapped = {
        EncounterTemplate{"easy", 0, 0.05F, {}},
        EncounterTemplate{"extreme", 5, 0.99F, {"titan"}},
    };
    director.configure_templates(gapped);

    director.notify_player_event("event", 1.0F);
    const auto enc = director.request_next_encounter();
    ASSERT_TRUE(enc.has_value());
    EXPECT_EQ(enc->template_id, "extreme");
    EXPECT_EQ(enc->difficulty_tier, 5U);
}

TEST(AiDirector, RequestEncounterAllSameTier_FirstInInsertionOrderWins)
{
    // All tier-1; at any positive intensity with max_configured_tier=1,
    // max_tier = floor(intensity * 1); if intensity >= 1.0 → max_tier=1 → all eligible.
    // First insertion wins on tie.
    AiDirector director;
    const std::vector<EncounterTemplate> same_tier = {
        EncounterTemplate{"alpha", 1, 0.10F, {}},
        EncounterTemplate{"beta",  1, 0.20F, {}},
        EncounterTemplate{"gamma", 1, 0.30F, {}},
    };
    director.configure_templates(same_tier);

    director.notify_player_event("event", 1.0F);
    const auto enc = director.request_next_encounter();
    ASSERT_TRUE(enc.has_value());
    // max_configured_tier=1; max_tier=floor(1.0*1)=1; all qualify; first wins
    EXPECT_EQ(enc->template_id, "alpha");
}

// =============================================================================
// 12) kRelief state pacing rules
// =============================================================================
TEST(AiDirector, ReliefStateDoesNotDropToNonIdleOnDecay)
{
    // In kRelief, the only valid exit is → kIdle; it cannot transition to kBuildUp/kPeak.
    AiDirector director;
    director.notify_player_event("spike", AiDirector::kThresholdRelief + 0.05F);
    ASSERT_EQ(director.state().tension, TensionState::kRelief);

    // Tick enough to drop intensity to just above kIdle threshold but below kBuildUp
    // kRelief total decay = (kDecayRatePerMs + kReliefDecayBonus) per ms
    // Intensity ≈ 0.90; need to drop to ~0.24
    // Δi needed = 0.90 - 0.24 = 0.66; total decay rate = 0.0004/ms
    // dt ≈ 0.66 / 0.0004 = 1650 ms.  Use 1700 ms to be conservative.
    director.tick(1700.0F);

    // Must still be kRelief or have transitioned all the way to kIdle — never kBuildUp
    const TensionState ts = director.state().tension;
    EXPECT_NE(ts, TensionState::kBuildUp);
    EXPECT_NE(ts, TensionState::kPeak);
}

TEST(AiDirector, ReliefExitsDirectlyToIdle_NotBuildUp)
{
    // Verify the kRelief → kIdle (skipping kBuildUp) transition rule.
    AiDirector director;
    director.notify_player_event("spike", AiDirector::kThresholdRelief + 0.05F);
    ASSERT_EQ(director.state().tension, TensionState::kRelief);

    // Tick until intensity is zero → must end in kIdle, never kBuildUp
    director.tick(5000.0F);
    EXPECT_EQ(director.state().tension, TensionState::kIdle);
}

TEST(AiDirector, ReliefAppliesAcceleratedDecayVsNormalDecay)
{
    // Two directors: one enters kRelief (extra decay), one stays in kBuildUp.
    // After the same tick, the kRelief director should have lower intensity.
    AiDirector relief_dir;
    AiDirector normal_dir;

    // Both start at same intensity (in kRelief range for relief_dir)
    relief_dir.notify_player_event("event", AiDirector::kThresholdRelief + 0.05F);
    ASSERT_EQ(relief_dir.state().tension, TensionState::kRelief);

    // Normal dir at high kBuildUp intensity (below kRelief threshold)
    normal_dir.notify_player_event("event", AiDirector::kThresholdPeak - 0.01F);
    ASSERT_EQ(normal_dir.state().tension, TensionState::kBuildUp);

    // Use same starting intensity for fair comparison by explicitly tracking the decay
    const float relief_before = relief_dir.state().intensity;
    const float normal_before = normal_dir.state().intensity;

    relief_dir.tick(200.0F);
    normal_dir.tick(200.0F);

    const float relief_drop = relief_before - relief_dir.state().intensity;
    const float normal_drop = normal_before - normal_dir.state().intensity;

    // kRelief must decay faster
    EXPECT_GT(relief_drop, normal_drop);
}

// =============================================================================
// 13) pacing natural decay: kBuildUp → kIdle without event injection
// =============================================================================
TEST(AiDirector, NaturalDecayBuildUpToIdle)
{
    AiDirector director;
    // Start in kBuildUp
    director.notify_player_event("event", AiDirector::kThresholdBuildUp + 0.05F);
    ASSERT_EQ(director.state().tension, TensionState::kBuildUp);

    // Natural decay rate = kDecayRatePerMs = 0.0001/ms.
    // Intensity ≈ 0.30; need to drop below 0.25.
    // Δi = 0.30 - 0.25 = 0.05; dt = 0.05 / 0.0001 = 500 ms → use 600 ms.
    director.tick(600.0F);
    EXPECT_EQ(director.state().tension, TensionState::kIdle);
}

// =============================================================================
// 14) event_id string is purely advisory (does not affect behaviour)
// =============================================================================
TEST(AiDirector, EventIdStringIsIgnored)
{
    AiDirector dirA;
    AiDirector dirB;

    dirA.notify_player_event("enemy_killed", 0.30F);
    dirB.notify_player_event("",             0.30F);  // empty string

    EXPECT_FLOAT_EQ(dirA.state().intensity, dirB.state().intensity);
    EXPECT_EQ(dirA.state().tension, dirB.state().tension);
    EXPECT_EQ(dirA.state().player_score, dirB.state().player_score);
}

// =============================================================================
// 15) intensity_contribution field accessible from returned encounter
// =============================================================================
TEST(AiDirector, EncounterTemplateFieldsRoundTrip)
{
    AiDirector director;
    const std::vector<EncounterTemplate> tmplts = {
        EncounterTemplate{"raid", 0, 0.35F, {"knight", "archer", "mage"}},
    };
    director.configure_templates(tmplts);
    director.notify_player_event("event", 0.50F);

    const auto enc = director.request_next_encounter();
    ASSERT_TRUE(enc.has_value());
    EXPECT_EQ(enc->template_id, "raid");
    EXPECT_EQ(enc->difficulty_tier, 0U);
    EXPECT_NEAR(enc->intensity_contribution, 0.35F, 1e-6F);
    ASSERT_EQ(enc->spawn_entity_ids.size(), 3U);
    EXPECT_EQ(enc->spawn_entity_ids[0], "knight");
    EXPECT_EQ(enc->spawn_entity_ids[1], "archer");
    EXPECT_EQ(enc->spawn_entity_ids[2], "mage");
}

// =============================================================================
// 16) Encounter selection at intensity threshold exact boundary values
// =============================================================================
TEST(AiDirector, RequestEncounterAtExactPeakThreshold)
{
    // intensity = kThresholdPeak (0.60) with max_configured_tier=2:
    // max_tier = floor(0.60 * 2) = 1 → tier 1 selected (not tier 2)
    AiDirector director;
    director.configure_templates(make_templates());

    director.notify_player_event("event", AiDirector::kThresholdPeak);
    const auto enc = director.request_next_encounter();
    ASSERT_TRUE(enc.has_value());
    EXPECT_EQ(enc->difficulty_tier, 1U);
    EXPECT_EQ(enc->template_id, "hard_ambush");
}

TEST(AiDirector, RequestEncounterAtExactReliefThreshold)
{
    // intensity = kThresholdRelief (0.85) with max_configured_tier=2:
    // max_tier = floor(0.85 * 2) = 1 → tier 1 selected
    AiDirector director;
    director.configure_templates(make_templates());

    director.notify_player_event("event", AiDirector::kThresholdRelief);
    EXPECT_EQ(director.state().tension, TensionState::kRelief);
    const auto enc = director.request_next_encounter();
    ASSERT_TRUE(enc.has_value());
    EXPECT_EQ(enc->difficulty_tier, 1U);
    EXPECT_EQ(enc->template_id, "hard_ambush");
}

}  // namespace
