// =============================================================================
// CHROMODYNAMIC — src/AiSquad.cpp
// Phase 682 — cd::ai::squad implementation
// =============================================================================
#include <cd/ai/squad/AiSquad.hpp>

#include <algorithm>
#include <cmath>
#include <ranges>
#include <stdexcept>

namespace cd::ai::squad
{

// =============================================================================
// Squad — membership management
// =============================================================================

void Squad::add_member(uint64_t entity_id, SquadRole role)
{
    // Capacity guard (Formation.member_count is uint8_t → max 255).
    if (members_.size() >= 255U)
    {
        return;
    }

    // Idempotent: ignore if already present.
    for (const auto& m : members_)
    {
        if (m.entity_id == entity_id)
        {
            return;
        }
    }

    SquadMember sm{};
    sm.entity_id = entity_id;
    sm.role      = role;
    sm.position  = {};
    sm.health    = 1.0F;
    members_.push_back(sm);
}

void Squad::remove_member(uint64_t entity_id)
{
    auto it = std::ranges::find_if(members_,
        [entity_id](const SquadMember& m) { return m.entity_id == entity_id; });
    if (it != members_.end())
    {
        members_.erase(it);
    }
}

// =============================================================================
// Squad — per-frame state feed
// =============================================================================

void Squad::update_position(uint64_t entity_id, std::array<float, 3> pos)
{
    for (auto& m : members_)
    {
        if (m.entity_id == entity_id)
        {
            m.position = pos;
            return;
        }
    }
}

void Squad::update_health(uint64_t entity_id, float health)
{
    for (auto& m : members_)
    {
        if (m.entity_id == entity_id)
        {
            m.health = std::clamp(health, 0.0F, 1.0F);
            return;
        }
    }
}

// =============================================================================
// Squad — blackboard writes
// =============================================================================

void Squad::set_target(uint64_t target_entity_id, std::array<float, 3> target_position)
{
    blackboard_.target_entity_id = target_entity_id;
    blackboard_.target_position  = target_position;
}

void Squad::raise_threat(float delta)
{
    blackboard_.threat_level = std::clamp(blackboard_.threat_level + delta, 0.0F, 1.0F);
}

void Squad::set_fact(const std::string& key, float value)
{
    blackboard_.shared_facts[key] = value;
}

// =============================================================================
// Squad — tick
// =============================================================================

void Squad::tick(float dt)
{
    // --- Formation update -----------------------------------------------------
    recompute_formation();

    // --- Threat decay ----------------------------------------------------------
    // Exponential decay: threat' = threat * exp(-kThreatDecayRate * dt)
    // Clamp to [0, 1] to avoid floating-point drift below zero.
    if (blackboard_.threat_level > 0.0F)
    {
        const float decay_factor = std::exp(-kThreatDecayRate * dt);
        blackboard_.threat_level = std::clamp(blackboard_.threat_level * decay_factor, 0.0F, 1.0F);
    }
}

// =============================================================================
// Squad — queries
// =============================================================================

const Formation& Squad::formation() const noexcept
{
    return formation_;
}

const Blackboard& Squad::blackboard() const noexcept
{
    return blackboard_;
}

std::span<const SquadMember> Squad::members() const noexcept
{
    return {members_.data(), members_.size()};
}

const SquadMember* Squad::find_member(uint64_t entity_id) const noexcept
{
    for (const auto& m : members_)
    {
        if (m.entity_id == entity_id)
        {
            return &m;
        }
    }
    return nullptr;
}

std::size_t Squad::size() const noexcept
{
    return members_.size();
}

bool Squad::empty() const noexcept
{
    return members_.empty();
}

// =============================================================================
// Squad — private helpers
// =============================================================================

void Squad::recompute_formation() noexcept
{
    const auto count = members_.size();
    if (count == 0U)
    {
        formation_ = {};
        return;
    }

    // Centroid — arithmetic mean of all member positions.
    float sum_x = 0.0F;
    float sum_y = 0.0F;
    float sum_z = 0.0F;
    for (const auto& m : members_)
    {
        sum_x += m.position[0];
        sum_y += m.position[1];
        sum_z += m.position[2];
    }
    const float inv_n = 1.0F / static_cast<float>(count);
    formation_.centroid[0] = sum_x * inv_n;
    formation_.centroid[1] = sum_y * inv_n;
    formation_.centroid[2] = sum_z * inv_n;

    // Radius — max Euclidean distance from centroid to any member.
    float max_dist_sq = 0.0F;
    for (const auto& m : members_)
    {
        const float dx = m.position[0] - formation_.centroid[0];
        const float dy = m.position[1] - formation_.centroid[1];
        const float dz = m.position[2] - formation_.centroid[2];
        const float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 > max_dist_sq)
        {
            max_dist_sq = d2;
        }
    }
    formation_.radius       = std::sqrt(max_dist_sq);
    formation_.member_count = static_cast<uint8_t>(count);
}

}  // namespace cd::ai::squad
