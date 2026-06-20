// =============================================================================
// CHROMODYNAMIC — cd/net/matchmaker/Matchmaker.cpp
// Phase 563 / Sprint W5B  — cd::net::matchmaker Sprint-1 implementation
// Phase 784 / FINALE-E4   — SkillScorer + FillStrategy real scoring
// Phase 825 / FINALE-E5   — MatchmakingQueue + skill-window widening
// =============================================================================
#include <cd/net/matchmaker/Matchmaker.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>

namespace cd::net::matchmaker
{

// ---------------------------------------------------------------------------
// LobbyRegistry — private helpers
// ---------------------------------------------------------------------------

Lobby* LobbyRegistry::find_lobby(std::uint64_t lobby_id) noexcept
{
    for (auto& lobby : m_lobbies)
    {
        if (lobby.lobby_id == lobby_id)
            return &lobby;
    }
    return nullptr;
}

const Lobby* LobbyRegistry::find_lobby(std::uint64_t lobby_id) const noexcept
{
    for (const auto& lobby : m_lobbies)
    {
        if (lobby.lobby_id == lobby_id)
            return &lobby;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// LobbyRegistry — public API
// ---------------------------------------------------------------------------

void LobbyRegistry::register_player(PlayerProfile profile)
{
    const std::uint64_t id = profile.id;
    m_profiles.insert_or_assign(id, profile);  // PlayerProfile is trivially copyable; std::move is a no-op
}

const PlayerProfile* LobbyRegistry::profile_of(std::uint64_t player_id) const noexcept
{
    const auto it = m_profiles.find(player_id);
    return (it != m_profiles.end()) ? &it->second : nullptr;
}

std::uint64_t LobbyRegistry::create_lobby(std::string game_mode)
{
    const std::uint64_t id = m_next_id++;
    m_lobbies.push_back(Lobby { id, {}, std::move(game_mode), true });
    return id;
}

bool LobbyRegistry::join_lobby(std::uint64_t lobby_id, std::uint64_t player_id)
{
    Lobby* const lobby = find_lobby(lobby_id);
    if (lobby == nullptr || !lobby->is_open)
        return false;

    lobby->player_ids.push_back(player_id);
    return true;
}

bool LobbyRegistry::leave_lobby(std::uint64_t lobby_id, std::uint64_t player_id)
{
    Lobby* const lobby = find_lobby(lobby_id);
    if (lobby == nullptr)
        return false;

    const auto it = std::ranges::find(lobby->player_ids, player_id);
    if (it == lobby->player_ids.end())
        return false;

    lobby->player_ids.erase(it);
    return true;
}

void LobbyRegistry::close_lobby(std::uint64_t lobby_id)
{
    Lobby* const lobby = find_lobby(lobby_id);
    if (lobby != nullptr)
        lobby->is_open = false;
}

std::span<const Lobby> LobbyRegistry::active_lobbies() const noexcept
{
    return { m_lobbies.data(), m_lobbies.size() };
}

// ---------------------------------------------------------------------------
// SkillScorer
// ---------------------------------------------------------------------------

void SkillScorer::set_weights(float w_skill, float w_region, float w_history) noexcept
{
    m_w_skill   = w_skill;
    m_w_region  = w_region;
    m_w_history = w_history;
}

void SkillScorer::set_max_skill_range(float range) noexcept
{
    m_max_skill_rng = (range > 0.0F) ? range : 1.0F;
}

void SkillScorer::add_history_pair(std::uint64_t player_a, std::uint64_t player_b)
{
    m_history.insert(make_pair_key(player_a, player_b));
}

std::uint64_t SkillScorer::make_pair_key(std::uint64_t a, std::uint64_t b) noexcept
{
    // Encode as (lo32 | hi32<<32) with lo = min(a,b) masked to 32 bits.
    // For full 64-bit IDs we XOR-fold to 32 bits; collisions only affect
    // history avoidance accuracy, not correctness of the hard gates.
    const std::uint64_t lo = std::min(a, b);
    const std::uint64_t hi = std::max(a, b);
    // Fold each 64-bit id to 32 bits via XOR, then combine.
    auto lo32 = static_cast<std::uint32_t>(lo ^ (lo >> 32U));
    auto hi32 = static_cast<std::uint32_t>(hi ^ (hi >> 32U));
    return (static_cast<std::uint64_t>(hi32) << 32U) | static_cast<std::uint64_t>(lo32);
}

// WGS-84 mean radius.
static constexpr float kEarthRadiusKm { 6371.0F };

float SkillScorer::haversine_km(float lat1, float lon1,
                                float lat2, float lon2) noexcept
{
    constexpr float kDeg2Rad { std::numbers::pi_v<float> / 180.0F };

    const float phi1  = lat1 * kDeg2Rad;
    const float phi2  = lat2 * kDeg2Rad;
    const float dphi  = (lat2 - lat1) * kDeg2Rad;
    const float dlam  = (lon2 - lon1) * kDeg2Rad;

    const float sinDphi = std::sin(dphi * 0.5F);
    const float sinDlam = std::sin(dlam * 0.5F);

    const float hav = sinDphi * sinDphi
                    + std::cos(phi1) * std::cos(phi2) * sinDlam * sinDlam;

    return 2.0F * kEarthRadiusKm * std::asin(std::sqrt(hav));
}

float SkillScorer::score_pair(const PlayerProfile& a,
                              const PlayerProfile& b) const noexcept
{
    // --- Skill component ---
    const float raw_delta     = std::fabs(a.skill_rating - b.skill_rating);
    const float norm_skill    = raw_delta / m_max_skill_rng;  // [0, 1+]

    // --- Region / distance component ---
    const float dist_km       = haversine_km(a.lat_deg, a.lon_deg,
                                              b.lat_deg, b.lon_deg);
    const float norm_dist     = dist_km / kMaxDistanceKm;     // [0, 1]

    // --- History avoidance component ---
    const float hist_penalty  = m_history.contains(make_pair_key(a.id, b.id))
                                 ? 1.0F : 0.0F;

    return m_w_skill * norm_skill
         + m_w_region * norm_dist
         + m_w_history * hist_penalty;
}

// ---------------------------------------------------------------------------
// SkillBasedFinder
// ---------------------------------------------------------------------------

void SkillBasedFinder::configure(std::uint32_t target_lobby_size,
                                  float         max_skill_delta) noexcept
{
    m_target_size     = target_lobby_size;
    m_max_skill_delta = max_skill_delta;
}

void SkillBasedFinder::configure(std::uint32_t target_lobby_size,
                                  float         max_skill_delta,
                                  float         w_skill,
                                  float         w_region,
                                  float         w_history,
                                  FillStrategy  strategy) noexcept
{
    m_target_size     = target_lobby_size;
    m_max_skill_delta = max_skill_delta;
    m_w_skill         = w_skill;
    m_w_region        = w_region;
    m_w_history       = w_history;
    m_strategy        = strategy;
}

void SkillBasedFinder::set_scorer(const SkillScorer* scorer) noexcept
{
    m_scorer = scorer;
}

std::optional<std::uint64_t>
SkillBasedFinder::find_match(const PlayerProfile& candidate,
                              const LobbyRegistry& registry) const noexcept
{
    // Build a transient scorer if no external one is provided.
    SkillScorer default_scorer;
    default_scorer.set_weights(m_w_skill, m_w_region, m_w_history);
    const SkillScorer& scorer = (m_scorer != nullptr) ? *m_scorer : default_scorer;

    std::optional<std::uint64_t> best_id;
    float                        best_score  { std::numeric_limits<float>::max() };
    float                        best_dist   { std::numeric_limits<float>::max() };
    std::size_t                  best_count  { 0 };

    for (const Lobby& lobby : registry.active_lobbies())
    {
        if (!lobby.is_open)
            continue;

        // Capacity gate.
        if (lobby.player_ids.size() >= static_cast<std::size_t>(m_target_size))
            continue;

        // Hard gates: skill delta and region match against registered peers.
        bool fits { true };
        const PlayerProfile* first_peer { nullptr };
        for (const std::uint64_t pid : lobby.player_ids)
        {
            const PlayerProfile* const peer = registry.profile_of(pid);
            if (peer == nullptr)
                continue;

            const float delta = std::fabs(candidate.skill_rating - peer->skill_rating);
            if (delta > m_max_skill_delta)
            {
                fits = false;
                break;
            }

            if (candidate.region != peer->region)
            {
                fits = false;
                break;
            }

            if (first_peer == nullptr)
                first_peer = peer;
        }

        if (!fits)
            continue;

        // kFastFill — return immediately on first qualifying lobby.
        if (m_strategy == FillStrategy::kFastFill)
            return lobby.lobby_id;

        if (m_strategy == FillStrategy::kBalanced)
        {
            // Lowest composite score vs first profiled peer wins.
            const float score = (first_peer != nullptr)
                                 ? scorer.score_pair(candidate, *first_peer)
                                 : 0.0F;

            // Tie-break: prefer fuller lobby (pack-the-room).
            const std::size_t cnt = lobby.player_ids.size();
            if (!best_id.has_value()
                || score < best_score
                || (score == best_score && cnt > best_count))
            {
                best_id    = lobby.lobby_id;
                best_score = score;
                best_count = cnt;
            }
        }
        else  // kStratified — lowest haversine distance vs first peer
        {
            const float dist = (first_peer != nullptr)
                                ? SkillScorer::haversine_km(candidate.lat_deg,
                                                             candidate.lon_deg,
                                                             first_peer->lat_deg,
                                                             first_peer->lon_deg)
                                : 0.0F;

            if (!best_id.has_value() || dist < best_dist)
            {
                best_id   = lobby.lobby_id;
                best_dist = dist;
            }
        }
    }

    return best_id;
}

// ---------------------------------------------------------------------------
// MatchmakingQueue
// ---------------------------------------------------------------------------

void MatchmakingQueue::set_target_lobby_size(std::uint32_t size) noexcept
{
    m_target_size = (size > 0U) ? size : 1U;
}

void MatchmakingQueue::set_max_wait_ticks(std::uint64_t ticks) noexcept
{
    m_max_wait_ticks = ticks;
}

void MatchmakingQueue::advance_clock() noexcept
{
    ++m_current_tick;
}

std::uint64_t MatchmakingQueue::current_tick() const noexcept
{
    return m_current_tick;
}

float MatchmakingQueue::effective_window(const MatchTicket& t) const noexcept
{
    const std::uint64_t age = (m_current_tick >= t.enqueue_tick)
                              ? (m_current_tick - t.enqueue_tick)
                              : 0ULL;
    const float widened = t.skill_window + t.widen_per_tick * static_cast<float>(age);
    if (t.max_skill_window > 0.0F)
        return std::min(widened, t.max_skill_window);
    return widened;
}

std::optional<std::uint64_t>
MatchmakingQueue::enqueue(MatchTicket ticket, LobbyRegistry& /*registry*/)
{
    // Duplicate guard: reject if the player already has a kPending ticket.
    if (m_player_pending.contains(ticket.player_id))
        return std::nullopt;

    ticket.ticket_id    = m_next_ticket_id++;
    ticket.enqueue_tick = m_current_tick;
    ticket.status       = TicketStatus::kPending;
    ticket.lobby_id     = 0U;

    const std::uint64_t tid = ticket.ticket_id;
    m_player_pending[ticket.player_id] = tid;

    const std::size_t idx = m_tickets.size();
    m_tickets.emplace_back(std::move(ticket));
    m_id_index[tid] = idx;

    return tid;
}

bool MatchmakingQueue::cancel(std::uint64_t ticket_id) noexcept
{
    const auto it = m_id_index.find(ticket_id);
    if (it == m_id_index.end())
        return false;

    MatchTicket& t = m_tickets[it->second];
    if (t.status != TicketStatus::kPending)
        return false;

    t.status = TicketStatus::kCancelled;
    m_player_pending.erase(t.player_id);
    return true;
}

const MatchTicket* MatchmakingQueue::status_of(std::uint64_t ticket_id) const noexcept
{
    const auto it = m_id_index.find(ticket_id);
    if (it == m_id_index.end())
        return nullptr;
    return &m_tickets[it->second];
}

std::size_t MatchmakingQueue::total_enqueued() const noexcept
{
    return m_tickets.size();
}

std::size_t MatchmakingQueue::pending_count() const noexcept
{
    std::size_t count { 0 };
    for (const auto& t : m_tickets)
    {
        if (t.status == TicketStatus::kPending)
            ++count;
    }
    return count;
}

MatchResult MatchmakingQueue::run_cycle(LobbyRegistry& registry,
                                         const std::string& game_mode)
{
    MatchResult result;

    // --- Step 1: expire tickets that have waited too long. ---
    if (m_max_wait_ticks > 0U)
    {
        for (auto& t : m_tickets)
        {
            if (t.status != TicketStatus::kPending)
                continue;
            const std::uint64_t age = (m_current_tick >= t.enqueue_tick)
                                      ? (m_current_tick - t.enqueue_tick)
                                      : 0ULL;
            if (age > m_max_wait_ticks)
            {
                t.status = TicketStatus::kExpired;
                m_player_pending.erase(t.player_id);
                ++result.tickets_expired;
            }
        }
    }

    // --- Step 2: collect indices of pending tickets and sort. ---
    std::vector<std::size_t> pending_idx;
    pending_idx.reserve(m_tickets.size());
    for (std::size_t i = 0; i < m_tickets.size(); ++i)
    {
        if (m_tickets[i].status == TicketStatus::kPending)
            pending_idx.emplace_back(i);
    }

    // Sort: higher priority first, then FIFO by enqueue_tick.
    std::ranges::sort(pending_idx, [this](std::size_t a, std::size_t b) -> bool
    {
        const MatchTicket& ta = m_tickets[a];
        const MatchTicket& tb = m_tickets[b];
        if (ta.priority != tb.priority)
            return ta.priority > tb.priority;
        return ta.enqueue_tick < tb.enqueue_tick;
    });

    // --- Step 3: greedy grouping. ---
    // matched_in_cycle[i] = true when pending_idx[i] is already consumed.
    std::vector<bool> consumed(pending_idx.size(), false);

    for (std::size_t head = 0; head < pending_idx.size(); ++head)
    {
        if (consumed[head])
            continue;

        const MatchTicket& leader = m_tickets[pending_idx[head]];
        const PlayerProfile* leader_profile = registry.profile_of(leader.player_id);

        // Build a group starting from this leader.
        std::vector<std::size_t> group_ticket_indices; // indices into pending_idx
        group_ticket_indices.emplace_back(head);
        consumed[head] = true;

        for (std::size_t j = head + 1;
             j < pending_idx.size()
             && group_ticket_indices.size() < static_cast<std::size_t>(m_target_size);
             ++j)
        {
            if (consumed[j])
                continue;

            const MatchTicket& candidate = m_tickets[pending_idx[j]];
            const PlayerProfile* cand_profile = registry.profile_of(candidate.player_id);

            // If either profile is absent, no constraint — accept freely.
            bool compatible { true };
            if (leader_profile != nullptr && cand_profile != nullptr)
            {
                // Skill window: use the minimum effective window of the two.
                const float eff_lead = effective_window(leader);
                const float eff_cand = effective_window(candidate);
                const float window   = std::min(eff_lead, eff_cand);

                const float delta = std::fabs(leader_profile->skill_rating
                                              - cand_profile->skill_rating);
                if (delta > window)
                    compatible = false;

                if (compatible && leader_profile->region != cand_profile->region)
                    compatible = false;
            }

            if (compatible)
            {
                group_ticket_indices.emplace_back(j);
                consumed[j] = true;
            }
        }

        // --- Step 4: group ready — only seal + match when exactly full. ---
        // Partial groups leave tickets kPending so widening can help next cycle.
        const bool full_group =
            group_ticket_indices.size() >= static_cast<std::size_t>(m_target_size);

        if (full_group)
        {
            const std::uint64_t lid = registry.create_lobby(game_mode);
            ++result.lobbies_created;

            for (const std::size_t gi : group_ticket_indices)
            {
                MatchTicket& t = m_tickets[pending_idx[gi]];
                static_cast<void>(registry.join_lobby(lid, t.player_id));
                t.status   = TicketStatus::kMatched;
                t.lobby_id = lid;
                m_player_pending.erase(t.player_id);
                ++result.tickets_matched;
            }

            registry.close_lobby(lid);  // seal after all members have joined
        }
        else
        {
            // Un-consume all members so they remain kPending for the next cycle.
            for (const std::size_t gi : group_ticket_indices)
                consumed[gi] = false;
        }
    }

    // Count remaining pending tickets.
    result.tickets_pending = static_cast<std::uint32_t>(pending_count());

    return result;
}

}  // namespace cd::net::matchmaker
