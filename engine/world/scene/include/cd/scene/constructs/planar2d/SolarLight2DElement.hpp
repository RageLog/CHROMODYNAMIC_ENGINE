// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/planar2d/SolarLight2DElement.hpp
// Standalone Concrete C++23 Construct Class #46: `cd::scene::SolarLight2DElement`
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

// [46] DirectionalLight2D -> SolarLight2DElement (07:10)
class SolarLight2DElement final
    : public ChromaConstructBase<46, ConstructCategory::Planar2D, ConstructRole::Element>
{
public:
    float sun_angle_deg      { 35.0F };
    float shadow_max_dist_px { 1024.0F };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "SolarLight2DElement"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "DirectionalLight2D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "07:10"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "SolarLight2DElement + SunElevation"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<SolarLight2DElement>(*this); }
};

} // namespace cd::scene
