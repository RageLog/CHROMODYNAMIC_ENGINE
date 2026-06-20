// =============================================================================
// CHROMODYNAMIC — cd::net::matchmaker tests
// Phase 563 / Sprint W5B
// Phase 784 / FINALE-E4  — SkillScorer + FillStrategy tests
// Phase 825 / FINALE-E5  — MatchmakingQueue + widening tests
//
// Tests: 35
//   --- LobbyLifecycle ---
//   1.  CreateJoinLeaveRoundTrip
//   2.  CloseLobbyRemovedFromActive
//   3.  JoinClosedLobbyReturnsFalse        (negative)
//   4.  LeaveNonExistentLobbyReturnsFalse  (negative)
//   5.  MultipleLobbiesCoexist
//   --- SkillBasedFinder ---
//   6.  FindMatchSkillWithinDelta
//   7.  FindMatchSkillOutsideDelta          (negative)
//   8.  FindMatchRegionRespected            (negative)
//   9.  FindMatchPrefersMostPopulated
//  10.  FindMatchEmptyLobbyNoConstraints
//  11.  RegionDistanceRespectedByBalancedStrategy
//  12.  FastFillReturnsFirstMatch
//  13.  StratifiedPicksClosestDistance
//  14.  SinglePlayerNoMatch                 (negative, zero open lobbies)
//  15.  ExactFillLobbyBecomesUnavailable
//  --- SkillScorer ---
//  16.  HighSkillDeltaPenalised
//  17.  HistoryAvoidancePenalises
//  18.  HistoryPairKeySymmetric
//  19.  ZeroWeightsAlwaysZeroScore
//  20.  MaxDistanceNormalisedToOne
//  --- MatchmakingQueue — enqueue / dequeue / cancel ---
//  21.  EnqueueReturnsUniqueIds
//  22.  DuplicatePlayerRejected             (negative)
//  23.  CancelPendingTicket
//  24.  CancelNonExistentReturnsFalse       (negative)
//  25.  CancelAlreadyMatchedReturnsFalse    (terminal state)
//  26.  StatusOfUnknownIdReturnsNull        (negative)
//  27.  EmptyQueueRunCycleReturnsZeros
//  --- MatchmakingQueue — matching ---
//  28.  ExactFillMatchSingleLobby
//  29.  PartialFillTicketsRemainPending
//  30.  SkillWindowGatesIncompatiblePlayers
//  31.  SkillWindowWideningAllowsEventualMatch
//  32.  PriorityOrderingRespected
//  33.  FairnessFirstInFirstOut
//  34.  ExpiryRemovesOldTickets
//  35.  RegionConstraintInQueue
// =============================================================================
#include <cd/net/matchmaker/Matchmaker.hpp>
#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string>

namespace
{

using cd::net::matchmaker::FillStrategy;
using cd::net::matchmaker::Lobby;
using cd::net::matchmaker::LobbyRegistry;
using cd::net::matchmaker::MatchmakingQueue;
using cd::net::matchmaker::MatchTicket;
using cd::net::matchmaker::PlayerProfile;
using cd::net::matchmaker::SkillBasedFinder;
using cd::net::matchmaker::SkillScorer;
using cd::net::matchmaker::TicketStatus;

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

// ===========================================================================
// Phase 825 — new tests
// ===========================================================================

// ---------------------------------------------------------------------------
// 3. Negative: joining a closed lobby returns false
// ---------------------------------------------------------------------------
TEST(LobbyLifecycle, JoinClosedLobbyReturnsFalse)
{
    LobbyRegistry reg;
    const std::uint64_t lid = reg.create_lobby("ranked");
    reg.close_lobby(lid);
    EXPECT_FALSE(reg.join_lobby(lid, 42U))
        << "join_lobby must fail for a closed lobby";
}

// ---------------------------------------------------------------------------
// 4. Negative: leaving a lobby that doesn't exist returns false
// ---------------------------------------------------------------------------
TEST(LobbyLifecycle, LeaveNonExistentLobbyReturnsFalse)
{
    LobbyRegistry reg;
    EXPECT_FALSE(reg.leave_lobby(0xDEADULL, 1U));
    EXPECT_FALSE(reg.leave_lobby(1U, 999U));  // lobby exists but player absent
    // create a real lobby so id 1 doesn't accidentally match below
    static_cast<void>(reg.create_lobby("x"));
    EXPECT_FALSE(reg.leave_lobby(1U, 999U));
}

// ---------------------------------------------------------------------------
// 5. Multiple lobbies coexist; active_lobbies span covers all
// ---------------------------------------------------------------------------
TEST(LobbyLifecycle, MultipleLobbiesCoexist)
{
    LobbyRegistry reg;
    const std::uint64_t id1 = reg.create_lobby("tdm");
    const std::uint64_t id2 = reg.create_lobby("ctf");
    const std::uint64_t id3 = reg.create_lobby("ffa");

    const auto all = reg.active_lobbies();
    EXPECT_EQ(all.size(), 3U);

    // IDs must be unique and monotonically increasing.
    EXPECT_LT(id1, id2);
    EXPECT_LT(id2, id3);
}

// ---------------------------------------------------------------------------
// 13. kStratified picks the lobby with the shortest haversine distance
// ---------------------------------------------------------------------------
TEST(SkillBasedFinder, StratifiedPicksClosestDistance)
{
    // Set up two lobbies with the same skill-band seed.
    // Candidate at London; lobby A seed at New York, lobby B seed at Frankfurt.
    LobbyRegistry reg;
    SkillBasedFinder finder;
    finder.configure(4, 500.0F,
                     /*w_skill*/   0.0F,
                     /*w_region*/  1.0F,
                     /*w_history*/ 0.0F,
                     FillStrategy::kStratified);

    const PlayerProfile ny  = make_profile(1, 1000.0F, kRegionNA,  40.7F, -74.1F);
    const PlayerProfile fra = make_profile(2, 1000.0F, kRegionNA,  50.1F,   8.7F);
    reg.register_player(ny);
    reg.register_player(fra);

    const std::uint64_t lid_a = reg.create_lobby("ranked");
    ASSERT_TRUE(reg.join_lobby(lid_a, ny.id));

    const std::uint64_t lid_b = reg.create_lobby("ranked");
    ASSERT_TRUE(reg.join_lobby(lid_b, fra.id));

    const PlayerProfile london = make_profile(99, 1000.0F, kRegionNA, 51.5F, 0.0F);
    const auto result = finder.find_match(london, reg);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, lid_b)
        << "kStratified must pick Frankfurt (closer) over New York";
}

// ---------------------------------------------------------------------------
// 14. Negative: single player with no open lobbies yields nullopt
// ---------------------------------------------------------------------------
TEST(SkillBasedFinder, SinglePlayerNoMatch)
{
    LobbyRegistry reg;  // empty registry
    SkillBasedFinder finder;
    finder.configure(4, 200.0F);

    const PlayerProfile p = make_profile(1, 500.0F, kRegionEU);
    const auto result = finder.find_match(p, reg);
    EXPECT_FALSE(result.has_value())
        << "No open lobbies should yield nullopt";
}

// ---------------------------------------------------------------------------
// 15. A lobby at exactly target_lobby_size is not offered
// ---------------------------------------------------------------------------
TEST(SkillBasedFinder, ExactFillLobbyBecomesUnavailable)
{
    LobbyRegistry reg;
    SkillBasedFinder finder;
    finder.configure(2, 500.0F);  // target size = 2

    // Seed: one player already in the lobby — capacity 2 so it is still open.
    const PlayerProfile s = make_profile(1, 500.0F, kRegionEU);
    reg.register_player(s);
    const std::uint64_t lid = reg.create_lobby("ranked");
    ASSERT_TRUE(reg.join_lobby(lid, s.id));

    // Second player fills the lobby to capacity.
    const PlayerProfile filler = make_profile(2, 500.0F, kRegionEU);
    reg.register_player(filler);
    ASSERT_TRUE(reg.join_lobby(lid, filler.id));
    // Mark it full (closed) as a host would do.
    reg.close_lobby(lid);

    // A third player must get nullopt — no open lobby available.
    const PlayerProfile third = make_profile(3, 500.0F, kRegionEU);
    const auto result = finder.find_match(third, reg);
    EXPECT_FALSE(result.has_value())
        << "Closed (full) lobby must not be offered to a third player";
}

// ---------------------------------------------------------------------------
// 18. SkillScorer: history pair key is order-independent (symmetric)
// ---------------------------------------------------------------------------
TEST(SkillScorer, HistoryPairKeySymmetric)
{
    SkillScorer scorer;
    scorer.set_weights(0.0F, 0.0F, 1.0F);

    const PlayerProfile a = make_profile(10, 500.0F, kRegionEU);
    const PlayerProfile b = make_profile(20, 500.0F, kRegionEU);

    // Add in reverse order — should still penalise.
    scorer.add_history_pair(b.id, a.id);

    const float score = scorer.score_pair(a, b);
    EXPECT_NEAR(score, 1.0F, 1e-5F)
        << "History penalty must apply regardless of argument order";
}

// ---------------------------------------------------------------------------
// 19. All weights zero → composite score is always zero
// ---------------------------------------------------------------------------
TEST(SkillScorer, ZeroWeightsAlwaysZeroScore)
{
    SkillScorer scorer;
    scorer.set_weights(0.0F, 0.0F, 0.0F);
    scorer.set_max_skill_range(1000.0F);

    const PlayerProfile a = make_profile(1, 0.0F,   kRegionEU, 0.0F,  0.0F);
    const PlayerProfile b = make_profile(2, 999.0F, kRegionNA, 90.0F, 0.0F);
    scorer.add_history_pair(a.id, b.id);

    EXPECT_NEAR(scorer.score_pair(a, b), 0.0F, 1e-6F);
}

// ---------------------------------------------------------------------------
// 20. Haversine antipodal distance ≈ kMaxDistanceKm
// ---------------------------------------------------------------------------
TEST(SkillScorer, MaxDistanceNormalisedToOne)
{
    // North pole (90N, 0E) vs south pole (90S, 0E) ≈ half-circumference.
    const float dist = SkillScorer::haversine_km(90.0F, 0.0F, -90.0F, 0.0F);
    // Should be close to π * 6371 ≈ 20015 km.
    EXPECT_NEAR(dist, 20015.0F, 5.0F);  // 5 km tolerance for float precision
}

// ---------------------------------------------------------------------------
// 21. Enqueue assigns unique, monotonically increasing ticket IDs
// ---------------------------------------------------------------------------
TEST(MatchmakingQueue, EnqueueReturnsUniqueIds)
{
    LobbyRegistry reg;
    MatchmakingQueue queue;

    MatchTicket t1; t1.player_id = 1U; t1.game_mode = "ranked";
    MatchTicket t2; t2.player_id = 2U; t2.game_mode = "ranked";
    MatchTicket t3; t3.player_id = 3U; t3.game_mode = "ranked";

    const auto id1 = queue.enqueue(t1, reg);
    const auto id2 = queue.enqueue(t2, reg);
    const auto id3 = queue.enqueue(t3, reg);

    ASSERT_TRUE(id1.has_value());
    ASSERT_TRUE(id2.has_value());
    ASSERT_TRUE(id3.has_value());

    EXPECT_NE(*id1, *id2);
    EXPECT_NE(*id2, *id3);
    EXPECT_LT(*id1, *id2);
    EXPECT_LT(*id2, *id3);
    EXPECT_EQ(queue.total_enqueued(), 3U);
    EXPECT_EQ(queue.pending_count(), 3U);
}

// ---------------------------------------------------------------------------
// 22. Negative: duplicate player_id is rejected while ticket is pending
// ---------------------------------------------------------------------------
TEST(MatchmakingQueue, DuplicatePlayerRejected)
{
    LobbyRegistry reg;
    MatchmakingQueue queue;

    MatchTicket t; t.player_id = 42U; t.game_mode = "ranked";
    const auto id1 = queue.enqueue(t, reg);
    ASSERT_TRUE(id1.has_value());

    // Same player — should be rejected.
    const auto id2 = queue.enqueue(t, reg);
    EXPECT_FALSE(id2.has_value())
        << "Duplicate enqueue for the same player must be rejected";
    EXPECT_EQ(queue.pending_count(), 1U);
}

// ---------------------------------------------------------------------------
// 23. Cancel a pending ticket transitions it to kCancelled
// ---------------------------------------------------------------------------
TEST(MatchmakingQueue, CancelPendingTicket)
{
    LobbyRegistry reg;
    MatchmakingQueue queue;

    MatchTicket t; t.player_id = 7U; t.game_mode = "tdm";
    const auto tid = queue.enqueue(t, reg);
    ASSERT_TRUE(tid.has_value());

    EXPECT_TRUE(queue.cancel(*tid));
    EXPECT_EQ(queue.pending_count(), 0U);

    const MatchTicket* status = queue.status_of(*tid);
    ASSERT_NE(status, nullptr);
    EXPECT_EQ(status->status, TicketStatus::kCancelled);

    // Same player may now re-enqueue.
    const auto tid2 = queue.enqueue(t, reg);
    EXPECT_TRUE(tid2.has_value())
        << "Player should be able to re-enqueue after cancellation";
}

// ---------------------------------------------------------------------------
// 24. Negative: cancel with unknown ticket_id returns false
// ---------------------------------------------------------------------------
TEST(MatchmakingQueue, CancelNonExistentReturnsFalse)
{
    LobbyRegistry reg;
    MatchmakingQueue queue;
    EXPECT_FALSE(queue.cancel(0xDEADBEEFULL));
    EXPECT_FALSE(queue.cancel(1U));
}

// ---------------------------------------------------------------------------
// 25. Negative: cancel on an already-matched ticket returns false
// ---------------------------------------------------------------------------
TEST(MatchmakingQueue, CancelAlreadyMatchedReturnsFalse)
{
    LobbyRegistry reg;
    MatchmakingQueue queue;
    queue.set_target_lobby_size(1);  // single-player lobby so it matches immediately

    MatchTicket t; t.player_id = 5U; t.game_mode = "ffa";
    const auto tid = queue.enqueue(t, reg);
    ASSERT_TRUE(tid.has_value());

    static_cast<void>(queue.run_cycle(reg, "ffa"));

    const MatchTicket* status = queue.status_of(*tid);
    ASSERT_NE(status, nullptr);
    EXPECT_EQ(status->status, TicketStatus::kMatched);

    EXPECT_FALSE(queue.cancel(*tid))
        << "Cannot cancel an already-matched ticket";
}

// ---------------------------------------------------------------------------
// 26. Negative: status_of with unknown id returns nullptr
// ---------------------------------------------------------------------------
TEST(MatchmakingQueue, StatusOfUnknownIdReturnsNull)
{
    LobbyRegistry reg;
    MatchmakingQueue queue;
    EXPECT_EQ(queue.status_of(9999U), nullptr);
}

// ---------------------------------------------------------------------------
// 27. Empty queue: run_cycle returns all-zero MatchResult
// ---------------------------------------------------------------------------
TEST(MatchmakingQueue, EmptyQueueRunCycleReturnsZeros)
{
    LobbyRegistry reg;
    MatchmakingQueue queue;
    const auto result = queue.run_cycle(reg, "ranked");

    EXPECT_EQ(result.tickets_matched, 0U);
    EXPECT_EQ(result.tickets_expired, 0U);
    EXPECT_EQ(result.tickets_pending, 0U);
    EXPECT_EQ(result.lobbies_created, 0U);
}

// ---------------------------------------------------------------------------
// 28. Exact fill: target_lobby_size players become one sealed lobby
// ---------------------------------------------------------------------------
TEST(MatchmakingQueue, ExactFillMatchSingleLobby)
{
    LobbyRegistry reg;
    MatchmakingQueue queue;
    queue.set_target_lobby_size(3);

    // Register profiles so skill/region gates can be evaluated.
    for (std::uint64_t i = 1; i <= 3; ++i)
    {
        reg.register_player(make_profile(i, 500.0F, kRegionEU));
        MatchTicket t;
        t.player_id    = i;
        t.game_mode    = "tdm";
        t.skill_window = 50.0F;
        static_cast<void>(queue.enqueue(t, reg));
    }

    const auto result = queue.run_cycle(reg, "tdm");

    EXPECT_EQ(result.tickets_matched, 3U);
    EXPECT_EQ(result.tickets_pending, 0U);
    EXPECT_EQ(result.lobbies_created, 1U);

    // The sealed lobby must be closed.
    const auto lobbies = reg.active_lobbies();
    ASSERT_EQ(lobbies.size(), 1U);
    EXPECT_FALSE(lobbies[0].is_open) << "Filled lobby must be closed";
    EXPECT_EQ(lobbies[0].player_ids.size(), 3U);
}

// ---------------------------------------------------------------------------
// 29. Partial fill: tickets remain kPending until the group is full
// ---------------------------------------------------------------------------
TEST(MatchmakingQueue, PartialFillTicketsRemainPending)
{
    LobbyRegistry reg;
    MatchmakingQueue queue;
    queue.set_target_lobby_size(4);  // need 4 — only 2 available

    for (std::uint64_t i = 1; i <= 2; ++i)
    {
        reg.register_player(make_profile(i, 500.0F, kRegionEU));
        MatchTicket t;
        t.player_id    = i;
        t.game_mode    = "ranked";
        t.skill_window = 50.0F;
        static_cast<void>(queue.enqueue(t, reg));
    }

    const auto result = queue.run_cycle(reg, "ranked");

    // Only full groups are sealed; 2 < target(4) so no lobby, no matches.
    EXPECT_EQ(result.tickets_matched, 0U);
    EXPECT_EQ(result.lobbies_created, 0U);
    EXPECT_EQ(result.tickets_pending, 2U);

    // No lobbies created in registry.
    EXPECT_TRUE(reg.active_lobbies().empty());
}

// ---------------------------------------------------------------------------
// 30. Skill window gates incompatible players — both remain kPending
// ---------------------------------------------------------------------------
TEST(MatchmakingQueue, SkillWindowGatesIncompatiblePlayers)
{
    LobbyRegistry reg;
    MatchmakingQueue queue;
    queue.set_target_lobby_size(2);

    // Player A: skill 500, tight window 20.
    reg.register_player(make_profile(1, 500.0F, kRegionEU));
    MatchTicket ta; ta.player_id = 1U; ta.skill_window = 20.0F;
    const auto tid_a = queue.enqueue(ta, reg);
    ASSERT_TRUE(tid_a.has_value());

    // Player B: skill 600, tight window 20 — delta=100 > 20.
    reg.register_player(make_profile(2, 600.0F, kRegionEU));
    MatchTicket tb; tb.player_id = 2U; tb.skill_window = 20.0F;
    const auto tid_b = queue.enqueue(tb, reg);
    ASSERT_TRUE(tid_b.has_value());

    const auto result = queue.run_cycle(reg, "ranked");

    // Incompatible — no full group formed, no lobby created, both still pending.
    EXPECT_EQ(result.tickets_matched, 0U);
    EXPECT_EQ(result.lobbies_created, 0U);
    EXPECT_EQ(result.tickets_pending, 2U);

    EXPECT_EQ(queue.status_of(*tid_a)->status, TicketStatus::kPending);
    EXPECT_EQ(queue.status_of(*tid_b)->status, TicketStatus::kPending);
    EXPECT_TRUE(reg.active_lobbies().empty());
}

// ---------------------------------------------------------------------------
// 31. Skill window widening allows a match that would otherwise fail
// ---------------------------------------------------------------------------
TEST(MatchmakingQueue, SkillWindowWideningAllowsEventualMatch)
{
    LobbyRegistry reg;
    MatchmakingQueue queue;
    queue.set_target_lobby_size(2);

    // Player A: skill 500, initial window 10, widens 20/tick, no cap.
    reg.register_player(make_profile(1, 500.0F, kRegionEU));
    MatchTicket ta;
    ta.player_id      = 1U;
    ta.skill_window   = 10.0F;
    ta.widen_per_tick = 20.0F;
    static_cast<void>(queue.enqueue(ta, reg));

    // Player B: skill 600, window 200 (generous).  Delta = 100.
    reg.register_player(make_profile(2, 600.0F, kRegionEU));
    MatchTicket tb;
    tb.player_id    = 2U;
    tb.skill_window = 200.0F;
    static_cast<void>(queue.enqueue(tb, reg));

    // Cycle 0 (tick 0): window_A = 10 < delta 100 → incompatible, no group formed.
    {
        const auto r = queue.run_cycle(reg, "ranked");
        EXPECT_EQ(r.tickets_matched, 0U)
            << "At tick 0 skill window 10 < delta 100 — should not match";
        EXPECT_EQ(r.lobbies_created, 0U);
        EXPECT_EQ(r.tickets_pending, 2U);
    }

    // Advance 5 ticks: window_A = 10 + 5*20 = 110 > 100.
    for (int tick = 0; tick < 5; ++tick)
        queue.advance_clock();

    // Cycle 1 (tick 5): window should now permit the match.
    {
        const auto r = queue.run_cycle(reg, "ranked");
        EXPECT_EQ(r.tickets_matched, 2U)
            << "After 5 ticks skill window 110 >= delta 100 — should match";
    }
}

// ---------------------------------------------------------------------------
// 32. Priority ordering: higher-priority player is grouped first
// ---------------------------------------------------------------------------
TEST(MatchmakingQueue, PriorityOrderingRespected)
{
    // Three players: skill all 500, target size 2.
    // Players A (prio 0), B (prio 10), C (prio 0).
    // Expected: B (highest prio) pairs with the next enqueued prio-0 ticket.
    // In sorted order: B first, then A, then C.
    // B+A form a group; C is leftover.
    LobbyRegistry reg;
    MatchmakingQueue queue;
    queue.set_target_lobby_size(2);

    reg.register_player(make_profile(1, 500.0F, kRegionEU));
    reg.register_player(make_profile(2, 500.0F, kRegionEU));
    reg.register_player(make_profile(3, 500.0F, kRegionEU));

    MatchTicket tA; tA.player_id = 1U; tA.priority = 0U;  tA.skill_window = 200.0F;
    MatchTicket tB; tB.player_id = 2U; tB.priority = 10U; tB.skill_window = 200.0F;
    MatchTicket tC; tC.player_id = 3U; tC.priority = 0U;  tC.skill_window = 200.0F;

    const auto tid_a = queue.enqueue(tA, reg);
    const auto tid_b = queue.enqueue(tB, reg);
    const auto tid_c = queue.enqueue(tC, reg);
    ASSERT_TRUE(tid_a.has_value());
    ASSERT_TRUE(tid_b.has_value());
    ASSERT_TRUE(tid_c.has_value());

    const auto result = queue.run_cycle(reg, "ranked");

    EXPECT_EQ(result.tickets_matched, 2U);
    EXPECT_EQ(result.tickets_pending, 1U);

    // B must be matched (highest priority).
    const MatchTicket* status_b = queue.status_of(*tid_b);
    ASSERT_NE(status_b, nullptr);
    EXPECT_EQ(status_b->status, TicketStatus::kMatched)
        << "Highest-priority player B must be matched first";
}

// ---------------------------------------------------------------------------
// 33. Fairness: equal-priority FIFO — earliest enqueue wins
// ---------------------------------------------------------------------------
TEST(MatchmakingQueue, FairnessFirstInFirstOut)
{
    // Three players, same priority, target 2.
    // A enqueued first → A+B pair; C is leftover.
    LobbyRegistry reg;
    MatchmakingQueue queue;
    queue.set_target_lobby_size(2);

    reg.register_player(make_profile(1, 500.0F, kRegionEU));
    reg.register_player(make_profile(2, 500.0F, kRegionEU));
    reg.register_player(make_profile(3, 500.0F, kRegionEU));

    MatchTicket tA; tA.player_id = 1U; tA.skill_window = 200.0F;
    MatchTicket tB; tB.player_id = 2U; tB.skill_window = 200.0F;
    MatchTicket tC; tC.player_id = 3U; tC.skill_window = 200.0F;

    const auto tid_a = queue.enqueue(tA, reg);
    const auto tid_b = queue.enqueue(tB, reg);
    const auto tid_c = queue.enqueue(tC, reg);
    ASSERT_TRUE(tid_a.has_value());
    ASSERT_TRUE(tid_b.has_value());
    ASSERT_TRUE(tid_c.has_value());

    const auto result = queue.run_cycle(reg, "ranked");
    EXPECT_EQ(result.tickets_matched, 2U);
    EXPECT_EQ(result.tickets_pending, 1U);

    // A was enqueued first — it must be in the matched pair.
    const MatchTicket* status_a = queue.status_of(*tid_a);
    ASSERT_NE(status_a, nullptr);
    EXPECT_EQ(status_a->status, TicketStatus::kMatched)
        << "FIFO: earliest-enqueued player A must be matched";

    // C (third) should still be pending.
    const MatchTicket* status_c = queue.status_of(*tid_c);
    ASSERT_NE(status_c, nullptr);
    EXPECT_EQ(status_c->status, TicketStatus::kPending)
        << "Third player C must remain pending";
}

// ---------------------------------------------------------------------------
// 34. Expiry: tickets older than max_wait_ticks are marked kExpired
// ---------------------------------------------------------------------------
TEST(MatchmakingQueue, ExpiryRemovesOldTickets)
{
    LobbyRegistry reg;
    MatchmakingQueue queue;
    queue.set_target_lobby_size(4);  // hard to fill — forces expiry
    queue.set_max_wait_ticks(3);

    reg.register_player(make_profile(1, 500.0F, kRegionEU));
    MatchTicket t; t.player_id = 1U; t.skill_window = 50.0F;
    const auto tid = queue.enqueue(t, reg);
    ASSERT_TRUE(tid.has_value());

    // Advance 4 ticks (> max_wait_ticks of 3).
    for (int i = 0; i < 4; ++i)
        queue.advance_clock();

    const auto result = queue.run_cycle(reg, "ranked");
    EXPECT_EQ(result.tickets_expired, 1U);
    EXPECT_EQ(result.tickets_matched, 0U);
    EXPECT_EQ(result.tickets_pending, 0U);

    const MatchTicket* status = queue.status_of(*tid);
    ASSERT_NE(status, nullptr);
    EXPECT_EQ(status->status, TicketStatus::kExpired);

    // Player can re-enqueue after expiry.
    const auto tid2 = queue.enqueue(t, reg);
    EXPECT_TRUE(tid2.has_value())
        << "Player must be able to re-enqueue after ticket expiry";
}

// ---------------------------------------------------------------------------
// 35. Region constraint in queue: mismatched regions both remain kPending
// ---------------------------------------------------------------------------
TEST(MatchmakingQueue, RegionConstraintInQueue)
{
    LobbyRegistry reg;
    MatchmakingQueue queue;
    queue.set_target_lobby_size(2);

    // EU player
    reg.register_player(make_profile(1, 500.0F, kRegionEU));
    MatchTicket teu; teu.player_id = 1U; teu.skill_window = 200.0F;
    const auto tid_eu = queue.enqueue(teu, reg);
    ASSERT_TRUE(tid_eu.has_value());

    // NA player (same skill, different region)
    reg.register_player(make_profile(2, 500.0F, kRegionNA));
    MatchTicket tna; tna.player_id = 2U; tna.skill_window = 200.0F;
    const auto tid_na = queue.enqueue(tna, reg);
    ASSERT_TRUE(tid_na.has_value());

    const auto result = queue.run_cycle(reg, "ranked");

    // Incompatible regions — no full group forms; both tickets remain pending.
    EXPECT_EQ(result.tickets_matched, 0U);
    EXPECT_EQ(result.lobbies_created, 0U);
    EXPECT_EQ(result.tickets_pending, 2U);

    EXPECT_EQ(queue.status_of(*tid_eu)->status, TicketStatus::kPending);
    EXPECT_EQ(queue.status_of(*tid_na)->status, TicketStatus::kPending);
    EXPECT_TRUE(reg.active_lobbies().empty());
}
