// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/OpenXrHandPilot3D.hpp
// Standalone Concrete C++23 Construct Class #156: `cd::scene::OpenXrHandPilot3D`
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

// 156: XRController3D -> OpenXrHandPilot3D
class OpenXrHandPilot3D final
    : public ChromaConstructBase<156, ConstructCategory::Spatial3D, ConstructRole::Pilot>
{
public:
    std::uint8_t hand_side_index       { 0u };
    float        grab_magnet_radius_m  { 0.25F };
    float        laser_pointer_range_m { 10.0F };
    float        haptic_amplitude      { 0.65F };
    bool         skeletal_hand_tracked { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "OpenXrHandPilot3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "XRController3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "25:08"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "OpenXrHandPilot3D + PhysicsGrabber";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<OpenXrHandPilot3D>(*this);
    }
};

} // namespace cd::scene
