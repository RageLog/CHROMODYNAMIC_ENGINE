// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/RoverVessel3D.hpp
// Standalone Concrete C++23 Construct Class #121: `cd::scene::RoverVessel3D`
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

// 121: VehicleBody3D -> RoverVessel3D
class RoverVessel3D final
    : public ChromaConstructBase<121, ConstructCategory::Spatial3D, ConstructRole::Vessel>
{
public:
    float         engine_torque_nm     { 480.0F };
    float         brake_torque_nm      { 620.0F };
    float         max_steer_angle_deg  { 32.0F };
    float         suspension_stiffness { 35.0F };
    float         suspension_damping   { 4.8F };
    float         chassis_mass_kg      { 1250.0F };
    std::uint32_t wheel_count          { 4u };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "RoverVessel3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "VehicleBody3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "18:46"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "RoverVessel3D + SuspensionChassis + CarbonPBR";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<RoverVessel3D>(*this);
    }
};

} // namespace cd::scene
