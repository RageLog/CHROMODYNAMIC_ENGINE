// =============================================================================
// CHROMODYNAMIC — cd/anim/BoneMask.hpp
// Phase 69.A / Wave 237 — per-bone weight mask for partial-body blends.
//
// Locomotion runs on the whole skeleton; aim/reload overlays should
// only apply to the upper body. `BoneMask` stores a per-bone weight
// ∈ [0, 1] that scales the contribution of one animation source
// inside a `BlendTree2` or `AdditiveBlend` call.
//
//   mask.set_weight(spine, 1.0F);   // full upper-body weight
//   mask.set_weight(legs, 0.0F);    // ignore the layer on legs
//
// The mask is a plain vector keyed by bone index — caller knows the
// index → joint-name mapping. `weight(i)` returns the stored value
// or 1.0 (full weight) when `i` is out of range, matching the
// "uninitialized bones contribute fully" convention.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <algorithm>
#include <cstddef>
#include <vector>

namespace cd::anim
{

class BoneMask
{
public:
    explicit BoneMask(std::size_t bone_count = 0)
        : weights_(bone_count, 1.0F) {}

    void resize(std::size_t bone_count)
    {
        weights_.resize(bone_count, 1.0F);
    }

    void set_weight(std::size_t bone_index, float weight) noexcept
    {
        if (bone_index >= weights_.size()) return;
        weights_[bone_index] = std::clamp(weight, 0.0F, 1.0F);
    }

    [[nodiscard]] float weight(std::size_t bone_index) const noexcept
    {
        return (bone_index < weights_.size()) ? weights_[bone_index] : 1.0F;
    }

    [[nodiscard]] std::size_t size() const noexcept { return weights_.size(); }

    void fill(float w) noexcept
    {
        std::fill(weights_.begin(), weights_.end(), std::clamp(w, 0.0F, 1.0F));
    }

    [[nodiscard]] const std::vector<float>& weights() const noexcept { return weights_; }

private:
    std::vector<float> weights_;
};

}  // namespace cd::anim
