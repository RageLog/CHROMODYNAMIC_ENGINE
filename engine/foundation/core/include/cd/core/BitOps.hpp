// =============================================================================
// CHROMODYNAMIC — cd/core/BitOps.hpp
// Phase 28.B / Wave 197 — bit manipulation helpers.
//
// Constexpr wrappers over <bit> (C++20). Provides:
//   * popcount(x)     — count of set bits
//   * count_lzero(x)  — leading zero count
//   * count_tzero(x)  — trailing zero count
//   * next_pow2(x)    — smallest power-of-two >= x (1 if x == 0)
//   * is_pow2(x)      — true iff x is a non-zero power of two
//   * align_up(x, a)  — round x up to multiple of a (a must be pow2)
//
// Used by allocators (FrameAllocator, Pool), hash table sizing, and
// SIMD lane masks. Centralizing here so call sites don't re-roll
// "(x + a - 1) & ~(a - 1)" with subtle bugs.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <bit>
#include <cstdint>

namespace cd::core
{

[[nodiscard]] constexpr int popcount(std::uint64_t x) noexcept
{
    return std::popcount(x);
}

[[nodiscard]] constexpr int count_lzero(std::uint64_t x) noexcept
{
    return std::countl_zero(x);
}

[[nodiscard]] constexpr int count_tzero(std::uint64_t x) noexcept
{
    return std::countr_zero(x);
}

[[nodiscard]] constexpr bool is_pow2(std::uint64_t x) noexcept
{
    return x != 0 && (x & (x - 1)) == 0;
}

[[nodiscard]] constexpr std::uint64_t next_pow2(std::uint64_t x) noexcept
{
    if (x <= 1) return 1;
    return std::bit_ceil(x);
}

/// Round `x` up to the nearest multiple of `align`. `align` MUST be a
/// power of two — UB otherwise (guarded only in debug via assert in
/// callers; this primitive stays branch-free).
[[nodiscard]] constexpr std::uint64_t align_up(std::uint64_t x,
                                               std::uint64_t align) noexcept
{
    return (x + align - 1) & ~(align - 1);
}

[[nodiscard]] constexpr std::uint64_t align_down(std::uint64_t x,
                                                 std::uint64_t align) noexcept
{
    return x & ~(align - 1);
}

}  // namespace cd::core
