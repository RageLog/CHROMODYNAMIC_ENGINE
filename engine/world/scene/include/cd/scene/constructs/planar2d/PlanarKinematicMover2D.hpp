// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/planar2d/PlanarKinematicMover2D.hpp
// Standalone Concrete C++23 Construct Class #30: `cd::scene::PlanarKinematicMover2D`
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

// [30] AnimatableBody2D -> PlanarKinematicMover2D (03:53)
class PlanarKinematicMover2D final
    : public ChromaConstructBase<30, ConstructCategory::Planar2D, ConstructRole::Element>
{
public:
    float amplitude    { 64.0F };
    float frequency_hz { 0.5F };
    bool  carry_riders { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "PlanarKinematicMover2D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "AnimatableBody2D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "03:53"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "PlanarKinematicMover2D + RiderCarry"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<PlanarKinematicMover2D>(*this); }
};

} // namespace cd::scene
