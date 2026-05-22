// =============================================================================
// CHROMODYNAMIC — cd/io/Endian.hpp
// ADR-017 P3 (Sprint S2.6) — wire-format byte order helpers.
//
// All cd::io binary streams use **little-endian** on the wire. This file
// provides byte-swap + host↔LE conversions for fixed-size integer/float types.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace cd::io
{

namespace detail
{

template <class T>
[[nodiscard]] constexpr T bswap(T v) noexcept
{
    static_assert(std::is_integral_v<T> && !std::is_same_v<T, bool>, "bswap requires an integral non-bool type");
    if constexpr (sizeof(T) == 1)
    {
        return v;
    }
    else if constexpr (sizeof(T) == 2)
    {
        auto u = static_cast<std::uint16_t>(v);
        return static_cast<T>(static_cast<std::uint16_t>((u << 8) | (u >> 8)));
    }
    else if constexpr (sizeof(T) == 4)
    {
        auto u = static_cast<std::uint32_t>(v);
        u = ((u & 0x00FF00FFu) << 8) | ((u & 0xFF00FF00u) >> 8);
        u = (u << 16) | (u >> 16);
        return static_cast<T>(u);
    }
    else if constexpr (sizeof(T) == 8)
    {
        auto u = static_cast<std::uint64_t>(v);
        u = ((u & 0x00FF00FF00FF00FFULL) << 8) | ((u & 0xFF00FF00FF00FF00ULL) >> 8);
        u = ((u & 0x0000FFFF0000FFFFULL) << 16) | ((u & 0xFFFF0000FFFF0000ULL) >> 16);
        return static_cast<T>((u << 32) | (u >> 32));
    }
    else
    {
        static_assert(sizeof(T) <= 8, "Unsupported integer width");
        return v;
    }
}

}  // namespace detail

template <class T>
[[nodiscard]] constexpr T to_little(T v) noexcept
{
    if constexpr (std::endian::native == std::endian::little)
        return v;
    else
        return detail::bswap(v);
}

template <class T>
[[nodiscard]] constexpr T from_little(T v) noexcept
{
    return to_little(v);  // symmetric
}

/// Write a trivially-copyable value into `dst` as little-endian. Caller must
/// ensure `dst` has at least sizeof(T) writable bytes.
template <class T>
void store_le(std::byte* dst, T value) noexcept
{
    static_assert(std::is_trivially_copyable_v<T>);
    if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>)
    {
        auto le = to_little(value);
        std::memcpy(dst, &le, sizeof(T));
    }
    else
    {
        // Floats: bit-cast to integer of the same width and swap.
        if constexpr (sizeof(T) == 4)
        {
            auto bits = std::bit_cast<std::uint32_t>(value);
            auto le = to_little(bits);
            std::memcpy(dst, &le, sizeof(T));
        }
        else if constexpr (sizeof(T) == 8)
        {
            auto bits = std::bit_cast<std::uint64_t>(value);
            auto le = to_little(bits);
            std::memcpy(dst, &le, sizeof(T));
        }
        else
        {
            std::memcpy(dst, &value, sizeof(T));
        }
    }
}

/// Read a trivially-copyable value from `src` interpreted as little-endian.
template <class T>
[[nodiscard]] T load_le(const std::byte* src) noexcept
{
    static_assert(std::is_trivially_copyable_v<T>);
    if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>)
    {
        T tmp {};
        std::memcpy(&tmp, src, sizeof(T));
        return from_little(tmp);
    }
    else if constexpr (std::is_same_v<T, float>)
    {
        std::uint32_t bits {};
        std::memcpy(&bits, src, sizeof(bits));
        return std::bit_cast<float>(from_little(bits));
    }
    else if constexpr (std::is_same_v<T, double>)
    {
        std::uint64_t bits {};
        std::memcpy(&bits, src, sizeof(bits));
        return std::bit_cast<double>(from_little(bits));
    }
    else
    {
        T tmp {};
        std::memcpy(&tmp, src, sizeof(T));
        return tmp;
    }
}

}  // namespace cd::io
