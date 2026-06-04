// =============================================================================
// CHROMODYNAMIC — cd/network/lobby/Lobby.hpp
// Phase 713 / Sprint W5A — cd::network::lobby Sprint-1
//
// Room-state management for multiplayer sessions.
// Sibling to cd::net::matchmaker: matchmaker pairs players, lobby manages
// the meeting place where they gather before game start.
//
// Public types:
//   LobbyConfig  — room configuration (name, game mode, capacity, passcode).
//   PlayerState  — per-player snapshot inside a room (id, display name, ready
//                  flag, team assignment).
//   RoomState    — aggregate of config + player list + lifecycle flags.
//   RoomId       — opaque 64-bit room identifier.
//   Lobby        — central registry: create / join / leave / ready / start.
//
// Sprint-1: in-memory only. Network sync is Sprint-2.
//
// All operations are single-threaded (no internal locks). Callers that share
// a Lobby across threads must apply external synchronisation.
//
// SOTA notes:
//   Steam Lobbies, Xbox Party Sessions and Discord Activities all expose a
//   "room owner starts the game when everyone is ready" contract that mirrors
//   start_game() here. The passcode gate corresponds to Steam's lobby password
//   / Discord invite-only mode. Sprint-2 will wire real transport (WebSocket /
//   ENet / reliable-UDP) into the same API surface.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cd::network::lobby
{

// ---------------------------------------------------------------------------
// RoomId
// ---------------------------------------------------------------------------

/// Opaque 64-bit room identifier.
/// Value 0 is reserved as the "null / invalid" sentinel.
using RoomId = std::uint64_t;

inline constexpr RoomId kInvalidRoomId { 0 };

// ---------------------------------------------------------------------------
// LobbyConfig
// ---------------------------------------------------------------------------

/// Immutable configuration supplied at room creation time.
struct LobbyConfig
{
    std::string  lobby_name;
    std::string  game_mode;
    std::uint8_t max_players { 4 };
    bool         is_public   { true };
    std::string  passcode;   ///< Empty string means no passcode required.
};

// ---------------------------------------------------------------------------
// PlayerState
// ---------------------------------------------------------------------------

/// Mutable per-player snapshot inside a room.
struct PlayerState
{
    std::uint64_t player_id    { 0 };
    std::string   display_name;
    bool          is_ready     { false };
    std::uint32_t team         { 0 };
};

// ---------------------------------------------------------------------------
// RoomState
// ---------------------------------------------------------------------------

/// Full snapshot of an active room.
struct RoomState
{
    RoomId                   room_id      { kInvalidRoomId };
    LobbyConfig              config;
    std::vector<PlayerState> players;
    bool                     game_started { false };
    double                   created_at_ms{ 0.0 };
};

// ---------------------------------------------------------------------------
// Lobby
// ---------------------------------------------------------------------------

/// Central in-memory registry for multiplayer rooms.
///
/// Workflow (happy path):
///   1. Host calls create_room()      → receives a RoomId.
///   2. Other players call join_room() with the RoomId (+ passcode if set).
///   3. Each player calls set_ready() when they are ready.
///   4. Host calls start_game()       → returns true only when ALL players
///      are marked ready and the room has not already started.
///   5. Consumer code transitions to the in-game state; the RoomState remains
///      accessible via room() for scoreboard / reconnect purposes.
///
/// Error semantics: all mutating methods return bool. False means the operation
/// was rejected (bad RoomId, wrong passcode, player not found, not all ready,
/// etc.). Callers should surface these to the user rather than ignoring them.
class Lobby
{
public:
    Lobby() noexcept = default;
    ~Lobby() noexcept = default;

    Lobby(const Lobby&) = delete;
    Lobby& operator=(const Lobby&) = delete;
    Lobby(Lobby&&) noexcept = default;
    Lobby& operator=(Lobby&&) noexcept = default;

    // ---- Room lifecycle ----

    /// Create a new room with the given configuration.
    /// `host_player_id` is automatically added to the player list as the
    /// first player (ready flag = false, team = 0).
    /// Returns the assigned RoomId (never kInvalidRoomId).
    [[nodiscard]] RoomId create_room(const LobbyConfig& config,
                                     uint64_t           host_player_id);

    /// Add a player to an existing room.
    /// Returns false when:
    ///   - `room_id` does not exist.
    ///   - The room is already at capacity (player count >= max_players).
    ///   - The game has already started.
    ///   - `passcode` does not match the room's passcode (if one is set).
    ///   - `player.player_id` is already in the room.
    [[nodiscard]] bool join_room(RoomId             room_id,
                                 const PlayerState& player,
                                 std::string_view   passcode);

    /// Remove a player from a room.
    /// Returns false if the room or player is not found.
    /// If the departing player was the last member the room is retained but
    /// empty (Sprint-2 will add auto-destroy / host-migration policies).
    [[nodiscard]] bool leave_room(RoomId room_id, uint64_t player_id);

    /// Toggle the ready flag for a specific player inside a room.
    /// Returns false if the room or player is not found, or if the game has
    /// already started.
    [[nodiscard]] bool set_ready(RoomId   room_id,
                                 uint64_t player_id,
                                 bool     ready);

    /// Attempt to start the game for a room.
    /// Succeeds only when:
    ///   - `room_id` exists.
    ///   - `host_player_id` is a member of the room.
    ///   - The room has at least one player.
    ///   - All players have is_ready == true.
    ///   - game_started is not already true.
    /// On success sets game_started = true and returns true.
    [[nodiscard]] bool start_game(RoomId room_id, uint64_t host_player_id);

    // ---- Queries ----

    /// Return a pointer to the RoomState for `room_id`, or nullptr if not
    /// found. The pointer is valid until the next mutating call on this Lobby.
    [[nodiscard]] const RoomState* room(RoomId room_id) const noexcept;

    /// Span over ALL rooms (active and started).
    /// Valid until the next mutating call.
    [[nodiscard]] std::span<const RoomState> active_rooms() const noexcept;

private:
    std::vector<RoomState>                           m_rooms;
    std::unordered_map<RoomId, std::size_t>          m_index;  ///< room_id -> m_rooms index
    RoomId                                           m_next_id { 1 };

    [[nodiscard]] RoomState*       find_room(RoomId room_id) noexcept;
    [[nodiscard]] const RoomState* find_room(RoomId room_id) const noexcept;
    [[nodiscard]] PlayerState*     find_player(RoomState& rs, uint64_t player_id) noexcept;
};

}  // namespace cd::network::lobby
