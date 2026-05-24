// =============================================================================
// CHROMODYNAMIC — cd/net/RleCodec.hpp
// Phase 70.A / Wave 238 — run-length encode/decode for byte buffers.
//
// Sparse network payloads (entity state deltas, terrain heightmap
// streams) often hold long runs of identical bytes. Classic RLE:
//
//   encode:  zero-fill 64 bytes → (count=64, value=0) — 2 bytes
//   encode:  one mixed byte then 30 zeros → 2 (mixed, zeros) — 4 bytes
//
// Wire format:
//   [u8 count (1..255)] [u8 value]   per run
//   count == 0 reserved (signal "end of stream" if caller wants it;
//   this codec doesn't insert it — produce only count ∈ [1, 255]).
//
// `decode(rle) → bytes` always succeeds for well-formed input (even
// count). Truncated streams return whatever was decoded so far.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cd::net
{

[[nodiscard]] inline std::vector<std::uint8_t>
rle_encode(const std::vector<std::uint8_t>& src)
{
    std::vector<std::uint8_t> out;
    out.reserve(src.size());
    std::size_t i = 0;
    while (i < src.size())
    {
        const std::uint8_t v = src[i];
        std::size_t j = i + 1;
        while (j < src.size() && src[j] == v && (j - i) < 255) ++j;
        out.push_back(static_cast<std::uint8_t>(j - i));
        out.push_back(v);
        i = j;
    }
    return out;
}

[[nodiscard]] inline std::vector<std::uint8_t>
rle_decode(const std::vector<std::uint8_t>& src)
{
    std::vector<std::uint8_t> out;
    out.reserve(src.size() * 2);
    for (std::size_t i = 0; i + 1 < src.size(); i += 2)
    {
        const std::uint8_t count = src[i];
        const std::uint8_t value = src[i + 1];
        for (std::uint8_t k = 0; k < count; ++k) out.push_back(value);
    }
    return out;
}

}  // namespace cd::net
