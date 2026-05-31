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

}  // namespace
