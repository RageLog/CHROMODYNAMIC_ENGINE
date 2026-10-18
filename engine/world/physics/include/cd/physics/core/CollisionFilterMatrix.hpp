// =============================================================================
// CHROMODYNAMIC — cd/physics/core/CollisionFilterMatrix.hpp
// 32x32 symmetric layer bitmatrix + category/mask/group collision filter.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <array>
#include <cstdint>

namespace cd::physics::core
{

struct CollisionFilter
{
    std::uint16_t category_layer { 0u };
    std::uint32_t category_bits { 1u };
    std::uint32_t mask_bits { 0xFFFFFFFFu };
    std::int16_t  group_index { 0 };

    friend constexpr bool operator==(const CollisionFilter&, const CollisionFilter&) noexcept = default;
};

class CollisionFilterMatrix
{
public:
    static constexpr std::uint16_t kMaxLayers = 32u;

    constexpr CollisionFilterMatrix() noexcept
    {
        enable_all();
    }

    constexpr void enable_all() noexcept
    {
        for (auto& row : layer_masks_)
        {
            row = 0xFFFFFFFFu;
        }
    }

    constexpr void disable_all() noexcept
    {
        for (auto& row : layer_masks_)
        {
            row = 0u;
        }
    }

    constexpr void set_layer_collision(std::uint16_t layer_a, std::uint16_t layer_b, bool collide) noexcept
    {
        if (layer_a >= kMaxLayers || layer_b >= kMaxLayers)
        {
            return;
        }
        const std::uint32_t bit_a = 1u << static_cast<std::uint32_t>(layer_a);
        const std::uint32_t bit_b = 1u << static_cast<std::uint32_t>(layer_b);
        if (collide)
        {
            layer_masks_[layer_a] |= bit_b;
            layer_masks_[layer_b] |= bit_a;
        }
        else
        {
            layer_masks_[layer_a] &= ~bit_b;
            layer_masks_[layer_b] &= ~bit_a;
        }
    }

    [[nodiscard]] constexpr bool can_layers_collide(std::uint16_t layer_a, std::uint16_t layer_b) const noexcept
    {
        if (layer_a >= kMaxLayers || layer_b >= kMaxLayers)
        {
            return false;
        }
        const std::uint32_t bit_b = 1u << static_cast<std::uint32_t>(layer_b);
        return (layer_masks_[layer_a] & bit_b) != 0u;
    }

    [[nodiscard]] constexpr bool should_collide(const CollisionFilter& a, const CollisionFilter& b) const noexcept
    {
        if (a.group_index != 0 && a.group_index == b.group_index)
        {
            return a.group_index > 0;
        }
        if (!can_layers_collide(a.category_layer, b.category_layer))
        {
            return false;
        }
        const bool a_accepts_b = (a.mask_bits & b.category_bits) != 0u;
        const bool b_accepts_a = (b.mask_bits & a.category_bits) != 0u;
        return a_accepts_b && b_accepts_a;
    }

private:
    std::array<std::uint32_t, kMaxLayers> layer_masks_ {};
};

}  // namespace cd::physics::core
