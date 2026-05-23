// =============================================================================
// CHROMODYNAMIC — cd/io/Crc32.hpp
// Phase 34.A / Wave 202 — CRC-32/IEEE byte-stream checksum.
//
// Implementation note: classic 256-entry lookup table built at compile
// time via `consteval`. Polynomial 0xEDB88320 (reversed 0x04C11DB7),
// matching zlib / png / Ethernet CRC-32. Initial value 0xFFFFFFFF, final
// XOR 0xFFFFFFFF.
//
// `crc32(span)` is a one-shot helper. `Crc32` class is the incremental
// form for streaming readers / hashing assets larger than RAM.
//
// Not cryptographically secure — strictly an integrity check / hash key.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace cd::io
{

namespace detail
{

[[nodiscard]] consteval std::array<std::uint32_t, 256> build_crc32_table() noexcept
{
    std::array<std::uint32_t, 256> t {};
    for (std::uint32_t i = 0; i < 256; ++i)
    {
        std::uint32_t c = i;
        for (int k = 0; k < 8; ++k)
            c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        t[i] = c;
    }
    return t;
}

inline constexpr std::array<std::uint32_t, 256> kCrc32Table = build_crc32_table();

}  // namespace detail

class Crc32
{
public:
    void update(std::span<const std::byte> bytes) noexcept
    {
        std::uint32_t c = state_;
        for (auto b : bytes)
        {
            c = detail::kCrc32Table[(c ^ static_cast<std::uint8_t>(b)) & 0xFFu] ^ (c >> 8);
        }
        state_ = c;
    }

    [[nodiscard]] std::uint32_t value() const noexcept
    {
        return state_ ^ 0xFFFFFFFFu;
    }

    void reset() noexcept { state_ = 0xFFFFFFFFu; }

private:
    std::uint32_t state_ { 0xFFFFFFFFu };
};

[[nodiscard]] inline std::uint32_t crc32(std::span<const std::byte> bytes) noexcept
{
    Crc32 c;
    c.update(bytes);
    return c.value();
}

}  // namespace cd::io
