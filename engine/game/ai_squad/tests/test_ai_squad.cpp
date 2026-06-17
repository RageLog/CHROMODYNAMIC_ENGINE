// =============================================================================
// CHROMODYNAMIC — tests/test_ai_squad.cpp
// Phase 682 — cd::ai::squad unit tests.
//
// Covers the 5+ contract requirements from the M13 W5A brief:
//   1. add_member enlistment + members() span.
//   2. remove_member removes an agent; remaining agents unaffected.
//   3. update_position feeds position; tick() recomputes formation centroid.
//   4. Formation radius grows when members spread out.
//   5. Blackboard threat_level decays toward 0 over ticks.
//   6. (extra) set_target / raise_threat / set_fact write to blackboard.
//   7. (extra) Idempotent add_member: duplicate entity_id ignored.
//   8. (extra) Empty-squad tick: formation stays zeroed, no crash.
//   9. (extra) find_member returns correct pointer or nullptr.
//  10. (extra) 4-agent squad formation centroid is mean of positions.
// =============================================================================
#include <cd/ai/squad/AiSquad.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <string>

namespace
{

using cd::ai::squad::Blackboard;
using cd::ai::squad::Formation;
using cd::ai::squad::Squad;
using cd::ai::squad::SquadMember;
using cd::ai::squad::SquadRole;

// Helper: build a 4-agent squad at cardinal positions.
//   entity 1 = pointman  @ (+2, 0, 0)
//   entity 2 = flanker   @ (-2, 0, 0)
//   entity 3 = suppressor@ (0, 0, +2)
//   entity 4 = coverer   @ (0, 0, -2)
// Centroid should be (0, 0, 0); radius should be 2.
inline Squad make_cardinal_squad()
{
    Squad sq;
    sq.add_member(1U, SquadRole::kPointman);
    sq.add_member(2U, SquadRole::kFlanker);
    sq.add_member(3U, SquadRole::kSuppressor);
    sq.add_member(4U, SquadRole::kCoverer);
    sq.update_position(1U, {2.0F, 0.0F, 0.0F});
    sq.update_position(2U, {-2.0F, 0.0F, 0.0F});
    sq.update_position(3U, {0.0F, 0.0F, 2.0F});
    sq.update_position(4U, {0.0F, 0.0F, -2.0F});
    return sq;
}

constexpr float kEps = 1e-4F;

// =============================================================================
// 1) add_member enlistment + members() span
// =============================================================================
TEST(AiSquad, AddMemberEnlists)
{
    Squad sq;
    EXPECT_TRUE(sq.empty());
    EXPECT_EQ(sq.size(), 0U);

    sq.add_member(42U, SquadRole::kPointman);
    EXPECT_EQ(sq.size(), 1U);
    EXPECT_FALSE(sq.empty());

    const auto span = sq.members();
    ASSERT_EQ(span.size(), 1U);
    EXPECT_EQ(span[0].entity_id, 42U);
    EXPECT_EQ(span[0].role, SquadRole::kPointman);
}

// =============================================================================
// 2) remove_member removes an agent; remaining agents unaffected
// =============================================================================
TEST(AiSquad, RemoveMember)
{
    Squad sq;
    sq.add_member(1U, SquadRole::kPointman);
    sq.add_member(2U, SquadRole::kFlanker);
    sq.add_member(3U, SquadRole::kSuppressor);

    EXPECT_EQ(sq.size(), 3U);
    sq.remove_member(2U);
    EXPECT_EQ(sq.size(), 2U);

    // Verify entity 2 is gone and others remain.
    EXPECT_EQ(sq.find_member(2U), nullptr);
    EXPECT_NE(sq.find_member(1U), nullptr);
    EXPECT_NE(sq.find_member(3U), nullptr);
}

// =============================================================================
// 3) update_position feeds position; tick() recomputes formation centroid
// =============================================================================
TEST(AiSquad, TickRecomputesCentroid)
{
    // Two agents symmetrically placed; centroid should be (0, 0, 0).
    Squad sq;
    sq.add_member(1U, SquadRole::kPointman);
    sq.add_member(2U, SquadRole::kFlanker);
    sq.update_position(1U, {4.0F, 0.0F, 0.0F});
    sq.update_position(2U, {-4.0F, 0.0F, 0.0F});

    sq.tick(0.016F);

    const Formation& f = sq.formation();
    EXPECT_NEAR(f.centroid[0], 0.0F, kEps);
    EXPECT_NEAR(f.centroid[1], 0.0F, kEps);
    EXPECT_NEAR(f.centroid[2], 0.0F, kEps);
    EXPECT_EQ(f.member_count, 2U);
}

// =============================================================================
// 4) Formation radius grows when members spread out
// =============================================================================
TEST(AiSquad, FormationRadiusReflectsSpread)
{
    Squad sq = make_cardinal_squad();
    sq.tick(0.0F);  // dt=0: no decay, just recomputes geometry.

    const Formation& f = sq.formation();
    // Centroid = (0,0,0); all members at distance 2.
    EXPECT_NEAR(f.radius, 2.0F, kEps);
    EXPECT_EQ(f.member_count, 4U);

    // Spread two members further.
    sq.update_position(1U, {10.0F, 0.0F, 0.0F});
    sq.update_position(2U, {-10.0F, 0.0F, 0.0F});
    sq.tick(0.0F);

    // New centroid still (0,0,0), radius now 10.
    const Formation& f2 = sq.formation();
    EXPECT_NEAR(f2.radius, 10.0F, kEps);
}

// =============================================================================
// 5) Blackboard threat_level decays toward 0 over ticks
// =============================================================================
TEST(AiSquad, ThreatDecaysOverTime)
{
    Squad sq;
    sq.add_member(1U, SquadRole::kSuppressor);
    sq.raise_threat(1.0F);
    EXPECT_NEAR(sq.blackboard().threat_level, 1.0F, kEps);

    // After 1 second: threat ~ exp(-1.0 * 1.0) ~ 0.3679.
    sq.tick(1.0F);
    const float expected = std::exp(-Squad::kThreatDecayRate * 1.0F);
    EXPECT_NEAR(sq.blackboard().threat_level, expected, 1e-3F);

    // After enough ticks, threat should approach 0.
    sq.tick(10.0F);
    EXPECT_LT(sq.blackboard().threat_level, 0.01F);
}

// =============================================================================
// 6) set_target / raise_threat / set_fact write to blackboard
// =============================================================================
TEST(AiSquad, BlackboardWrites)
{
    Squad sq;
    sq.set_target(99U, {5.0F, 0.0F, 3.0F});
    sq.raise_threat(0.7F);
    sq.set_fact("alarm_triggered", 1.0F);
    sq.set_fact("suppression_rounds", 12.0F);

    const Blackboard& bb = sq.blackboard();
    EXPECT_EQ(bb.target_entity_id, 99U);
    EXPECT_NEAR(bb.target_position[0], 5.0F, kEps);
    EXPECT_NEAR(bb.target_position[2], 3.0F, kEps);
    EXPECT_NEAR(bb.threat_level, 0.7F, kEps);
    EXPECT_NEAR(bb.shared_facts.at("alarm_triggered"), 1.0F, kEps);
    EXPECT_NEAR(bb.shared_facts.at("suppression_rounds"), 12.0F, kEps);
}

// =============================================================================
// 7) Idempotent add_member: duplicate entity_id ignored
// =============================================================================
TEST(AiSquad, DuplicateAddIsIdempotent)
{
    Squad sq;
    sq.add_member(7U, SquadRole::kPointman);
    sq.add_member(7U, SquadRole::kFlanker);  // duplicate — should be ignored.
    sq.add_member(7U, SquadRole::kCoverer);  // duplicate again.

    EXPECT_EQ(sq.size(), 1U);
    // Role should still be kPointman (the first add).
    const SquadMember* m = sq.find_member(7U);
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->role, SquadRole::kPointman);
}

// =============================================================================
// 8) Empty-squad tick: formation stays zeroed, no crash
// =============================================================================
TEST(AiSquad, EmptySquadTickIsNoop)
{
    Squad sq;
    EXPECT_NO_THROW(sq.tick(1.0F));

    const Formation& f = sq.formation();
    EXPECT_EQ(f.member_count, 0U);
    EXPECT_NEAR(f.radius, 0.0F, kEps);
    EXPECT_NEAR(f.centroid[0], 0.0F, kEps);
}

// =============================================================================
// 9) find_member returns correct pointer or nullptr
// =============================================================================
TEST(AiSquad, FindMember)
{
    Squad sq;
    sq.add_member(10U, SquadRole::kFlanker);
    sq.add_member(20U, SquadRole::kCoverer);

    const SquadMember* p10 = sq.find_member(10U);
    ASSERT_NE(p10, nullptr);
    EXPECT_EQ(p10->role, SquadRole::kFlanker);

    const SquadMember* p99 = sq.find_member(99U);
    EXPECT_EQ(p99, nullptr);
}

// =============================================================================
// 10) 4-agent squad formation centroid is mean of positions
// =============================================================================
TEST(AiSquad, FourAgentCentroidIsMean)
{
    // Place 4 agents at non-symmetric positions; verify centroid = mean.
    Squad sq;
    sq.add_member(1U, SquadRole::kPointman);
    sq.add_member(2U, SquadRole::kFlanker);
    sq.add_member(3U, SquadRole::kSuppressor);
    sq.add_member(4U, SquadRole::kCoverer);

    sq.update_position(1U, {1.0F, 0.0F, 0.0F});
    sq.update_position(2U, {3.0F, 0.0F, 0.0F});
    sq.update_position(3U, {5.0F, 4.0F, 0.0F});
    sq.update_position(4U, {7.0F, 4.0F, 0.0F});

    sq.tick(0.0F);

    // Expected centroid: x=(1+3+5+7)/4=4, y=(0+0+4+4)/4=2, z=0.
    const Formation& f = sq.formation();
    EXPECT_NEAR(f.centroid[0], 4.0F, kEps);
    EXPECT_NEAR(f.centroid[1], 2.0F, kEps);
    EXPECT_NEAR(f.centroid[2], 0.0F, kEps);
    EXPECT_EQ(f.member_count, 4U);

    // Farthest member: entity 4 at (7,4,0) → dist from (4,2,0) = sqrt(9+4) = sqrt(13) ≈ 3.606.
    const float expected_radius = std::sqrt(13.0F);
    EXPECT_NEAR(f.radius, expected_radius, 1e-3F);
}

// =============================================================================
// 11) Capacity cap: add_member is a no-op past 255 members (Formation.member_count
//     is uint8_t). This locks the early-return guard in add_member() that was
//     never exercised before — without it, member 256 would silently corrupt
//     the uint8_t formation count. (Band-3 squad-v1 seal: CPU charter edge.)
// =============================================================================
TEST(AiSquad, CapacityCappedAt255)
{
    Squad sq;
    for (std::uint64_t id = 1U; id <= 300U; ++id)
    {
        sq.add_member(id, SquadRole::kSuppressor);
    }
    // Only the first 255 enlistments succeed; 256..300 are dropped.
    EXPECT_EQ(sq.size(), 255U);

    sq.tick(0.0F);
    // member_count is uint8_t — 255 must round-trip exactly, no overflow to 0.
    EXPECT_EQ(sq.formation().member_count, 255U);

    // Entities 1..255 are present; 256 was rejected by the cap.
    EXPECT_NE(sq.find_member(255U), nullptr);
    EXPECT_EQ(sq.find_member(256U), nullptr);
}

// =============================================================================
// 12) raise_threat clamps to [0, 1] at BOTH boundaries. Existing decay test
//     never drives threat above 1 or below 0, so the std::clamp upper/lower
//     edges in raise_threat() were untested.
// =============================================================================
TEST(AiSquad, RaiseThreatClampsToUnitRange)
{
    Squad sq;
    // Overshoot the upper bound: 0.7 + 0.6 = 1.3 → clamped to 1.0.
    sq.raise_threat(0.7F);
    sq.raise_threat(0.6F);
    EXPECT_NEAR(sq.blackboard().threat_level, 1.0F, kEps);

    // Negative delta drives toward 0 and clamps at the lower bound (no
    // negative threat). 1.0 + (-5.0) = -4.0 → clamped to 0.0.
    sq.raise_threat(-5.0F);
    EXPECT_NEAR(sq.blackboard().threat_level, 0.0F, kEps);
}

// =============================================================================
// 13) update_health clamps to [0, 1] and ignores unknown entities. The health
//     clamp + the not-a-member early-return were both untested branches.
// =============================================================================
TEST(AiSquad, UpdateHealthClampsAndIgnoresUnknown)
{
    Squad sq;
    sq.add_member(1U, SquadRole::kPointman);

    sq.update_health(1U, 2.5F);   // above 1 → clamps to 1.
    ASSERT_NE(sq.find_member(1U), nullptr);
    EXPECT_NEAR(sq.find_member(1U)->health, 1.0F, kEps);

    sq.update_health(1U, -3.0F);  // below 0 → clamps to 0.
    EXPECT_NEAR(sq.find_member(1U)->health, 0.0F, kEps);

    // Unknown entity is a no-op: the known member's health is untouched.
    sq.update_health(999U, 0.5F);
    EXPECT_NEAR(sq.find_member(1U)->health, 0.0F, kEps);
    EXPECT_EQ(sq.find_member(999U), nullptr);
}

}  // namespace
