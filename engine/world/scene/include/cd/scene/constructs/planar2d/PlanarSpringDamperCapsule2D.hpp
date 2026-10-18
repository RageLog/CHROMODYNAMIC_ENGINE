// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/planar2d/PlanarSpringDamperCapsule2D.hpp
// Standalone Concrete C++23 Construct Class #34: `cd::scene::PlanarSpringDamperCapsule2D`
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

// [34] DampedSpringJoint2D -> PlanarSpringDamperCapsule2D (04:41)
class PlanarSpringDamperCapsule2D final
    : public ChromaConstructBase<34, ConstructCategory::Planar2D, ConstructRole::TraitCapsule>
{
public:
    float frequency_hz  { 4.0F };
    float damping_ratio { 0.7F };
    float rest_length   { 48.0F };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "PlanarSpringDamperCapsule2D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "DampedSpringJoint2D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "04:41"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "PlanarSpringDamperCapsule2D + ImplicitSpring"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<PlanarSpringDamperCapsule2D>(*this); }
};

} // namespace cd::scene
