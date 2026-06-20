// =============================================================================
// CHROMODYNAMIC — cd::network::lobby tests
// Phase 713 / Sprint W5A  +  depth-pass (100% coverage close-out)
//
// Tests: 8 (original) + 20 (depth pass) = 28
//
// Original (8):
//   1.  CreateRoom           — host added as first player, valid RoomId returned.
//   2.  JoinRoom             — second player joins successfully; duplicate rejected.
//   3.  JoinRoomPasscode     — correct passcode grants; wrong passcode rejects.
//   4.  LeaveRoom            — player leaves; others remain; unknown returns false.
//   5.  SetReady             — ready flag toggles; unknown player returns false.
//   6.  StartGame            — full 4-player flow: create → join → ready → start.
//   7.  StartGameNotReady    — start_game rejects when any player is not ready.
//   8.  CapacityGate         — join_room rejects when at max_players.
//
// Depth pass (20):
//   9.  HostMigrationOnLeave      — host leaving transfers host to next player.
//  10.  HostMigrationMultiLeave   — sequential host departures keep migrating.
//  11.  EmptyRoomAfterLastLeave   — last player leaving leaves empty room; host=0.
//  12.  DestroyEmptyRoom          — destroy_room removes it; non-empty rejects.
//  13.  DestroyRoomUnknown        — destroy_room on non-existent id returns false.
//  14.  DestroyRoomPreservesOther — other rooms remain intact after destroy.
//  15.  JoinAfterGameStarted      — join_room rejects once game_started=true.
//  16.  SetReadyAfterGameStarted  — set_ready rejects once game_started=true.
//  17.  StartGameNonMember        — start_game rejects if caller not in room.
//  18.  StartGameEmptyRoom        — start_game rejects an empty room.
//  19.  StartGameSinglePlayer     — solo host ready → start succeeds.
//  20.  ActiveRoomsSpan           — active_rooms returns all rooms (multi-room).
//  21.  MultipleRoomsIsolated     — operations on one room don't affect another.
//  22.  RoomIdMonotonic           — successive create_room yields unique ids.
//  23.  JoinPasscodePublicRoomAnyPass — public room admits with any passcode string.
//  24.  LeaveRoomUnknownRoom      — leave_room on bogus id returns false.
//  25.  HostFieldSetOnCreate      — RoomState.host_player_id matches creator.
//  26.  MaxPlayersOne             — max_players=1 admits only host; join rejected.
//  27.  ReadyThenUnready          — toggling ready back prevents start.
//  28.  DisplayNamePropagated     — PlayerState display_name survives join/room().
// =============================================================================
#include <cd/network/lobby/Lobby.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <string>

namespace
{

using cd::network::lobby::Lobby;
using cd::network::lobby::LobbyConfig;
using cd::network::lobby::PlayerState;
using cd::network::lobby::RoomId;
using cd::network::lobby::RoomState;
using cd::network::lobby::kInvalidRoomId;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

[[nodiscard]] LobbyConfig make_config(std::string name    = "room-1",
                                      std::string mode    = "deathmatch",
                                      uint8_t     cap     = 4,
                                      bool        pub     = true,
                                      std::string pass    = "")
{
    LobbyConfig cfg;
    cfg.lobby_name  = std::move(name);
    cfg.game_mode   = std::move(mode);
    cfg.max_players = cap;
    cfg.is_public   = pub;
    cfg.passcode    = std::move(pass);
    return cfg;
}

[[nodiscard]] PlayerState make_player(uint64_t id, std::string display = "Player", uint32_t team = 0)
{
    PlayerState ps;
    ps.player_id    = id;
    ps.display_name = std::move(display);
    ps.team         = team;
    return ps;
}

}  // namespace

// ---------------------------------------------------------------------------
// 1. CreateRoom — host added as first player, valid RoomId returned
// ---------------------------------------------------------------------------
TEST(LobbyCreate, CreateRoom)
{
    Lobby lobby;

    const RoomId rid = lobby.create_room(make_config(), /*host_player_id=*/1001U);
    EXPECT_NE(rid, kInvalidRoomId);

    const RoomState* rs = lobby.room(rid);
    ASSERT_NE(rs, nullptr);
    EXPECT_EQ(rs->room_id, rid);
    EXPECT_EQ(rs->config.game_mode, "deathmatch");
    EXPECT_FALSE(rs->game_started);
    ASSERT_EQ(rs->players.size(), 1U);
    EXPECT_EQ(rs->players[0].player_id, 1001U);
}

// ---------------------------------------------------------------------------
// 2. JoinRoom — second player joins successfully
// ---------------------------------------------------------------------------
TEST(LobbyJoin, JoinRoomSuccess)
{
    Lobby lobby;

    const RoomId rid = lobby.create_room(make_config(), 1001U);
    EXPECT_TRUE(lobby.join_room(rid, make_player(1002U, "Alice"), ""));

    const RoomState* rs = lobby.room(rid);
    ASSERT_NE(rs, nullptr);
    EXPECT_EQ(rs->players.size(), 2U);

    // Duplicate join must fail.
    EXPECT_FALSE(lobby.join_room(rid, make_player(1002U), ""));

    // Join non-existent room must fail.
    EXPECT_FALSE(lobby.join_room(kInvalidRoomId, make_player(9999U), ""));
}

// ---------------------------------------------------------------------------
// 3. JoinRoomPasscode — correct passcode grants entry; wrong rejects
// ---------------------------------------------------------------------------
TEST(LobbyJoin, PasscodeGate)
{
    Lobby lobby;

    const RoomId rid = lobby.create_room(make_config("secret-room", "tdm", 4, false, "hunter2"), 1001U);

    // Wrong passcode — rejected.
    EXPECT_FALSE(lobby.join_room(rid, make_player(1002U), "wrongpass"));

    // Correct passcode — accepted.
    EXPECT_TRUE(lobby.join_room(rid, make_player(1002U), "hunter2"));

    // Public room (no passcode) always admits.
    const RoomId pub_rid = lobby.create_room(make_config("open-room"), 2001U);
    EXPECT_TRUE(lobby.join_room(pub_rid, make_player(2002U), ""));
    EXPECT_TRUE(lobby.join_room(pub_rid, make_player(2003U), "anything-goes"));
}

// ---------------------------------------------------------------------------
// 4. LeaveRoom — player leaves; others remain; unknown returns false
// ---------------------------------------------------------------------------
TEST(LobbyLeave, LeaveRoom)
{
    Lobby lobby;

    const RoomId rid = lobby.create_room(make_config(), 1001U);
    ASSERT_TRUE(lobby.join_room(rid, make_player(1002U), ""));
    ASSERT_TRUE(lobby.join_room(rid, make_player(1003U), ""));

    EXPECT_TRUE(lobby.leave_room(rid, 1002U));

    const RoomState* rs = lobby.room(rid);
    ASSERT_NE(rs, nullptr);
    EXPECT_EQ(rs->players.size(), 2U);

    // Unknown player — returns false.
    EXPECT_FALSE(lobby.leave_room(rid, 9999U));

    // Unknown room — returns false.
    EXPECT_FALSE(lobby.leave_room(kInvalidRoomId, 1001U));
}

// ---------------------------------------------------------------------------
// 5. SetReady — flag toggles; unknown player returns false
// ---------------------------------------------------------------------------
TEST(LobbyReady, SetReady)
{
    Lobby lobby;

    const RoomId rid = lobby.create_room(make_config(), 1001U);
    ASSERT_TRUE(lobby.join_room(rid, make_player(1002U), ""));

    // Initially not ready.
    const RoomState* rs = lobby.room(rid);
    ASSERT_NE(rs, nullptr);
    EXPECT_FALSE(rs->players[0].is_ready);

    // Toggle ready.
    EXPECT_TRUE(lobby.set_ready(rid, 1001U, true));
    EXPECT_TRUE(lobby.room(rid)->players[0].is_ready);

    // Toggle back.
    EXPECT_TRUE(lobby.set_ready(rid, 1001U, false));
    EXPECT_FALSE(lobby.room(rid)->players[0].is_ready);

    // Unknown player — returns false.
    EXPECT_FALSE(lobby.set_ready(rid, 9999U, true));

    // Unknown room — returns false.
    EXPECT_FALSE(lobby.set_ready(kInvalidRoomId, 1001U, true));
}

// ---------------------------------------------------------------------------
// 6. StartGame — full 4-player lobby flow: create → join → ready → start
//    MOMENT: A multiplayer dev creates a 4-player room, players join,
//            hit ready, host starts game — the whole lobby lifecycle in
//            one library call sequence.
// ---------------------------------------------------------------------------
TEST(LobbyStart, FourPlayerFullFlow)
{
    Lobby lobby;

    constexpr uint64_t kHost = 1001U;
    constexpr uint64_t kP2   = 1002U;
    constexpr uint64_t kP3   = 1003U;
    constexpr uint64_t kP4   = 1004U;

    // 1. Host creates room.
    const RoomId rid = lobby.create_room(make_config("squad-room", "battle-royale", 4), kHost);
    ASSERT_NE(rid, kInvalidRoomId);

    // 2. Three more players join.
    ASSERT_TRUE(lobby.join_room(rid, make_player(kP2, "Bob"),   ""));
    ASSERT_TRUE(lobby.join_room(rid, make_player(kP3, "Carol"), ""));
    ASSERT_TRUE(lobby.join_room(rid, make_player(kP4, "Dan"),   ""));

    EXPECT_EQ(lobby.room(rid)->players.size(), 4U);

    // 3. All players mark ready.
    EXPECT_TRUE(lobby.set_ready(rid, kHost, true));
    EXPECT_TRUE(lobby.set_ready(rid, kP2,   true));
    EXPECT_TRUE(lobby.set_ready(rid, kP3,   true));
    EXPECT_TRUE(lobby.set_ready(rid, kP4,   true));

    // 4. Host starts the game.
    EXPECT_TRUE(lobby.start_game(rid, kHost));
    EXPECT_TRUE(lobby.room(rid)->game_started);

    // 5. Double-start is idempotent-but-rejected.
    EXPECT_FALSE(lobby.start_game(rid, kHost));
}

// ---------------------------------------------------------------------------
// 7. StartGameNotReady — start_game rejects if any player is not ready
// ---------------------------------------------------------------------------
TEST(LobbyStart, StartGameNotReady)
{
    Lobby lobby;

    const RoomId rid = lobby.create_room(make_config(), 1001U);
    ASSERT_TRUE(lobby.join_room(rid, make_player(1002U), ""));

    // Only host is ready.
    ASSERT_TRUE(lobby.set_ready(rid, 1001U, true));
    // Player 1002 is still not ready → start must fail.
    EXPECT_FALSE(lobby.start_game(rid, 1001U));
    EXPECT_FALSE(lobby.room(rid)->game_started);

    // Now everyone is ready.
    ASSERT_TRUE(lobby.set_ready(rid, 1002U, true));
    EXPECT_TRUE(lobby.start_game(rid, 1001U));
}

// ---------------------------------------------------------------------------
// 8. CapacityGate — join_room rejects when room is full
// ---------------------------------------------------------------------------
TEST(LobbyJoin, CapacityGate)
{
    Lobby lobby;

    // Create a 2-player room; host is already inside.
    const RoomId rid = lobby.create_room(make_config("mini", "1v1", 2), 1001U);
    EXPECT_TRUE(lobby.join_room(rid, make_player(1002U), ""));

    // Room is full (2/2) — third join must fail.
    EXPECT_FALSE(lobby.join_room(rid, make_player(1003U), ""));

    const RoomState* rs = lobby.room(rid);
    ASSERT_NE(rs, nullptr);
    EXPECT_EQ(rs->players.size(), 2U);
}

// ===========================================================================
// Depth pass — 20 additional tests
// ===========================================================================

// ---------------------------------------------------------------------------
// 9. HostMigrationOnLeave — host leaving promotes next player
// ---------------------------------------------------------------------------
TEST(LobbyHostMigration, HostLeavesPromotesNext)
{
    Lobby lobby;

    constexpr uint64_t kHost = 1001U;
    constexpr uint64_t kP2   = 1002U;
    constexpr uint64_t kP3   = 1003U;

    const RoomId rid = lobby.create_room(make_config(), kHost);
    ASSERT_TRUE(lobby.join_room(rid, make_player(kP2), ""));
    ASSERT_TRUE(lobby.join_room(rid, make_player(kP3), ""));

    // Verify initial host.
    EXPECT_EQ(lobby.room(rid)->host_player_id, kHost);

    // Host leaves.
    EXPECT_TRUE(lobby.leave_room(rid, kHost));

    const RoomState* rs = lobby.room(rid);
    ASSERT_NE(rs, nullptr);
    EXPECT_EQ(rs->players.size(), 2U);
    // New host is the first remaining player (kP2 was first after host).
    EXPECT_EQ(rs->host_player_id, kP2);
    // The old host is gone.
    const bool old_host_gone = std::ranges::none_of(rs->players,
        [](const PlayerState& p){ return p.player_id == kHost; });
    EXPECT_TRUE(old_host_gone);
}

// ---------------------------------------------------------------------------
// 10. HostMigrationMultiLeave — sequential host departures keep migrating
// ---------------------------------------------------------------------------
TEST(LobbyHostMigration, SequentialMigration)
{
    Lobby lobby;

    constexpr uint64_t kH  = 100U;
    constexpr uint64_t kP1 = 101U;
    constexpr uint64_t kP2 = 102U;

    const RoomId rid = lobby.create_room(make_config("chain", "ffa", 4), kH);
    ASSERT_TRUE(lobby.join_room(rid, make_player(kP1), ""));
    ASSERT_TRUE(lobby.join_room(rid, make_player(kP2), ""));

    // First migration: kH → kP1.
    ASSERT_TRUE(lobby.leave_room(rid, kH));
    EXPECT_EQ(lobby.room(rid)->host_player_id, kP1);

    // Second migration: kP1 → kP2.
    ASSERT_TRUE(lobby.leave_room(rid, kP1));
    EXPECT_EQ(lobby.room(rid)->host_player_id, kP2);

    // One player left; no further migration needed.
    EXPECT_EQ(lobby.room(rid)->players.size(), 1U);
}

// ---------------------------------------------------------------------------
// 11. EmptyRoomAfterLastLeave — last player leaves; room empty; host = 0
// ---------------------------------------------------------------------------
TEST(LobbyHostMigration, EmptyRoomHostReset)
{
    Lobby lobby;

    const RoomId rid = lobby.create_room(make_config(), 777U);
    EXPECT_EQ(lobby.room(rid)->host_player_id, 777U);

    EXPECT_TRUE(lobby.leave_room(rid, 777U));

    const RoomState* rs = lobby.room(rid);
    ASSERT_NE(rs, nullptr);
    EXPECT_TRUE(rs->players.empty());
    EXPECT_EQ(rs->host_player_id, 0U);

    // Room still exists (not destroyed) — caller must call destroy_room.
    EXPECT_EQ(lobby.active_rooms().size(), 1U);
}

// ---------------------------------------------------------------------------
// 12. DestroyEmptyRoom — destroy_room removes the room; non-empty rejects
// ---------------------------------------------------------------------------
TEST(LobbyDestroy, DestroyEmptyRoom)
{
    Lobby lobby;

    const RoomId rid = lobby.create_room(make_config(), 500U);
    ASSERT_EQ(lobby.active_rooms().size(), 1U);

    // Non-empty room must be rejected.
    EXPECT_FALSE(lobby.destroy_room(rid));
    EXPECT_EQ(lobby.active_rooms().size(), 1U);

    // Drain the room.
    ASSERT_TRUE(lobby.leave_room(rid, 500U));

    // Now destroy succeeds.
    EXPECT_TRUE(lobby.destroy_room(rid));
    EXPECT_EQ(lobby.active_rooms().size(), 0U);
    EXPECT_EQ(lobby.room(rid), nullptr);
}

// ---------------------------------------------------------------------------
// 13. DestroyRoomUnknown — destroy_room on missing id returns false
// ---------------------------------------------------------------------------
TEST(LobbyDestroy, DestroyUnknownRoom)
{
    Lobby lobby;
    EXPECT_FALSE(lobby.destroy_room(kInvalidRoomId));
    EXPECT_FALSE(lobby.destroy_room(9999U));
}

// ---------------------------------------------------------------------------
// 14. DestroyRoomPreservesOther — other rooms are unaffected by a destroy
// ---------------------------------------------------------------------------
TEST(LobbyDestroy, DestroyPreservesOtherRooms)
{
    Lobby lobby;

    const RoomId r1 = lobby.create_room(make_config("r1"), 1U);
    const RoomId r2 = lobby.create_room(make_config("r2"), 2U);
    const RoomId r3 = lobby.create_room(make_config("r3"), 3U);

    ASSERT_EQ(lobby.active_rooms().size(), 3U);

    // Empty r2 then destroy it.
    ASSERT_TRUE(lobby.leave_room(r2, 2U));
    ASSERT_TRUE(lobby.destroy_room(r2));

    EXPECT_EQ(lobby.active_rooms().size(), 2U);
    // r1 and r3 must still be accessible.
    EXPECT_NE(lobby.room(r1), nullptr);
    EXPECT_NE(lobby.room(r3), nullptr);
    EXPECT_EQ(lobby.room(r2), nullptr);
    // Their configs are intact.
    EXPECT_EQ(lobby.room(r1)->config.lobby_name, "r1");
    EXPECT_EQ(lobby.room(r3)->config.lobby_name, "r3");
}

// ---------------------------------------------------------------------------
// 15. JoinAfterGameStarted — join_room rejects once game is in progress
// ---------------------------------------------------------------------------
TEST(LobbyJoin, JoinAfterGameStarted)
{
    Lobby lobby;

    const RoomId rid = lobby.create_room(make_config(), 1001U);
    ASSERT_TRUE(lobby.set_ready(rid, 1001U, true));
    ASSERT_TRUE(lobby.start_game(rid, 1001U));
    ASSERT_TRUE(lobby.room(rid)->game_started);

    EXPECT_FALSE(lobby.join_room(rid, make_player(1002U), ""));
}

// ---------------------------------------------------------------------------
// 16. SetReadyAfterGameStarted — set_ready rejects once game started
// ---------------------------------------------------------------------------
TEST(LobbyReady, SetReadyAfterGameStarted)
{
    Lobby lobby;

    const RoomId rid = lobby.create_room(make_config(), 1001U);
    ASSERT_TRUE(lobby.set_ready(rid, 1001U, true));
    ASSERT_TRUE(lobby.start_game(rid, 1001U));

    EXPECT_FALSE(lobby.set_ready(rid, 1001U, false));
}

// ---------------------------------------------------------------------------
// 17. StartGameNonMember — start_game rejects if caller is not in the room
// ---------------------------------------------------------------------------
TEST(LobbyStart, StartGameNonMember)
{
    Lobby lobby;

    const RoomId rid = lobby.create_room(make_config(), 1001U);
    ASSERT_TRUE(lobby.set_ready(rid, 1001U, true));

    // Player 9999 is not in the room — must be rejected.
    EXPECT_FALSE(lobby.start_game(rid, 9999U));
    EXPECT_FALSE(lobby.room(rid)->game_started);

    // Actual host succeeds.
    EXPECT_TRUE(lobby.start_game(rid, 1001U));
}

// ---------------------------------------------------------------------------
// 18. StartGameEmptyRoom — start_game rejects an empty room
// ---------------------------------------------------------------------------
TEST(LobbyStart, StartGameEmptyRoom)
{
    Lobby lobby;

    const RoomId rid = lobby.create_room(make_config(), 1001U);
    ASSERT_TRUE(lobby.leave_room(rid, 1001U));

    // Room is empty; any player_id should return false (including 0).
    EXPECT_FALSE(lobby.start_game(rid, 1001U));
    EXPECT_FALSE(lobby.start_game(rid, 0U));
}

// ---------------------------------------------------------------------------
// 19. StartGameSinglePlayer — solo host ready → start succeeds
// ---------------------------------------------------------------------------
TEST(LobbyStart, SinglePlayerStart)
{
    Lobby lobby;

    const RoomId rid = lobby.create_room(make_config("solo", "practice", 1), 42U);
    ASSERT_TRUE(lobby.set_ready(rid, 42U, true));

    EXPECT_TRUE(lobby.start_game(rid, 42U));
    EXPECT_TRUE(lobby.room(rid)->game_started);
}

// ---------------------------------------------------------------------------
// 20. ActiveRoomsSpan — active_rooms returns ALL rooms (multi-room registry)
// ---------------------------------------------------------------------------
TEST(LobbyQuery, ActiveRoomsSpan)
{
    Lobby lobby;

    EXPECT_TRUE(lobby.active_rooms().empty());

    const RoomId r1 = lobby.create_room(make_config("r1"), 1U);
    EXPECT_EQ(lobby.active_rooms().size(), 1U);

    const RoomId r2 = lobby.create_room(make_config("r2"), 2U);
    const RoomId r3 = lobby.create_room(make_config("r3"), 3U);
    EXPECT_EQ(lobby.active_rooms().size(), 3U);

    // Span correctly reflects all three ids.
    const auto all = lobby.active_rooms();
    std::vector<RoomId> ids;
    ids.reserve(all.size());
    for (const auto& rs : all)
        ids.push_back(rs.room_id);

    EXPECT_NE(std::ranges::find(ids, r1), ids.end());
    EXPECT_NE(std::ranges::find(ids, r2), ids.end());
    EXPECT_NE(std::ranges::find(ids, r3), ids.end());
    (void)r3;
}

// ---------------------------------------------------------------------------
// 21. MultipleRoomsIsolated — operations on one room don't bleed into another
// ---------------------------------------------------------------------------
TEST(LobbyQuery, MultipleRoomsIsolated)
{
    Lobby lobby;

    const RoomId rA = lobby.create_room(make_config("A"), 10U);
    const RoomId rB = lobby.create_room(make_config("B"), 20U);

    ASSERT_TRUE(lobby.join_room(rA, make_player(11U), ""));
    ASSERT_TRUE(lobby.set_ready(rA, 10U, true));
    ASSERT_TRUE(lobby.set_ready(rA, 11U, true));
    ASSERT_TRUE(lobby.start_game(rA, 10U));

    // Room B must be completely unaffected.
    const RoomState* rsB = lobby.room(rB);
    ASSERT_NE(rsB, nullptr);
    EXPECT_FALSE(rsB->game_started);
    EXPECT_EQ(rsB->players.size(), 1U);
    EXPECT_EQ(rsB->host_player_id, 20U);
}

// ---------------------------------------------------------------------------
// 22. RoomIdMonotonic — successive rooms receive unique, non-zero ids
// ---------------------------------------------------------------------------
TEST(LobbyCreate, RoomIdMonotonic)
{
    Lobby lobby;

    constexpr int kCount = 5;
    std::vector<RoomId> ids;
    ids.reserve(static_cast<std::size_t>(kCount));

    for (int i = 0; i < kCount; ++i)
        ids.push_back(lobby.create_room(make_config(), static_cast<uint64_t>(i) + 1U));

    // All ids are non-zero and unique.
    for (const RoomId id : ids)
        EXPECT_NE(id, kInvalidRoomId);

    // Uniqueness: no two ids should compare equal.
    for (std::size_t x = 0; x < ids.size(); ++x)
        for (std::size_t y = x + 1U; y < ids.size(); ++y)
            EXPECT_NE(ids[x], ids[y]) << "Duplicate room id at indices " << x << " and " << y;
}

// ---------------------------------------------------------------------------
// 23. JoinPasscodePublicRoomAnyPass — public room admits any passcode string
// ---------------------------------------------------------------------------
TEST(LobbyJoin, PublicRoomAcceptsAnyPasscode)
{
    Lobby lobby;

    // Public room (no passcode configured).
    const RoomId rid = lobby.create_room(make_config("open"), 1001U);

    EXPECT_TRUE(lobby.join_room(rid, make_player(1002U), ""));
    EXPECT_TRUE(lobby.join_room(rid, make_player(1003U), "surprise-no-passcode"));
    EXPECT_TRUE(lobby.join_room(rid, make_player(1004U), "hunter2"));

    EXPECT_EQ(lobby.room(rid)->players.size(), 4U);
}

// ---------------------------------------------------------------------------
// 24. LeaveRoomUnknownRoom — leave_room on bogus id returns false immediately
// ---------------------------------------------------------------------------
TEST(LobbyLeave, UnknownRoomReturnsFalse)
{
    Lobby lobby;
    EXPECT_FALSE(lobby.leave_room(kInvalidRoomId, 1U));
    EXPECT_FALSE(lobby.leave_room(9999U, 1U));
}

// ---------------------------------------------------------------------------
// 25. HostFieldSetOnCreate — RoomState.host_player_id matches the creator
// ---------------------------------------------------------------------------
TEST(LobbyCreate, HostFieldSetOnCreate)
{
    Lobby lobby;

    constexpr uint64_t kCreator = 55555U;
    const RoomId rid = lobby.create_room(make_config(), kCreator);
    const RoomState* rs = lobby.room(rid);
    ASSERT_NE(rs, nullptr);
    EXPECT_EQ(rs->host_player_id, kCreator);
    EXPECT_EQ(rs->players.size(), 1U);
    EXPECT_EQ(rs->players.front().player_id, kCreator);
}

// ---------------------------------------------------------------------------
// 26. MaxPlayersOne — max_players=1 admits only the host; any join rejected
// ---------------------------------------------------------------------------
TEST(LobbyJoin, MaxPlayersOne)
{
    Lobby lobby;

    const RoomId rid = lobby.create_room(make_config("solo", "practice", 1), 1U);
    EXPECT_EQ(lobby.room(rid)->players.size(), 1U);

    // Second player must be refused — capacity 1 is already filled by host.
    EXPECT_FALSE(lobby.join_room(rid, make_player(2U), ""));
}

// ---------------------------------------------------------------------------
// 27. ReadyThenUnready — toggling ready back to false prevents game start
// ---------------------------------------------------------------------------
TEST(LobbyReady, ToggleReadyPreventsStart)
{
    Lobby lobby;

    const RoomId rid = lobby.create_room(make_config(), 1001U);
    ASSERT_TRUE(lobby.join_room(rid, make_player(1002U), ""));

    ASSERT_TRUE(lobby.set_ready(rid, 1001U, true));
    ASSERT_TRUE(lobby.set_ready(rid, 1002U, true));

    // Both ready — start would succeed, but we toggle one back first.
    ASSERT_TRUE(lobby.set_ready(rid, 1002U, false));

    EXPECT_FALSE(lobby.start_game(rid, 1001U));
    EXPECT_FALSE(lobby.room(rid)->game_started);

    // Re-ready → now it succeeds.
    ASSERT_TRUE(lobby.set_ready(rid, 1002U, true));
    EXPECT_TRUE(lobby.start_game(rid, 1001U));
}

// ---------------------------------------------------------------------------
// 28. DisplayNamePropagated — PlayerState.display_name survives join/room()
// ---------------------------------------------------------------------------
TEST(LobbyJoin, DisplayNamePropagated)
{
    Lobby lobby;

    const RoomId rid = lobby.create_room(make_config(), 1001U);
    ASSERT_TRUE(lobby.join_room(rid, make_player(1002U, "GloriousGamer"), ""));

    const RoomState* rs = lobby.room(rid);
    ASSERT_NE(rs, nullptr);
    ASSERT_EQ(rs->players.size(), 2U);

    const auto it = std::ranges::find_if(rs->players,
        [](const PlayerState& p){ return p.player_id == 1002U; });
    ASSERT_NE(it, rs->players.end());
    EXPECT_EQ(it->display_name, "GloriousGamer");
}
