// =============================================================================
// CHROMODYNAMIC — cd/network/lobby/Lobby.cpp
// Phase 713 / Sprint W5A — cd::network::lobby Sprint-1 implementation
// =============================================================================
#include <cd/network/lobby/Lobby.hpp>

#include <algorithm>
#include <cstddef>

namespace cd::network::lobby
{

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

RoomState* Lobby::find_room(RoomId room_id) noexcept
{
    const auto it = m_index_.find(room_id);
    if (it == m_index_.end())
        return nullptr;
    return &m_rooms_[it->second];
}

const RoomState* Lobby::find_room(RoomId room_id) const noexcept
{
    const auto it = m_index_.find(room_id);
    if (it == m_index_.end())
        return nullptr;
    return &m_rooms_[it->second];
}

PlayerState* Lobby::find_player(RoomState& rs, uint64_t player_id) noexcept
{
    for (auto& p : rs.players)
    {
        if (p.player_id == player_id)
            return &p;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

RoomId Lobby::create_room(const LobbyConfig& config, uint64_t host_player_id)
{
    const RoomId id = m_next_id_++;

    RoomState rs;
    rs.room_id        = id;
    rs.config         = config;
    rs.host_player_id = host_player_id;

    // Add host as first player.
    PlayerState host;
    host.player_id = host_player_id;
    rs.players.push_back(host);

    const std::size_t idx = m_rooms_.size();
    m_rooms_.push_back(std::move(rs));
    m_index_.emplace(id, idx);

    return id;
}

bool Lobby::join_room(RoomId             room_id,
                      const PlayerState& player,
                      std::string_view   passcode)
{
    RoomState* const rs = find_room(room_id);
    if (rs == nullptr)
        return false;

    // Game already in progress.
    if (rs->game_started)
        return false;

    // Capacity gate.
    if (rs->players.size() >= static_cast<std::size_t>(rs->config.max_players))
        return false;

    // Passcode gate (only enforced when the room has a non-empty passcode).
    if (!rs->config.passcode.empty() && passcode != rs->config.passcode)
        return false;

    // Duplicate check.
    if (find_player(*rs, player.player_id) != nullptr)
        return false;

    rs->players.push_back(player);
    return true;
}

bool Lobby::leave_room(RoomId room_id, uint64_t player_id)
{
    RoomState* const rs = find_room(room_id);
    if (rs == nullptr)
        return false;

    auto& players = rs->players;
    const auto it = std::ranges::find_if(players,
                                         [player_id](const PlayerState& p)
                                         { return p.player_id == player_id; });
    if (it == players.end())
        return false;

    const bool leaving_is_host = (player_id == rs->host_player_id);
    players.erase(it);

    // Host-migration: if the host departed and others remain, promote the new
    // front player. If the room is now empty, clear host_player_id.
    if (leaving_is_host)
    {
        rs->host_player_id = players.empty() ? 0U : players.front().player_id;
    }

    return true;
}

bool Lobby::destroy_room(RoomId room_id)
{
    const auto idx_it = m_index_.find(room_id);
    if (idx_it == m_index_.end())
        return false;

    const std::size_t idx = idx_it->second;
    RoomState& rs = m_rooms_[idx];

    // Only allow destroying empty rooms.
    if (!rs.players.empty())
        return false;

    // Swap-erase to keep m_rooms_ contiguous; update the index of the
    // room that was moved into position idx (if any).
    const std::size_t last = m_rooms_.size() - 1U;
    if (idx != last)
    {
        const RoomId moved_id = m_rooms_[last].room_id;
        m_rooms_[idx] = std::move(m_rooms_[last]);
        m_index_[moved_id] = idx;
    }
    m_rooms_.pop_back();
    m_index_.erase(idx_it);
    return true;
}

bool Lobby::set_ready(RoomId room_id, uint64_t player_id, bool ready)
{
    RoomState* const rs = find_room(room_id);
    if (rs == nullptr)
        return false;

    if (rs->game_started)
        return false;

    PlayerState* const ps = find_player(*rs, player_id);
    if (ps == nullptr)
        return false;

    ps->is_ready = ready;
    return true;
}

bool Lobby::start_game(RoomId room_id, uint64_t host_player_id)
{
    RoomState* const rs = find_room(room_id);
    if (rs == nullptr)
        return false;

    // Already started.
    if (rs->game_started)
        return false;

    // Host must be a member.
    if (find_player(*rs, host_player_id) == nullptr)
        return false;

    // Need at least one player.
    if (rs->players.empty())
        return false;

    // All players must be ready.
    const bool all_ready = std::ranges::all_of(rs->players,
                                                [](const PlayerState& p)
                                                { return p.is_ready; });
    if (!all_ready)
        return false;

    rs->game_started = true;
    return true;
}

const RoomState* Lobby::room(RoomId room_id) const noexcept
{
    return find_room(room_id);
}

std::span<const RoomState> Lobby::active_rooms() const noexcept
{
    return { m_rooms_.data(), m_rooms_.size() };
}

}  // namespace cd::network::lobby
