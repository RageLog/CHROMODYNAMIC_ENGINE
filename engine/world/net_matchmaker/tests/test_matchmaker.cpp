// =============================================================================
// CHROMODYNAMIC — cd::net::matchmaker tests
// Phase 563 / Sprint W5B
// Phase 784 / FINALE-E4  — SkillScorer + FillStrategy tests
//
// Tests: 11
//   1.  LobbyLifecycle — create + join + leave round-trip
//   2.  FindMatchSkillWithinDelta — returns an open lobby when skill fits
//   3.  FindMatchSkillOutsideDelta — returns nullopt when skill is too far
//   4.  FindMatchRegionRespected — ignores lobby with mismatched region
//   5.  CloseLobbyRemovedFromActive — close_lobby marks lobby as not open
//   6.  FindMatchPrefersMostPopulated — pack-the-room heuristic
//   7.  FindMatchEmptyLobbyNoConstraints — empty lobby accepted regardless of skill
//   8.  SkillScorer_HighSkillDeltaPenalised — large skill gap raises score
//   9.  SkillScorer_RegionDistanceRespected — closer lobby wins kBalanced
//  10.  FastFillReturnsFirstMatch — kFastFill bypasses scoring and returns first
//  11.  SkillScorer_HistoryAvoidancePenalises — repeated pairing raises score
// =============================================================================
#include <cd/net/matchmaker/Matchmaker.hpp>
#include <gtest/gtest.h>

#include <array>
#include <cstdint>

namespace
{

using cd::net::matchmaker::FillStrategy;
using cd::net::matchmaker::Lobby;
using cd::net::matchmaker::LobbyRegistry;
using cd::net::matchmaker::PlayerProfile;
using cd::net::matchmaker::SkillBasedFinder;
using cd::net::matchmaker::SkillScorer;

// ---------------------------------------------------------------------------
// Helper factories
// ---------------------------------------------------------------------------

[[nodiscard]] PlayerProfile make_profile(std::uint64_t               id,
                                          float                       skill,
                                          std::array<std::uint8_t, 4> region,
                                          float                       lat = 0.0F,
                                          float                       lon = 0.0F)
{
    PlayerProfile p;
    p.id           = id;
    p.skill_rating = skill;
    p.region       = region;
    p.lat_deg      = lat;
    p.lon_deg      = lon;
    return p;
}

constexpr std::array<std::uint8_t, 4> kRegionEU  { 1, 0, 0, 0 };
constexpr std::array<std::uint8_t, 4> kRegionNA  { 2, 0, 0, 0 };

}  // namespace

// ---------------------------------------------------------------------------
// 1. Create + join + leave round-trip
// ---------------------------------------------------------------------------
TEST(LobbyLifecycle, CreateJoinLeaveRoundTrip)
{
    LobbyRegistry reg;

    const std::uint64_t lid = reg.create_lobby("deathmatch");
    EXPECT_NE(lid, 0U);

    EXPECT_TRUE(reg.join_lobby(lid, 100U));
    EXPECT_TRUE(reg.join_lobby(lid, 200U));

    // Verify both players are listed.
    const auto lobbies = reg.active_lobbies();
    ASSERT_EQ(lobbies.size(), 1U);
    EXPECT_EQ(lobbies[0].player_ids.size(), 2U);
    EXPECT_EQ(lobbies[0].game_mode, "deathmatch");
    EXPECT_TRUE(lobbies[0].is_open);

    EXPECT_TRUE(reg.leave_lobby(lid, 100U));
    const auto after_leave = reg.active_lobbies();
    ASSERT_EQ(after_leave.size(), 1U);
    EXPECT_EQ(after_leave[0].player_ids.size(), 1U);

    // Leaving a non-existent player returns false.
    EXPECT_FALSE(reg.leave_lobby(lid, 999U));

    // Joining a non-existent lobby returns false.
    EXPECT_FALSE(reg.join_lobby(0xDEADBEEFULL, 300U));
}

// ---------------------------------------------------------------------------
// 2. find_match returns an existing lobby when skill is within delta
// ---------------------------------------------------------------------------
TEST(SkillBasedFinder, FindMatchSkillWithinDelta)
{
    LobbyRegistry reg;
    SkillBasedFinder finder;
    finder.configure(4, 50.0F);

    const PlayerProfile seed = make_profile(1, 1000.0F, kRegionEU);
    reg.register_player(seed);

    const std::uint64_t lid = reg.create_lobby("ranked");
    ASSERT_TRUE(reg.join_lobby(lid, seed.id));

    const PlayerProfile candidate = make_profile(2, 1030.0F, kRegionEU);
    const auto result = finder.find_match(candidate, reg);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, lid);
}

// ---------------------------------------------------------------------------
// 3. find_match returns nullopt when skill exceeds delta
// ---------------------------------------------------------------------------
TEST(SkillBasedFinder, FindMatchSkillOutsideDelta)
{
    LobbyRegistry reg;
    SkillBasedFinder finder;
    finder.configure(4, 50.0F);

    const PlayerProfile seed = make_profile(1, 1000.0F, kRegionEU);
    reg.register_player(seed);

    const std::uint64_t lid = reg.create_lobby("ranked");
    ASSERT_TRUE(reg.join_lobby(lid, seed.id));

    // Candidate is 200 skill points away — outside the 50-point delta.
    const PlayerProfile candidate = make_profile(2, 1200.0F, kRegionEU);
    const auto result = finder.find_match(candidate, reg);
    EXPECT_FALSE(result.has_value());
}

// ---------------------------------------------------------------------------
// 4. find_match respects region preference (ignores mismatched region)
// ---------------------------------------------------------------------------
TEST(SkillBasedFinder, FindMatchRegionRespected)
{
    LobbyRegistry reg;
    SkillBasedFinder finder;
    finder.configure(4, 200.0F);  // generous skill delta so skill is not the filter

    // Seed player in EU lobby — same skill band as candidate.
    const PlayerProfile eu_seed = make_profile(1, 1000.0F, kRegionEU);
    reg.register_player(eu_seed);
    const std::uint64_t eu_lid = reg.create_lobby("ctf");
    ASSERT_TRUE(reg.join_lobby(eu_lid, eu_seed.id));

    // Candidate is in NA — should NOT match the EU lobby.
    const PlayerProfile na_candidate = make_profile(2, 1050.0F, kRegionNA);
    const auto result = finder.find_match(na_candidate, reg);
    EXPECT_FALSE(result.has_value());
}

// ---------------------------------------------------------------------------
// 5. close_lobby removes lobby from active set
// ---------------------------------------------------------------------------
TEST(LobbyLifecycle, CloseLobbyRemovedFromActive)
{
    LobbyRegistry reg;

    const std::uint64_t lid1 = reg.create_lobby("survival");
    const std::uint64_t lid2 = reg.create_lobby("survival");

    reg.close_lobby(lid1);

    const auto lobbies = reg.active_lobbies();
    // Both entries are still in the vector but lid1 is closed.
    bool found_open = false;
    bool found_closed = false;
    for (const Lobby& l : lobbies)
    {
        if (l.lobby_id == lid1) found_closed = !l.is_open;
        if (l.lobby_id == lid2) found_open   =  l.is_open;
    }
    EXPECT_TRUE(found_closed) << "lid1 should be closed";
    EXPECT_TRUE(found_open)   << "lid2 should still be open";

    // SkillBasedFinder must skip the closed lobby.
    SkillBasedFinder finder;
    finder.configure(4, 500.0F);
    const PlayerProfile p = make_profile(99, 100.0F, kRegionEU);
    // No profiles registered — lobby constraints are unconstrained, but lid1
    // is closed so only lid2 is eligible.
    const auto result = finder.find_match(p, reg);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, lid2);
}

// ---------------------------------------------------------------------------
// 6. find_match prefers the most populated eligible lobby
// ---------------------------------------------------------------------------
TEST(SkillBasedFinder, FindMatchPrefersMostPopulated)
{
    LobbyRegistry reg;
    SkillBasedFinder finder;
    finder.configure(8, 100.0F);

    // Lobby A — 1 player.
    const PlayerProfile p1 = make_profile(1, 500.0F, kRegionNA);
    reg.register_player(p1);
    const std::uint64_t lid_a = reg.create_lobby("ffa");
    ASSERT_TRUE(reg.join_lobby(lid_a, p1.id));

    // Lobby B — 3 players.
    const std::uint64_t lid_b = reg.create_lobby("ffa");
    for (std::uint64_t i = 20; i < 23; ++i)
    {
        const PlayerProfile pi = make_profile(i, 520.0F, kRegionNA);
        reg.register_player(pi);
        ASSERT_TRUE(reg.join_lobby(lid_b, pi.id));
    }

    // Candidate in NA, skill 510 — within 100 of both lobbies' players.
    const PlayerProfile candidate = make_profile(99, 510.0F, kRegionNA);
    const auto result = finder.find_match(candidate, reg);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, lid_b);  // B has more players.
}

// ---------------------------------------------------------------------------
// 7. find_match accepts an empty lobby regardless of skill/region (no peers)
// ---------------------------------------------------------------------------
TEST(SkillBasedFinder, FindMatchEmptyLobbyNoConstraints)
{
    LobbyRegistry reg;
    SkillBasedFinder finder;
    finder.configure(4, 10.0F);  // tight skill delta

    // Create an empty lobby — no one has joined yet.
    const std::uint64_t lid = reg.create_lobby("tdm");

    // Candidate with any skill/region should match the empty lobby.
    const PlayerProfile candidate = make_profile(1, 9999.0F, kRegionNA);
    const auto result = finder.find_match(candidate, reg);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, lid);
}

// ---------------------------------------------------------------------------
// 8. SkillScorer: large skill gap produces higher score than small gap
// ---------------------------------------------------------------------------
TEST(SkillScorer, HighSkillDeltaPenalised)
{
    SkillScorer scorer;
    scorer.set_weights(1.0F, 0.0F, 0.0F);  // skill-only
    scorer.set_max_skill_range(1000.0F);

    // Player A: skill 500, at (0,0).
    const PlayerProfile a = make_profile(1, 500.0F, kRegionEU, 0.0F, 0.0F);

    // Close peer: delta = 20 → score ≈ 0.02.
    const PlayerProfile close_peer = make_profile(2, 520.0F, kRegionEU, 0.0F, 0.0F);

    // Far peer: delta = 400 → score ≈ 0.40.
    const PlayerProfile far_peer   = make_profile(3, 900.0F, kRegionEU, 0.0F, 0.0F);

    const float score_close = scorer.score_pair(a, close_peer);
    const float score_far   = scorer.score_pair(a, far_peer);

    EXPECT_LT(score_close, score_far)
        << "Close skill peer must score better (lower) than far peer";

    // Exact values within tolerance.
    EXPECT_NEAR(score_close, 0.02F, 1e-4F);
    EXPECT_NEAR(score_far,   0.40F, 1e-4F);
}

// ---------------------------------------------------------------------------
// 9. SkillScorer: region distance respected — kBalanced picks closer lobby
// ---------------------------------------------------------------------------
TEST(SkillBasedFinder, RegionDistanceRespectedByBalancedStrategy)
{
    // Approximate real coords:
    //   London:    51.5N,   0.0E
    //   New York:  40.7N,  74.1W
    //   Frankfurt: 50.1N,   8.7E  (close to London)

    LobbyRegistry reg;
    SkillBasedFinder finder;
    // w_skill=0 so only distance matters in scoring; region bytes identical
    // so the hard region-equality gate is bypassed (same region tag for all).
    finder.configure(4, 500.0F,
                     /*w_skill*/   0.0F,
                     /*w_region*/  1.0F,
                     /*w_history*/ 0.0F,
                     FillStrategy::kBalanced);

    // Lobby A — seed player in New York.
    const PlayerProfile ny_seed = make_profile(1, 1000.0F, kRegionNA,
                                                40.7F, -74.1F);
    reg.register_player(ny_seed);
    const std::uint64_t lid_a = reg.create_lobby("ranked");
    ASSERT_TRUE(reg.join_lobby(lid_a, ny_seed.id));

    // Lobby B — seed player in Frankfurt (much closer to London).
    const PlayerProfile fra_seed = make_profile(2, 1000.0F, kRegionNA,
                                                 50.1F,   8.7F);
    reg.register_player(fra_seed);
    const std::uint64_t lid_b = reg.create_lobby("ranked");
    ASSERT_TRUE(reg.join_lobby(lid_b, fra_seed.id));

    // Candidate in London — should prefer Frankfurt lobby.
    const PlayerProfile london = make_profile(99, 1000.0F, kRegionNA,
                                               51.5F,   0.0F);
    const auto result = finder.find_match(london, reg);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, lid_b)
        << "London candidate should be matched to Frankfurt lobby (closer)";
}

// ---------------------------------------------------------------------------
// 10. kFastFill returns the first qualifying lobby without scoring
// ---------------------------------------------------------------------------
TEST(SkillBasedFinder, FastFillReturnsFirstMatch)
{
    LobbyRegistry reg;
    SkillBasedFinder finder;
    finder.configure(4, 500.0F,
                     /*w_skill*/   1.0F,
                     /*w_region*/  1.0F,
                     /*w_history*/ 1.0F,
                     FillStrategy::kFastFill);

    // Lobby A — 1 player, skill 800 (farther from candidate than B).
    const PlayerProfile seed_a = make_profile(1, 800.0F, kRegionEU);
    reg.register_player(seed_a);
    const std::uint64_t lid_a = reg.create_lobby("ranked");
    ASSERT_TRUE(reg.join_lobby(lid_a, seed_a.id));

    // Lobby B — 3 players, skill 505 (closer to candidate).
    const std::uint64_t lid_b = reg.create_lobby("ranked");
    for (std::uint64_t i = 10; i < 13; ++i)
    {
        const PlayerProfile pi = make_profile(i, 505.0F, kRegionEU);
        reg.register_player(pi);
        ASSERT_TRUE(reg.join_lobby(lid_b, pi.id));
    }

    // Candidate — within delta of both lobbies.
    const PlayerProfile candidate = make_profile(99, 500.0F, kRegionEU);

    // kFastFill must return the *first* qualifying lobby (lid_a, created first),
    // regardless of score or population.
    const auto result = finder.find_match(candidate, reg);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, lid_a)
        << "kFastFill must return the first qualifying lobby, not the best-scored one";
}

// ---------------------------------------------------------------------------
// 11. SkillScorer history avoidance penalises repeated pairing
// ---------------------------------------------------------------------------
TEST(SkillScorer, HistoryAvoidancePenalises)
{
    SkillScorer scorer;
    scorer.set_weights(0.0F, 0.0F, 1.0F);  // history-only

    const PlayerProfile a = make_profile(1, 500.0F, kRegionEU, 0.0F, 0.0F);
    const PlayerProfile b = make_profile(2, 500.0F, kRegionEU, 0.0F, 0.0F);
    const PlayerProfile c = make_profile(3, 500.0F, kRegionEU, 0.0F, 0.0F);

    // a and b have played together; a and c have not.
    scorer.add_history_pair(a.id, b.id);

    const float score_ab = scorer.score_pair(a, b);
    const float score_ac = scorer.score_pair(a, c);

    EXPECT_GT(score_ab, score_ac)
        << "Repeated a-b pairing must score worse (higher) than fresh a-c pairing";

    EXPECT_NEAR(score_ab, 1.0F, 1e-5F);  // full penalty
    EXPECT_NEAR(score_ac, 0.0F, 1e-5F);  // no penalty
}
