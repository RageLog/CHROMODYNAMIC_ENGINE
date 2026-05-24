// =============================================================================
// CHROMODYNAMIC — cd/net/SequenceId.hpp
// Phase 74.A / Wave 242 — wrap-around-aware sequence number comparison.
//
// Packet sequence numbers eventually wrap around (e.g. uint16 cycles
// every 65536 packets ≈ 18 minutes at 60Hz). Naive `<` comparison
// breaks at the wrap boundary. Glenn Fiedler's standard fix:
//
//   seq_greater_than(a, b) ≡ a > b in "half-of-the-space" distance:
//     (a > b && a - b ≤ N/2) || (a < b && b - a > N/2)
//
// where N = 2^bits. Returns true iff `a` is the newer of two values.
//
// `seq_distance(a, b)` returns signed gap in [-N/2, N/2-1] — useful
// for "how many packets behind are we" telemetry.
//
// Template on integer type — uint16 = 64K window; uint32 = 4B window.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstdint>
#include <type_traits>

namespace cd::net
{

template <class T>
[[nodiscard]] constexpr bool seq_greater_than(T a, T b) noexcept
{
    static_assert(std::is_unsigned_v<T>, "SequenceId requires unsigned T");
    constexpr T kHalf = T { 1 } << (sizeof(T) * 8 - 1);
    return ((a > b) && (a - b <= kHalf))
        || ((a < b) && (b - a >  kHalf));
}

template <class T>
[[nodiscard]] constexpr T seq_distance(T a, T b) noexcept
{
    static_assert(std::is_unsigned_v<T>, "SequenceId requires unsigned T");
    return static_cast<T>(a - b);
}

}  // namespace cd::net
