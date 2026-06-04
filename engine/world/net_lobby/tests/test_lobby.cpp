// =============================================================================
// CHROMODYNAMIC — cd::network::lobby tests
// Phase 713 / Sprint W5A
//
// Tests: 8
//   1. CreateRoom         — host is added as first player, valid RoomId returned.
//   2. JoinRoom           — second player joins successfully.
//   3. JoinRoomPasscode   — correct passcode grants entry; wrong passcode rejects.
//   4. LeaveRoom          — player leaves; others remain; unknown player returns false.
//   5. SetReady           — ready flag toggles; unknown player returns false.
//   6. StartGame          — full 4-player lobby flow: create → join → ready → start.
//   7. StartGameNotReady  — start_game rejects if any player is not ready.
//   8. CapacityGate       — join_room rejects when room is at max_players.
// =============================================================================
#include <cd/network/lobby/Lobby.hpp>
#include <gtest/gtest.h>

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

    const RoomId rid = lobby.create_room(make_config(), /*host=*/1001U);
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
