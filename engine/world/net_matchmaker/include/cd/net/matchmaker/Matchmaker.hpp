// =============================================================================
// CHROMODYNAMIC — cd/net/matchmaker/Matchmaker.hpp
// Phase 563 / Sprint W5B  — cd::net::matchmaker Sprint-1
// Phase 784 / FINALE-E4   — SkillScorer + FillStrategy (real scoring)
// Phase 825 / FINALE-E5   — MatchTicket queue + skill-window widening
//
// In-memory matchmaking primitives. Network transport is Sprint-2.
//
// Public types:
//   PlayerProfile    — player identity + skill rating + region tag + lat/lon.
//   Lobby            — aggregate of player IDs + game mode metadata.
//   LobbyRegistry    — create / join / leave / close lobbies; optional profile
//                      store (register_player) for skill/region lookups.
//   SkillScorer      — stateless pair scorer: skill delta + haversine distance
//                      + history avoidance. Lower score = better match.
//   FillStrategy     — enum class controlling lobby-selection heuristic:
//                      kBalanced   — lowest composite score wins.
//                      kStratified — skill bracket must be close, then distance.
//                      kFastFill   — return the first qualifying lobby immediately.
//   SkillBasedFinder — find an existing open lobby using SkillScorer + strategy.
//   MatchTicket      — enqueued matchmaking request for a single player.
//   TicketStatus     — current disposition of a MatchTicket.
//   MatchResult      — outcome returned by MatchmakingQueue::run_cycle.
//   MatchmakingQueue — FIFO queue with skill-window widening on a logical clock.
//
// All operations are single-threaded (no internal locks). Callers that share
// a registry across threads must apply external synchronisation.
//
// SOTA notes:
//   Valve's Steam Matchmaking uses a skill-bracket + ping-region filter similar
//   to the region-tag approach here. Epic Online Services and Xbox Live both
//   expose a "session bucket" model; LobbyRegistry is the in-process analogue
//   before real session-service backends are wired in (Sprint-2).
//   Haversine distance follows the WGS-84 mean radius (6371 km).
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
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
/// `lat_deg` and `lon_deg` are WGS-84 decimal degrees used by SkillScorer for
/// haversine distance scoring. Defaults (0,0) are accepted and produce a
/// distance of 0 relative to other (0,0) players.
struct PlayerProfile
{
    std::uint64_t               id           { 0 };
    float                       skill_rating { 0.0F };
    std::array<std::uint8_t, 4> region       {};
    float                       lat_deg      { 0.0F };
    float                       lon_deg      { 0.0F };
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
// FillStrategy
// ---------------------------------------------------------------------------

/// Controls the lobby-selection heuristic used by SkillBasedFinder.
///
///   kBalanced   — Among all qualifying lobbies, the one with the lowest
///                 SkillScorer composite score wins (best overall quality).
///   kStratified — Skill bracket is the primary filter (must be within
///                 max_skill_delta); among passing lobbies, lowest haversine
///                 distance wins.  History avoidance is still applied.
///   kFastFill   — Return the first qualifying lobby found without scoring.
///                 Suitable for high-volume queues where latency > quality.
enum class FillStrategy : std::uint8_t
{
    kBalanced   = 0,
    kStratified = 1,
    kFastFill   = 2,
};

// ---------------------------------------------------------------------------
// SkillScorer
// ---------------------------------------------------------------------------

/// Stateless pair-scorer: lower score = better match.
///
/// Composite score = w_skill   * normalised_skill_delta
///                 + w_region  * haversine_km / kMaxDistanceKm
///                 + w_history * history_penalty(a, b)
///
/// All weights default to 1.0. They are multiplicative scalars; set a weight
/// to 0.0 to disable a factor entirely.
///
/// history_penalty(a, b) = 1.0 if a and b appear together in the history
/// set, 0.0 otherwise.  The caller populates the history set before scoring.
class SkillScorer
{
public:
    /// Maximum inter-player distance used to normalise haversine scores.
    /// 20 015 km ≈ half the Earth's circumference (antipodal distance).
    static constexpr float kMaxDistanceKm { 20015.0F };

    SkillScorer() noexcept = default;

    /// Set composite-score weights (must be >= 0).
    void set_weights(float w_skill, float w_region, float w_history) noexcept;

    /// Maximum skill delta used for normalisation (not a hard gate here;
    /// gating is the caller's responsibility).  Defaults to 1000.
    void set_max_skill_range(float range) noexcept;

    /// Register a pair that has already played together so the scorer can
    /// apply history avoidance.  Order does not matter.
    void add_history_pair(std::uint64_t player_a, std::uint64_t player_b);

    /// Compute the composite score for matching `a` against `b`.
    /// Lower = better match.
    [[nodiscard]] float score_pair(const PlayerProfile& a,
                                   const PlayerProfile& b) const noexcept;

    /// Haversine great-circle distance in km between two lat/lon points.
    [[nodiscard]] static float haversine_km(float lat1, float lon1,
                                            float lat2, float lon2) noexcept;

private:
    float m_w_skill       { 1.0F };
    float m_w_region      { 1.0F };
    float m_w_history     { 1.0F };
    float m_max_skill_rng { 1000.0F };

    // Encode ordered pair (min_id, max_id) as a single 64-bit key.
    [[nodiscard]] static std::uint64_t make_pair_key(std::uint64_t a,
                                                      std::uint64_t b) noexcept;

    std::unordered_set<std::uint64_t> m_history;
};

// ---------------------------------------------------------------------------
// SkillBasedFinder
// ---------------------------------------------------------------------------

/// Stateless helper that scans an existing LobbyRegistry and returns the
/// lobby_id of the best-fitting open lobby for a given PlayerProfile.
///
/// Hard gate criteria (all must hold regardless of FillStrategy):
///   1. Lobby is open.
///   2. Current player count < target_lobby_size.
///   3. For each player already in the lobby whose profile is registered:
///      |candidate.skill_rating - peer.skill_rating| <= max_skill_delta.
///   4. If the lobby has at least one player with a registered profile, the
///      candidate's region tag must equal that player's region tag.
///      An empty lobby (or a lobby whose members have no registered profiles)
///      has no region or skill constraint.
///
/// Among qualifying lobbies the winner is determined by FillStrategy:
///   kBalanced   — lowest SkillScorer composite score vs first peer.
///   kStratified — lowest haversine distance vs first peer.
///   kFastFill   — first qualifying lobby in iteration order.
class SkillBasedFinder
{
public:
    SkillBasedFinder() noexcept = default;

    /// Configure matching parameters.
    /// `target_lobby_size` — max players before a lobby is considered full.
    /// `max_skill_delta`   — maximum absolute difference in skill_rating.
    void configure(std::uint32_t target_lobby_size, float max_skill_delta) noexcept;

    /// Extended configuration with weighting and strategy.
    /// `w_skill`    — SkillScorer weight for skill-delta component.
    /// `w_region`   — SkillScorer weight for haversine-distance component.
    /// `w_history`  — SkillScorer weight for history-avoidance component.
    /// `strategy`   — lobby-selection heuristic (default kBalanced).
    void configure(std::uint32_t target_lobby_size,
                   float         max_skill_delta,
                   float         w_skill,
                   float         w_region,
                   float         w_history,
                   FillStrategy  strategy = FillStrategy::kBalanced) noexcept;

    /// Provide a SkillScorer with pre-populated history for this search pass.
    /// The scorer is referenced by pointer; lifetime must exceed find_match.
    /// Pass nullptr to use default (no history, equal weights) scoring.
    void set_scorer(const SkillScorer* scorer) noexcept;

    /// Search `registry` for a suitable lobby for `candidate`.
    /// Returns the lobby_id of the best match, or std::nullopt if none found.
    [[nodiscard]] std::optional<std::uint64_t>
    find_match(const PlayerProfile& candidate,
               const LobbyRegistry& registry) const noexcept;

private:
    std::uint32_t        m_target_size      { 4 };
    float                m_max_skill_delta  { 100.0F };
    float                m_w_skill          { 1.0F };
    float                m_w_region         { 1.0F };
    float                m_w_history        { 1.0F };
    FillStrategy         m_strategy         { FillStrategy::kBalanced };
    const SkillScorer*   m_scorer           { nullptr };
};

// ---------------------------------------------------------------------------
// MatchTicket
// ---------------------------------------------------------------------------

/// Disposition of a ticket inside MatchmakingQueue.
enum class TicketStatus : std::uint8_t
{
    kPending   = 0,  ///< In the queue waiting for a match.
    kMatched   = 1,  ///< Successfully matched; lobby_id is valid.
    kCancelled = 2,  ///< Cancelled by the player or host.
    kExpired   = 3,  ///< Wait-time exceeded the queue's max_wait_ticks.
};

/// A single matchmaking request issued by one player.
///
/// `ticket_id`        — monotonically increasing, assigned by MatchmakingQueue.
/// `enqueue_tick`     — logical clock tick at enqueue time (set by queue).
/// `skill_window`     — initial skill delta; widens by `widen_per_tick` each
///                      logical tick until it reaches `max_skill_window`.
/// `widen_per_tick`   — skill window expansion per advance_clock() call.
/// `max_skill_window` — ceiling for widening; 0 means no widening.
/// `priority`         — higher value = processed earlier in a cycle.
///                      Equal-priority tickets are FIFO by enqueue_tick.
struct MatchTicket
{
    std::uint64_t ticket_id        { 0 };
    std::uint64_t player_id        { 0 };
    std::string   game_mode;
    std::uint64_t enqueue_tick     { 0 };
    float         skill_window     { 100.0F };
    float         widen_per_tick   { 0.0F };
    float         max_skill_window { 0.0F };
    std::uint32_t priority         { 0 };
    TicketStatus  status           { TicketStatus::kPending };
    std::uint64_t lobby_id         { 0 };  ///< Valid when status == kMatched.
};

/// Result of a single run_cycle() call.
struct MatchResult
{
    std::uint32_t tickets_matched  { 0 };  ///< Number of tickets matched this cycle.
    std::uint32_t tickets_expired  { 0 };  ///< Tickets whose wait exceeded max_wait_ticks.
    std::uint32_t tickets_pending  { 0 };  ///< Still waiting for a match.
    std::uint32_t lobbies_created  { 0 };  ///< New lobbies opened this cycle.
};

// ---------------------------------------------------------------------------
// MatchmakingQueue
// ---------------------------------------------------------------------------

/// Priority-FIFO ticket queue with skill-window widening on a logical clock.
///
/// Design notes
/// ============
/// * **Logical clock** — `advance_clock()` increments an internal tick counter.
///   No real-time is used; callers drive progression (deterministic + testable).
/// * **Widening** — each tick the effective skill window for a ticket grows by
///   `ticket.widen_per_tick` until `ticket.max_skill_window` is reached.
/// * **Priority** — within a `run_cycle()` pass tickets are sorted descending
///   by `priority`, then ascending by `enqueue_tick` (FIFO tie-break).
/// * **Matching** — the queue creates lobbies itself via the supplied
///   LobbyRegistry; `target_lobby_size` players fill one lobby per cycle pass.
///   After a lobby reaches `target_lobby_size` it is closed immediately.
/// * **Duplicate guard** — enqueue() returns false if the player already has a
///   kPending ticket in the queue.
/// * **Partial groups** — groups smaller than target_lobby_size are NOT matched;
///   tickets stay kPending so skill-window widening can unlock them next cycle.
/// * **Expiry** — tickets older than `max_wait_ticks` logical ticks are marked
///   kExpired and removed during run_cycle().
///
/// Thread safety: same as LobbyRegistry — single-threaded; caller locks.
class MatchmakingQueue
{
public:
    MatchmakingQueue() noexcept = default;
    ~MatchmakingQueue() noexcept = default;
    MatchmakingQueue(const MatchmakingQueue&) = delete;
    MatchmakingQueue& operator=(const MatchmakingQueue&) = delete;
    MatchmakingQueue(MatchmakingQueue&&) noexcept = default;
    MatchmakingQueue& operator=(MatchmakingQueue&&) noexcept = default;

    // ---- Configuration ----

    /// Number of players required to seal a lobby (default 4).
    void set_target_lobby_size(std::uint32_t size) noexcept;

    /// Maximum logical ticks a ticket may wait before expiry (0 = never expire).
    void set_max_wait_ticks(std::uint64_t ticks) noexcept;

    // ---- Clock ----

    /// Advance the logical clock by one tick.
    /// After calling this, skill windows of all pending tickets widen.
    void advance_clock() noexcept;

    /// Return the current logical tick.
    [[nodiscard]] std::uint64_t current_tick() const noexcept;

    // ---- Ticket management ----

    /// Enqueue a new matchmaking request.
    ///
    /// Returns the assigned ticket_id on success, or std::nullopt if:
    ///   - player_id already has a kPending ticket (duplicate guard).
    ///   - registry is nullptr.
    ///
    /// The `skill_window`, `widen_per_tick`, `max_skill_window` and `priority`
    /// fields of `ticket` are read before insertion; `ticket_id` and
    /// `enqueue_tick` are set by the queue.
    [[nodiscard]] std::optional<std::uint64_t>
    enqueue(MatchTicket ticket, LobbyRegistry& registry);

    /// Cancel a pending ticket.
    /// Returns true if the ticket was kPending and is now kCancelled.
    /// Returns false if not found or already in a terminal state.
    [[nodiscard]] bool cancel(std::uint64_t ticket_id) noexcept;

    /// Look up the current status of a ticket.
    /// Returns nullptr if the ticket_id is unknown.
    [[nodiscard]] const MatchTicket* status_of(std::uint64_t ticket_id) const noexcept;

    // ---- Matching ----

    /// Run one matching cycle over all pending tickets.
    ///
    /// Algorithm:
    ///   1. Expire tickets exceeding max_wait_ticks.
    ///   2. Sort pending tickets by priority (desc) then enqueue_tick (asc).
    ///   3. Greedily group compatible tickets: two tickets are compatible if
    ///      their player profiles are registered in `registry` and
    ///      |skill_a - skill_b| <= min(eff_window_a, eff_window_b) and
    ///      region tags match.
    ///   4. When a group reaches target_lobby_size, create + seal the lobby.
    ///   5. Partial groups (< target_lobby_size) remain kPending; no lobby is
    ///      created.  Their skill windows widen each advance_clock() call.
    ///
    /// Returns a MatchResult summary.
    [[nodiscard]] MatchResult run_cycle(LobbyRegistry& registry,
                                        const std::string& game_mode);

    /// Total number of tickets ever enqueued (including cancelled/expired/matched).
    [[nodiscard]] std::size_t total_enqueued() const noexcept;

    /// Number of tickets currently in kPending state.
    [[nodiscard]] std::size_t pending_count() const noexcept;

private:
    std::vector<MatchTicket>                      m_tickets;
    std::unordered_map<std::uint64_t, std::size_t> m_id_index;  ///< ticket_id -> m_tickets index
    std::unordered_map<std::uint64_t, std::uint64_t> m_player_pending; ///< player_id -> ticket_id

    std::uint64_t m_next_ticket_id  { 1 };
    std::uint64_t m_current_tick    { 0 };
    std::uint32_t m_target_size     { 4 };
    std::uint64_t m_max_wait_ticks  { 0 };  ///< 0 = never expire.

    /// Effective skill window for a ticket at the current tick.
    [[nodiscard]] float effective_window(const MatchTicket& t) const noexcept;
};

}  // namespace cd::net::matchmaker
