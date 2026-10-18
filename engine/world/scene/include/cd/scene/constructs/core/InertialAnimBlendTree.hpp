// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/core/InertialAnimBlendTree.hpp
// Standalone Concrete C++23 Construct Class #14: `cd::scene::InertialAnimBlendTree`
// =============================================================================
#pragma once

#include <cd/scene/constructs/IChromaConstruct.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace cd::scene
{

// [14] AnimationTree -> InertialAnimBlendTree (39:38)
class InertialAnimBlendTree final
    : public ChromaConstructBase<14, ConstructCategory::CoreSystem, ConstructRole::TraitCapsule>
{
public:
    float inertial_blend_sec { 0.18F };
    float stride_speed_scale { 1.0F };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "InertialAnimBlendTree"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "AnimationTree"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "39:38"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "InertialAnimBlendTree + SkinnedAvatar"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<InertialAnimBlendTree>(*this); }
};

} // namespace cd::scene
