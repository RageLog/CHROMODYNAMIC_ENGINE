// =============================================================================
// CHROMODYNAMIC — cd/net/matchmaker/Matchmaker.cpp
// Phase 563 / Sprint W5B — cd::net::matchmaker Sprint-1 implementation
// =============================================================================
#include <cd/net/matchmaker/Matchmaker.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>

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
// SkillBasedFinder
// ---------------------------------------------------------------------------

void SkillBasedFinder::configure(std::uint32_t target_lobby_size,
                                  float         max_skill_delta) noexcept
{
    m_target_size     = target_lobby_size;
    m_max_skill_delta = max_skill_delta;
}

std::optional<std::uint64_t>
SkillBasedFinder::find_match(const PlayerProfile& candidate,
                              const LobbyRegistry& registry) const noexcept
{
    std::optional<std::uint64_t> best_id;
    std::size_t                  best_count { 0 };

    for (const Lobby& lobby : registry.active_lobbies())
    {
        if (!lobby.is_open)
            continue;

        // Capacity gate.
        if (lobby.player_ids.size() >= static_cast<std::size_t>(m_target_size))
            continue;

        // Skill and region checks against registered players in the lobby.
        bool fits { true };
        for (const std::uint64_t pid : lobby.player_ids)
        {
            const PlayerProfile* const peer = registry.profile_of(pid);
            if (peer == nullptr)
                continue;  // no profile data — skip constraint for this peer

            // Skill check.
            const float delta = std::fabs(candidate.skill_rating - peer->skill_rating);
            if (delta > m_max_skill_delta)
            {
                fits = false;
                break;
            }

            // Region check: candidate must match the first profiled peer's region.
            if (candidate.region != peer->region)
            {
                fits = false;
                break;
            }
        }

        if (!fits)
            continue;

        // Prefer the lobby with the most existing players (pack-the-room).
        const std::size_t cnt = lobby.player_ids.size();
        if (!best_id.has_value() || cnt > best_count)
        {
            best_id    = lobby.lobby_id;
            best_count = cnt;
        }
    }

    return best_id;
}

}  // namespace cd::net::matchmaker
