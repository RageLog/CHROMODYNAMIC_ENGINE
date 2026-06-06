// =============================================================================
// CHROMODYNAMIC — cd/net/matchmaker/Matchmaker.cpp
// Phase 563 / Sprint W5B  — cd::net::matchmaker Sprint-1 implementation
// Phase 784 / FINALE-E4   — SkillScorer + FillStrategy real scoring
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
    m_profiles.insert_or_assign(id, std::move(profile));
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

    const auto it = std::find(lobby->player_ids.begin(),
                              lobby->player_ids.end(),
                              player_id);
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
    const float hist_penalty  = m_history.count(make_pair_key(a.id, b.id)) > 0U
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

}  // namespace cd::net::matchmaker
