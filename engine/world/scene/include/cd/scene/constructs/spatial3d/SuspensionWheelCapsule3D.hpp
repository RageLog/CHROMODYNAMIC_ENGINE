// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/SuspensionWheelCapsule3D.hpp
// Standalone Concrete C++23 Construct Class #122: `cd::scene::SuspensionWheelCapsule3D`
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

// 122: VehicleWheel3D -> SuspensionWheelCapsule3D
class SuspensionWheelCapsule3D final
    : public ChromaConstructBase<122, ConstructCategory::Spatial3D, ConstructRole::TraitCapsule>
{
public:
    float wheel_radius_m       { 0.35F };
    float suspension_rest_m    { 0.28F };
    float suspension_travel_m  { 0.18F };
    float pacejka_lateral_grip { 1.15F };
    float pacejka_long_grip    { 1.20F };
    bool  steerable            { true };
    bool  driven               { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "SuspensionWheelCapsule3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "VehicleWheel3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "18:46"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "SuspensionWheelCapsule3D + AckermannTire";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<SuspensionWheelCapsule3D>(*this);
    }
};

} // namespace cd::scene
