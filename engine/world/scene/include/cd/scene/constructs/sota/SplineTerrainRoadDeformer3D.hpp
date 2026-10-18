// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/sota/SplineTerrainRoadDeformer3D.hpp
// Standalone Concrete C++23 Construct Class #240: `cd::scene::SplineTerrainRoadDeformer3D`
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

// 240: SplineTerrainRoadDeformer3D
class SplineTerrainRoadDeformer3D final
    : public ChromaConstructBase<240, ConstructCategory::NextGenSOTA, ConstructRole::Element>
{
public:
    float road_width_m        { 6.0F };
    float embankment_falloff  { 3.5F };
    bool  auto_paint_splatmap { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "SplineTerrainRoadDeformer3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "SplineTerrainRoadDeformer3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "42:00+ (Beyond Godot SOTA)"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "SplineTerrainRoadDeformer3D + SplineCarve + CarbonRoad"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<SplineTerrainRoadDeformer3D>(*this); }
};

} // namespace cd::scene
