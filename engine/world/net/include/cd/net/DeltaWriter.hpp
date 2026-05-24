// =============================================================================
// CHROMODYNAMIC — cd/net/DeltaWriter.hpp
// Phase 52.A / Wave 220 — byte-level delta encoder against a baseline.
//
// Many network sync protocols transmit deltas: "this 32-byte entity
// state changed by these N bytes since the last ack." DeltaWriter is
// the encoder primitive — given a baseline span and a current span of
// equal length, it emits a compact (offset, value) stream of differing
// byte slots.
//
// Wire format (little-endian):
//   u16 changed_count
//   repeat changed_count times:
//     u16 offset
//     u8  value
//
// For 32-byte payloads with ≤ 4 bytes changed, the delta size is
// 2 + 4*(2+1) = 14 bytes — half the raw 32. The break-even point is
// changed_count < (payload_size - 2) / 3.
//
// `DeltaReader` applies a delta to a baseline buffer.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace cd::net
{

[[nodiscard]] inline std::vector<std::byte>
write_delta(std::span<const std::byte> baseline,
            std::span<const std::byte> current) noexcept
{
    std::vector<std::byte> out;
    if (baseline.size() != current.size()) return out;
    // Count first to reserve.
    std::uint16_t changed = 0;
    for (std::size_t i = 0; i < baseline.size(); ++i)
        if (baseline[i] != current[i]) ++changed;
    out.reserve(2u + static_cast<std::size_t>(changed) * 3u);
    out.push_back(static_cast<std::byte>(changed & 0xFFu));
    out.push_back(static_cast<std::byte>((changed >> 8) & 0xFFu));
    for (std::size_t i = 0; i < baseline.size(); ++i)
    {
        if (baseline[i] != current[i])
        {
            const auto off = static_cast<std::uint16_t>(i);
            out.push_back(static_cast<std::byte>(off & 0xFFu));
            out.push_back(static_cast<std::byte>((off >> 8) & 0xFFu));
            out.push_back(current[i]);
        }
    }
    return out;
}

/// Apply `delta` to `target` in place. `target` should already hold
/// the baseline payload. Returns true on success; false on malformed
/// (out-of-range offset, truncated stream).
inline bool apply_delta(std::span<std::byte> target,
                        std::span<const std::byte> delta) noexcept
{
    if (delta.size() < 2) return false;
    const auto changed = static_cast<std::uint16_t>(
        static_cast<std::uint8_t>(delta[0])
      | (static_cast<std::uint16_t>(static_cast<std::uint8_t>(delta[1])) << 8));
    if (delta.size() < 2u + static_cast<std::size_t>(changed) * 3u) return false;
    for (std::uint16_t i = 0; i < changed; ++i)
    {
        const std::size_t base = 2u + static_cast<std::size_t>(i) * 3u;
        const auto off = static_cast<std::uint16_t>(
            static_cast<std::uint8_t>(delta[base])
          | (static_cast<std::uint16_t>(static_cast<std::uint8_t>(delta[base + 1])) << 8));
        if (off >= target.size()) return false;
        target[off] = delta[base + 2];
    }
    return true;
}

}  // namespace cd::net
