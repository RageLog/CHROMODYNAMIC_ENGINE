// =============================================================================
// CHROMODYNAMIC — cd/render/SortKey.hpp
// Phase 36.A / Wave 204 — packed 64-bit draw-call sort key.
//
// Sort keys are the standard technique (Christer Ericson, "Order your
// graphics draw calls around!", 2008) for ordering an unsorted bucket
// of submissions in a single radix/std::sort pass. We encode every
// dimension that matters for batching + correctness into one 64-bit
// integer with descending significance:
//
//   bit 63 .. 62  layer       (2 bits, 4 layers: opaque/skybox/transparent/ui)
//   bit 61 .. 58  pass        (4 bits, 16 passes inside a layer)
//   bit 57 .. 56  blend group (2 bits, 4 blend modes)
//   bit 55 .. 32  material id (24 bits, 16M materials)
//   bit 31 ..  8  depth bits  (24 bits, normalized depth quantised
//                              front→back; flipped for transparent)
//   bit  7 ..  0  user lo     (8 bits, caller-defined disambiguation)
//
// Sorting ascending order yields: opaque-front-to-back, then transparent
// back-to-front (because the caller flips the depth bits before packing).
//
// The layout is intentionally header-only & inline so call sites can
// build keys inside hot draw-submission loops without function-call
// cost. Validating the layout: bit-counts add to 64 (2+4+2+24+24+8).
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <compare>
#include <cstdint>

namespace cd::render
{

struct SortKey
{
    std::uint64_t value { 0 };

    friend constexpr bool operator==(SortKey, SortKey) noexcept = default;
    friend constexpr auto operator<=>(SortKey, SortKey) noexcept = default;
};

enum class SortLayer : std::uint8_t
{
    kOpaque       = 0,
    kSkybox       = 1,
    kTransparent  = 2,
    kUi           = 3,
};

enum class SortBlend : std::uint8_t
{
    kOff          = 0,
    kAlpha        = 1,
    kAdditive     = 2,
    kPremultiplied = 3,
};

[[nodiscard]] constexpr SortKey make_sort_key(
    SortLayer layer,
    std::uint8_t pass,          // 0..15
    SortBlend blend,
    std::uint32_t material_id,  // 0..0xFFFFFF
    std::uint32_t depth_bits,   // 0..0xFFFFFF (caller pre-flips for transparent)
    std::uint8_t user_lo) noexcept
{
    SortKey k;
    k.value =
        (static_cast<std::uint64_t>(static_cast<std::uint8_t>(layer) & 0x3u) << 62) |
        (static_cast<std::uint64_t>(pass & 0xFu) << 58) |
        (static_cast<std::uint64_t>(static_cast<std::uint8_t>(blend) & 0x3u) << 56) |
        (static_cast<std::uint64_t>(material_id & 0xFFFFFFu) << 32) |
        (static_cast<std::uint64_t>(depth_bits & 0xFFFFFFu) << 8) |
        static_cast<std::uint64_t>(user_lo);
    return k;
}

[[nodiscard]] constexpr SortLayer layer_of(SortKey k) noexcept
{
    return static_cast<SortLayer>((k.value >> 62) & 0x3u);
}

[[nodiscard]] constexpr std::uint32_t material_id_of(SortKey k) noexcept
{
    return static_cast<std::uint32_t>((k.value >> 32) & 0xFFFFFFu);
}

[[nodiscard]] constexpr std::uint32_t depth_bits_of(SortKey k) noexcept
{
    return static_cast<std::uint32_t>((k.value >> 8) & 0xFFFFFFu);
}

}  // namespace cd::render
