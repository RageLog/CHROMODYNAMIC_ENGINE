// =============================================================================
// CHROMODYNAMIC — cd/net/QoSTier.hpp
// Phase 87.A / Wave 255 — channel quality-of-service tiers.
//
// Multiplayer channels carry traffic with different latency / reliability
// budgets. The send scheduler reads the `QoSTier` per channel and
// prioritizes / drops accordingly.
//
//   * kCritical — voice / control input — reliable, low-latency, never
//                 drop.
//   * kHigh     — gameplay events — reliable.
//   * kNormal   — entity state — best-effort, drop on congestion.
//   * kLow      — analytics, telemetry — best-effort, defer freely.
//
// `tier_priority(t)` returns an integer where higher = more urgent
// (the send scheduler picks the highest-priority pending packet).
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstdint>

namespace cd::net
{

enum class QoSTier : std::uint8_t
{
    kLow      = 0,
    kNormal   = 1,
    kHigh     = 2,
    kCritical = 3,
};

[[nodiscard]] constexpr std::uint32_t tier_priority(QoSTier t) noexcept
{
    return static_cast<std::uint32_t>(t);
}

[[nodiscard]] constexpr bool may_drop_on_congestion(QoSTier t) noexcept
{
    return t == QoSTier::kLow || t == QoSTier::kNormal;
}

[[nodiscard]] constexpr bool requires_reliable_delivery(QoSTier t) noexcept
{
    return t == QoSTier::kCritical || t == QoSTier::kHigh;
}

}  // namespace cd::net
