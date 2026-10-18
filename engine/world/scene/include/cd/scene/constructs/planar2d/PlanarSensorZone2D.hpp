// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/planar2d/PlanarSensorZone2D.hpp
// Standalone Concrete C++23 Construct Class #37: `cd::scene::PlanarSensorZone2D`
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

// [37] Area2D -> PlanarSensorZone2D (05:10)
class PlanarSensorZone2D final
    : public ChromaConstructBase<37, ConstructCategory::Planar2D, ConstructRole::Element>
{
public:
    float gravity_override { 980.0F };
    bool  monitoring       { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "PlanarSensorZone2D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "Area2D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "05:10"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "PlanarSensorZone2D + TriggerVolume"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<PlanarSensorZone2D>(*this); }
};

} // namespace cd::scene
