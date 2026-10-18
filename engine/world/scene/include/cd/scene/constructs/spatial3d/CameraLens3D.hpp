// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/CameraLens3D.hpp
// Standalone Concrete C++23 Construct Class #119: `cd::scene::CameraLens3D`
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

// 119: Camera3D -> CameraLens3D
class CameraLens3D final
    : public ChromaConstructBase<119, ConstructCategory::Spatial3D, ConstructRole::Element>
{
public:
    float         vertical_fov_deg   { 65.0F };
    float         near_plane_m       { 0.05F };
    float         aperture_fstop     { 2.8F };
    float         shutter_speed_sec  { 1.0F / 125.0F };
    float         iso_sensitivity    { 100.0F };
    float         focus_distance_m   { 10.0F };
    bool          reversed_z_infinite { true };
    bool          taa_jitter_enabled { true };
    std::uint32_t active_bookmark    { 0u };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "CameraLens3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "Camera3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "18:22"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "CameraLens3D + PhysicalOptics + Bookmarks";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<CameraLens3D>(*this);
    }
};

} // namespace cd::scene
