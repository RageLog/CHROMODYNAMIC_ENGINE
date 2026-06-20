// =============================================================================
// CHROMODYNAMIC — cd/game/ai_director/AiDirector.cpp
// Phase 620 — cd::game::ai_director implementation.
//
// Design references: see AiDirector.hpp header.
// =============================================================================
#include <cd/game/ai_director/AiDirector.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace cd::game::ai_director
{

// -----------------------------------------------------------------------------
// configure_templates
// -----------------------------------------------------------------------------
void AiDirector::configure_templates(std::span<const EncounterTemplate> templates)
{
    templates_.assign(templates.begin(), templates.end());
}

// -----------------------------------------------------------------------------
// notify_player_event
// -----------------------------------------------------------------------------
void AiDirector::notify_player_event(std::string_view /*event_id*/,
                                     float             intensity_delta)
{
    intensity_ += intensity_delta;
    // Clamp to [0, 1].
    intensity_ = std::clamp(intensity_, 0.0F, 1.0F);

    // Accumulate score for positive pressure events.
    if (intensity_delta > 0.0F)
    {
        // Saturating multiply with a FLOAT-domain guard: a huge intensity_delta
        // (e.g. 1e18F) is UB if cast straight to uint32_t, so compare in float
        // first and saturate before any narrowing cast.
        constexpr uint32_t kMaxU32        = std::numeric_limits<uint32_t>::max();
        constexpr uint32_t kMultiplyGuard = kMaxU32 / kScorePerUnit;
        const float        floored        = std::floor(intensity_delta);
        uint32_t           score_add      = kMaxU32;
        if (floored < static_cast<float>(kMultiplyGuard))
        {
            // Safe: floored < guard < UINT32_MAX, so both the cast and the
            // multiply stay in range.
            score_add = static_cast<uint32_t>(floored) * kScorePerUnit;
        }
        const uint32_t headroom  = kMaxU32 - player_score_;
        player_score_ += (score_add <= headroom) ? score_add : headroom;
    }

    update_tension_state();
}

// -----------------------------------------------------------------------------
// tick
// -----------------------------------------------------------------------------
void AiDirector::tick(float dt_ms)
{
    if (dt_ms <= 0.0F) { return; }

    // Apply intensity decay.
    float decay = kDecayRatePerMs * dt_ms;
    if (tension_ == TensionState::kRelief)
    {
        decay += kReliefDecayBonus * dt_ms;
    }
    intensity_ = std::max(0.0F, intensity_ - decay);

    // Advance time-in-state.
    time_in_state_ms_ += dt_ms;

    update_tension_state();
}

// -----------------------------------------------------------------------------
// state
// -----------------------------------------------------------------------------
DirectorState AiDirector::state() const noexcept
{
    return DirectorState{
        .tension         = tension_,
        .intensity       = intensity_,
        .time_in_state_ms = time_in_state_ms_,
        .player_score    = player_score_,
    };
}

// -----------------------------------------------------------------------------
// request_next_encounter
// -----------------------------------------------------------------------------
std::optional<EncounterTemplate> AiDirector::request_next_encounter() const
{
    if (templates_.empty()) { return std::nullopt; }
    if (intensity_ <= 0.0F) { return std::nullopt; }

    // Determine the highest tier available at the current intensity.
    // Find the maximum configured tier first.
    uint32_t max_configured_tier = 0;
    for (const auto& t : templates_)
    {
        max_configured_tier = std::max(max_configured_tier, t.difficulty_tier);
    }

    const auto max_tier = static_cast<uint32_t>(
        std::floor(intensity_ * static_cast<float>(max_configured_tier)));

    // Pick the template with the highest tier that does not exceed max_tier.
    // On ties, prefer the first in insertion order (stable).
    const EncounterTemplate* best = nullptr;
    for (const auto& t : templates_)
    {
        if (t.difficulty_tier > max_tier) { continue; }
        if (best == nullptr || t.difficulty_tier > best->difficulty_tier)
        {
            best = &t;
        }
    }

    if (best == nullptr) { return std::nullopt; }
    return *best;
}

// -----------------------------------------------------------------------------
// update_tension_state (private)
// -----------------------------------------------------------------------------
void AiDirector::update_tension_state()
{
    TensionState new_state{};

    if (tension_ == TensionState::kRelief)
    {
        // In kRelief we stay until intensity drops below kThresholdBuildUp,
        // then transition directly back to kIdle (L4D "Director rest" model).
        if (intensity_ < kThresholdBuildUp)
        {
            new_state = TensionState::kIdle;
        }
        else
        {
            new_state = TensionState::kRelief;
        }
    }
    else
    {
        // Forward-only threshold machine: kIdle → kBuildUp → kPeak → kRelief.
        if (intensity_ >= kThresholdRelief)
        {
            new_state = TensionState::kRelief;
        }
        else if (intensity_ >= kThresholdPeak)
        {
            new_state = TensionState::kPeak;
        }
        else if (intensity_ >= kThresholdBuildUp)
        {
            new_state = TensionState::kBuildUp;
        }
        else
        {
            new_state = TensionState::kIdle;
        }
    }

    if (new_state != tension_)
    {
        tension_          = new_state;
        time_in_state_ms_ = 0.0F;
    }
}

}  // namespace cd::game::ai_director
