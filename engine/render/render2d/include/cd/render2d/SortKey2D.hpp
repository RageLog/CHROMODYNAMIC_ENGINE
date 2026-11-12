// =============================================================================
// CHROMODYNAMIC — cd/render2d/SortKey2D.hpp
// FAZ 34 (ADR-20261111) — Deterministic 64-Bit Layer / Z-Index / Y-Sort /
// Material Draw-Call Sort Key for High-Throughput 2D & Isometric Rendering.
// =============================================================================
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>

namespace cd::render2d
{

/// Packs 2D draw order into a monotonic uint64_t so a single fast integer sort
/// orders back-to-front by:
///   [63..56] layer         (0..255, e.g. Background=10, World=100, Foreground=200)
///   [55..40] z_bias        (signed int16 biased by +32768)
///   [39..16] y_sort_fixed  (24-bit fixed-point top-down Y sort when enabled;
///                           higher world Y = further up = drawn earlier/behind,
///                           lower world Y = closer to viewer = drawn later/in front)
///   [15.. 0] material_slot (groups identical texture/material state together)
[[nodiscard]] constexpr std::uint64_t make_sort_key_2d(
    std::uint8_t  layer,
    std::int16_t  z_index,
    float         world_foot_y,
    bool          y_sort_enabled,
    std::uint16_t material_slot = 0U) noexcept
{
    const std::uint64_t layer_bits = static_cast<std::uint64_t>(layer) << 56U;
    const std::uint64_t z_biased   = static_cast<std::uint64_t>(
        static_cast<std::int32_t>(z_index) + 32768) & 0xFFFFULL;
    const std::uint64_t z_bits     = z_biased << 40U;

    std::uint64_t y_bits = 0x800000ULL << 16U;
    if (y_sort_enabled && std::isfinite(world_foot_y))
    {
        // In Y-up world coordinates, larger Y is further "back" (drawn first -> smaller key),
        // smaller Y is closer "front" (drawn later -> larger key).
        const float clamped_y = std::clamp(-world_foot_y, -4096.0F, 4096.0F);
        const float normalized = (clamped_y + 4096.0F) * (16777215.0F / 8192.0F);
        const auto quantized = static_cast<std::uint64_t>(
            std::clamp(normalized, 0.0F, 16777215.0F));
        y_bits = (quantized & 0xFFFFFFULL) << 16U;
    }

    const std::uint64_t mat_bits = static_cast<std::uint64_t>(material_slot);
    return layer_bits | z_bits | y_bits | mat_bits;
}

struct SortableItemRef2D
{
    std::uint64_t sort_key { 0ULL };
    std::uint32_t item_index { 0U };
};

inline void sort_draw_items_2d(std::span<SortableItemRef2D> items) noexcept
{
    std::stable_sort(items.begin(), items.end(), [](const SortableItemRef2D& a, const SortableItemRef2D& b) noexcept {
        return a.sort_key < b.sort_key;
    });
}

}  // namespace cd::render2d
