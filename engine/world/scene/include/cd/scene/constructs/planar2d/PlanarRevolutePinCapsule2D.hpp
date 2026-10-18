// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/planar2d/PlanarRevolutePinCapsule2D.hpp
// Standalone Concrete C++23 Construct Class #36: `cd::scene::PlanarRevolutePinCapsule2D`
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

// [36] PinJoint2D -> PlanarRevolutePinCapsule2D (05:02)
class PlanarRevolutePinCapsule2D final
    : public ChromaConstructBase<36, ConstructCategory::Planar2D, ConstructRole::TraitCapsule>
{
public:
    float lower_angle_deg { -45.0F };
    float upper_angle_deg { 45.0F };
    bool  limits_enabled  { false };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "PlanarRevolutePinCapsule2D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "PinJoint2D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "05:02"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "PlanarRevolutePinCapsule2D + AngularLimits"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<PlanarRevolutePinCapsule2D>(*this); }
};

} // namespace cd::scene
