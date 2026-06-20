// =============================================================================
// CHROMODYNAMIC — cd/core/Bitset.hpp
// Phase 33.B / Wave 201 — small fixed-capacity bitset.
//
// `std::bitset<N>` is fine but:
//   * It's templated on N but doesn't expose `popcount` over a range
//     in constexpr context until C++26.
//   * No `find_first_set` / `for_each_set` iteration helpers.
//   * Visual Studio's debug-iterator instrumentation interacts poorly
//     with our `-Werror -Wconversion` baseline on some paths.
//
// `cd::core::Bitset<N>` is a thin wrapper over an array of uint64
// words that exposes the iteration helpers our render-mask / asset-flag
// call sites need. Bit indexing is little-endian (bit 0 = least
// significant of word 0) — matches std::bitset.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>

namespace cd::core
{

template <std::size_t N>
class Bitset
{
public:
    static constexpr std::size_t kBitsPerWord = 64;
    static constexpr std::size_t kWordCount = (N + kBitsPerWord - 1) / kBitsPerWord;

    constexpr Bitset() noexcept = default;  // words_ default-inits to all-zero

    constexpr void set(std::size_t i) noexcept
    {
        words_[i / kBitsPerWord] |= (1ULL << (i % kBitsPerWord));
    }

    constexpr void clear(std::size_t i) noexcept
    {
        words_[i / kBitsPerWord] &= ~(1ULL << (i % kBitsPerWord));
    }

    [[nodiscard]] constexpr bool test(std::size_t i) const noexcept
    {
        return (words_[i / kBitsPerWord] >> (i % kBitsPerWord)) & 1ULL;
    }

    constexpr void reset() noexcept
    {
        for (auto& w : words_) w = 0;
    }

    [[nodiscard]] constexpr std::size_t count() const noexcept
    {
        std::size_t total = 0;
        for (auto w : words_) total += static_cast<std::size_t>(std::popcount(w));
        return total;
    }

    [[nodiscard]] constexpr bool any() const noexcept
    {
        return std::ranges::any_of(words_, [](auto w) { return w != 0; });
    }

    [[nodiscard]] constexpr bool none() const noexcept { return !any(); }

    /// Returns N if no bit is set (N is out-of-range sentinel).
    [[nodiscard]] constexpr std::size_t find_first_set() const noexcept
    {
        for (std::size_t i = 0; i < kWordCount; ++i)
        {
            if (words_[i] != 0)
            {
                return i * kBitsPerWord + static_cast<std::size_t>(std::countr_zero(words_[i]));
            }
        }
        return N;
    }

    /// Invoke `fn(index)` for each set bit, in ascending index order.
    template <class F>
    constexpr void for_each_set(F&& fn) const
    {
        for (std::size_t i = 0; i < kWordCount; ++i)
        {
            std::uint64_t w = words_[i];
            while (w != 0)
            {
                const auto bit = static_cast<std::size_t>(std::countr_zero(w));
                fn(i * kBitsPerWord + bit);
                w &= w - 1;
            }
        }
    }

    [[nodiscard]] constexpr std::size_t size() const noexcept { return N; }

private:
    std::array<std::uint64_t, kWordCount> words_ {};
};

}  // namespace cd::core
