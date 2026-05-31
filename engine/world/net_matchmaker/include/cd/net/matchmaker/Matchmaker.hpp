// =============================================================================
// CHROMODYNAMIC — cd/net/matchmaker/Matchmaker.hpp
// Phase 563 / Sprint W5B — cd::net::matchmaker Sprint-1
//
// In-memory matchmaking primitives. Network transport is Sprint-2.
//
// Public types:
//   PlayerProfile   — player identity + skill rating + region tag.
//   Lobby           — aggregate of player IDs + game mode metadata.
//   LobbyRegistry   — create / join / leave / close lobbies; optional profile
//                     store (register_player) for skill/region lookups.
//   SkillBasedFinder — find an existing open lobby within skill + region
//                      constraints using profiles stored in the registry.
//
// All operations are single-threaded (no internal locks). Callers that share
// a registry across threads must apply external synchronisation.
//
// SOTA notes:
//   Valve's Steam Matchmaking uses a skill-bracket + ping-region filter similar
//   to the region-tag approach here. Epic Online Services and Xbox Live both
//   expose a "session bucket" model; LobbyRegistry is the in-process analogue
//   before real session-service backends are wired in (Sprint-2).
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace cd::net::matchmaker
{

// ---------------------------------------------------------------------------
// PlayerProfile
// ---------------------------------------------------------------------------

/// Lightweight per-player descriptor.
/// `region` is a 4-byte opaque tag: convention is { continent, country_hi,
/// country_lo, datacenter } but the matchmaker treats it as an opaque blob —
/// equality implies same region bucket.
struct PlayerProfile
{
    std::uint64_t               id           { 0 };
    float                       skill_rating { 0.0F };
    std::array<std::uint8_t, 4> region       {};
};

// ---------------------------------------------------------------------------
// Lobby
// ---------------------------------------------------------------------------

/// A named game session collecting player IDs.
/// `is_open` is true while new players can join.
struct Lobby
{
    std::uint64_t              lobby_id  { 0 };
    std::vector<std::uint64_t> player_ids;
    std::string                game_mode;
    bool                       is_open   { true };
};

// ---------------------------------------------------------------------------
// LobbyRegistry
// ---------------------------------------------------------------------------

/// Central store for all active lobbies plus an optional in-memory profile
/// cache.  Call `register_player` before `join_lobby` so that
/// `SkillBasedFinder` can evaluate skill/region constraints.
///
/// Lobby IDs are monotonically increasing 64-bit integers assigned at
/// create_lobby time. IDs are never reused within a registry instance
/// lifetime.
class LobbyRegistry
{
public:
    LobbyRegistry() noexcept = default;
    ~LobbyRegistry() noexcept = default;
    LobbyRegistry(const LobbyRegistry&) = delete;
    LobbyRegistry& operator=(const LobbyRegistry&) = delete;
    LobbyRegistry(LobbyRegistry&&) noexcept = default;
    LobbyRegistry& operator=(LobbyRegistry&&) noexcept = default;

    // ---- Profile store (needed by SkillBasedFinder) ----

    /// Store a player profile so subsequent skill/region lookups can succeed.
    /// Overwrites any existing entry for the same id.
    void register_player(PlayerProfile profile);

    /// Look up a player profile by id. Returns nullptr if not found.
    [[nodiscard]] const PlayerProfile* profile_of(std::uint64_t player_id) const noexcept;

    // ---- Lobby management ----

    /// Create a new open lobby for the given game mode.
    /// Returns the assigned lobby_id.
    [[nodiscard]] std::uint64_t create_lobby(std::string game_mode);

    /// Add `player_id` to lobby `lobby_id`.
    /// Returns false if the lobby does not exist or is closed.
    [[nodiscard]] bool join_lobby(std::uint64_t lobby_id, std::uint64_t player_id);

    /// Remove `player_id` from lobby `lobby_id`.
    /// Returns false if the lobby or player was not found.
    [[nodiscard]] bool leave_lobby(std::uint64_t lobby_id, std::uint64_t player_id);

    /// Mark the lobby as closed; it will no longer appear in active_lobbies().
    void close_lobby(std::uint64_t lobby_id);

    /// Span over all lobbies (including closed ones).
    /// The caller can filter on `is_open`. The span is valid until the next
    /// mutating call on this registry.
    [[nodiscard]] std::span<const Lobby> active_lobbies() const noexcept;

private:
    std::vector<Lobby>                                   m_lobbies;
    std::unordered_map<std::uint64_t, PlayerProfile>     m_profiles;
    std::uint64_t                                        m_next_id { 1 };

    [[nodiscard]] Lobby* find_lobby(std::uint64_t lobby_id) noexcept;
    [[nodiscard]] const Lobby* find_lobby(std::uint64_t lobby_id) const noexcept;
};

// ---------------------------------------------------------------------------
// SkillBasedFinder
// ---------------------------------------------------------------------------

/// Stateless helper that scans an existing LobbyRegistry and returns the
/// lobby_id of the best-fitting open lobby for a given PlayerProfile.
///
/// Selection criteria (all must hold):
///   1. Lobby is open.
///   2. Current player count < target_lobby_size.
///   3. For each player already in the lobby whose profile is registered:
///      |candidate.skill_rating - peer.skill_rating| <= max_skill_delta.
///   4. If the lobby has at least one player with a registered profile, the
///      candidate's region tag must equal that player's region tag.
///      An empty lobby (or a lobby whose members have no registered profiles)
///      has no region or skill constraint.
///
/// Among qualifying lobbies the one with the highest current player count is
/// preferred (pack-the-room heuristic — minimises session startup latency).
class SkillBasedFinder
{
public:
    SkillBasedFinder() noexcept = default;

    /// Configure matching parameters.
    /// `target_lobby_size` — max players before a lobby is considered full.
    /// `max_skill_delta`   — maximum absolute difference in skill_rating.
    void configure(std::uint32_t target_lobby_size, float max_skill_delta) noexcept;

    /// Search `registry` for a suitable lobby for `candidate`.
    /// Returns the lobby_id of the best match, or std::nullopt if none found.
    [[nodiscard]] std::optional<std::uint64_t>
    find_match(const PlayerProfile& candidate,
               const LobbyRegistry& registry) const noexcept;

private:
    std::uint32_t m_target_size      { 4 };
    float         m_max_skill_delta  { 100.0F };
};

}  // namespace cd::net::matchmaker
