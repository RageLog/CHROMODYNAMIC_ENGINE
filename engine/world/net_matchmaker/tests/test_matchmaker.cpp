// =============================================================================
// CHROMODYNAMIC — cd::net::matchmaker tests
// Phase 563 / Sprint W5B
//
// Tests: 7
//   1. LobbyLifecycle — create + join + leave round-trip
//   2. FindMatchSkillWithinDelta — returns an open lobby when skill fits
//   3. FindMatchSkillOutsideDelta — returns nullopt when skill is too far
//   4. FindMatchRegionRespected — ignores lobby with mismatched region
//   5. CloseLobbyRemovedFromActive — close_lobby marks lobby as not open
//   6. FindMatchPrefersMostPopulated — pack-the-room heuristic
//   7. FindMatchEmptyLobbyNoConstraints — empty lobby accepted regardless of skill
// =============================================================================
#include <cd/net/matchmaker/Matchmaker.hpp>
#include <gtest/gtest.h>

#include <array>
#include <cstdint>

namespace
{

using cd::net::matchmaker::Lobby;
using cd::net::matchmaker::LobbyRegistry;
using cd::net::matchmaker::PlayerProfile;
using cd::net::matchmaker::SkillBasedFinder;

// ---------------------------------------------------------------------------
// Helper factories
// ---------------------------------------------------------------------------

[[nodiscard]] PlayerProfile make_profile(std::uint64_t               id,
                                          float                       skill,
                                          std::array<std::uint8_t, 4> region)
{
    PlayerProfile p;
    p.id           = id;
    p.skill_rating = skill;
    p.region       = region;
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
