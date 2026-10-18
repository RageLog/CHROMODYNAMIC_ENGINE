// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/planar2d/PlanarRadianceTint2D.hpp
// Standalone Concrete C++23 Construct Class #43: `cd::scene::PlanarRadianceTint2D`
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

// [43] CanvasModulate -> PlanarRadianceTint2D (06:29)
class PlanarRadianceTint2D final
    : public ChromaConstructBase<43, ConstructCategory::Planar2D, ConstructRole::TraitCapsule>
{
public:
    float color_temperature_k { 6500.0F };
    float exposure_ev         { 0.0F };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "PlanarRadianceTint2D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "CanvasModulate"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "06:29"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "PlanarRadianceTint2D + KelvinTint"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<PlanarRadianceTint2D>(*this); }
};

} // namespace cd::scene
